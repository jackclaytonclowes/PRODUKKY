// host_smoke.cpp — the plugin as a host sees it: parameters exposed, latency
// declared, blocks run, presets recalled, state round-tripped, and — the part
// no DSP test can reach — a real transport driving the feel section, because
// swing is the one thing here that depends entirely on the host's grid.
//
// It also paints the editor into a PNG, which is the only way to look at the
// interface without a DAW.
//
//   cmake --build build --target host_smoke && ./build/host_smoke_artefacts/Release/host_smoke
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Presets.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <typeinfo>
#include <functional>

static int passed = 0;
static juce::StringArray failures;
static void check(const juce::String& name, bool ok, const juce::String& detail = {}){
    if (ok){ ++passed; std::printf("  ok    %s\n", name.toRawUTF8()); }
    else {
        failures.add(name + (detail.isEmpty() ? "" : " — " + detail));
        std::printf("  FAIL  %s%s\n", name.toRawUTF8(),
                    detail.isEmpty() ? "" : (" — " + detail).toRawUTF8());
    }
}

// a transport, so the grid-locked half of the plugin has something to lock to
class TestPlayHead : public juce::AudioPlayHead {
public:
    TestPlayHead(double sampleRate, double bpm) : sr(sampleRate), tempo(bpm) {}
    juce::Optional<PositionInfo> getPosition() const override {
        PositionInfo p;
        p.setIsPlaying(playing);
        p.setBpm(tempo);
        p.setTimeInSamples(samples);
        p.setTimeInSeconds(samples / sr);
        p.setPpqPosition(samples / sr * tempo / 60.0);
        return p;
    }
    void advance(int n){ samples += n; }
    bool playing = true;
    int64_t samples = 0;
private:
    double sr, tempo;
};

static bool runBlocks(CrateProcessor& p, TestPlayHead& ph, int blocks, int blockSize,
                      double& peak, long& bad){
    juce::AudioBuffer<float> buffer(2, blockSize);
    juce::MidiBuffer midi;
    juce::Random rng(4242);
    peak = 0.0; bad = 0;
    for (int b = 0; b < blocks; ++b){
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < blockSize; ++i)
                buffer.setSample(ch, i, rng.nextFloat() - 0.5f);
        p.processBlock(buffer, midi);
        ph.advance(blockSize);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < blockSize; ++i){
                const float v = buffer.getSample(ch, i);
                if (!std::isfinite(v)) ++bad;
                else peak = juce::jmax(peak, std::abs(static_cast<double>(v)));
            }
    }
    return bad == 0;
}

int main(int argc, char** argv){
    juce::ScopedJuceInitialiser_GUI juceInit;
    const juce::String shotPath = argc > 1 ? argv[1] : "crate-plugin.png";

    std::printf("\nInstantiation\n");
    CrateProcessor proc;
    check("parameters exposed to the host",
          proc.getParameters().size() == crate::Params::get().count(),
          juce::String(proc.getParameters().size()) + " of "
              + juce::String(crate::Params::get().count()));
    proc.setPlayConfigDetails(2, 2, 48000.0, 256);
    proc.prepareToPlay(48000.0, 256);
    check("latency reported to the host", proc.getLatencySamples() > 0,
          juce::String(proc.getLatencySamples()) + " samples");

    TestPlayHead ph(48000.0, 93.0);
    proc.setPlayHead(&ph);

    double peak = 0.0; long bad = 0;
    check("runs blocks without producing NaN", runBlocks(proc, ph, 40, 256, peak, bad),
          juce::String(bad) + " non-finite");
    check("output stays inside full scale", peak <= 1.0, juce::String(peak, 3));

    std::printf("\nTransport\n");
    {
        // with swing up and the transport running, the plugin must see the grid
        if (auto* sw = proc.apvts.getParameter("swing")) sw->setValueNotifyingHost(sw->convertTo0to1(66.0f));
        ph.playing = true;
        runBlocks(proc, ph, 20, 256, peak, bad);
        check("the host transport reaches the engine",
              proc.transportRunning.load() && std::abs(proc.hostBpm.load() - 93.0f) < 0.01f,
              juce::String(proc.hostBpm.load(), 1) + " BPM, playing "
                  + (proc.transportRunning.load() ? "yes" : "no"));
        ph.playing = false;
        runBlocks(proc, ph, 4, 256, peak, bad);
        check("and it notices when the transport stops", !proc.transportRunning.load());
        ph.playing = true;
    }
    {
        // the grid setting changes the base delay, and the host has to be told
        const int before = proc.getLatencySamples();
        if (auto* g = proc.apvts.getParameter("grid")) g->setValueNotifyingHost(0.0f);   // 1/8
        runBlocks(proc, ph, 2, 256, peak, bad);
        const int after = proc.getLatencySamples();
        check("changing the grid re-declares the latency", after != before && after > 0,
              juce::String(before) + " → " + juce::String(after) + " samples");
        if (auto* g = proc.apvts.getParameter("grid")) g->setValueNotifyingHost(0.5f);   // back to 1/16
        runBlocks(proc, ph, 2, 256, peak, bad);
    }
    {
        // so does Lookahead, by exactly 5 ms, and back again
        const int before = proc.getLatencySamples();
        auto* lk = proc.apvts.getParameter("look");
        if (lk) lk->setValueNotifyingHost(1.0f);
        runBlocks(proc, ph, 2, 256, peak, bad);
        const int on = proc.getLatencySamples();
        if (lk) lk->setValueNotifyingHost(0.0f);
        runBlocks(proc, ph, 2, 256, peak, bad);
        check("Lookahead re-declares the latency: 5 ms more while it is on",
              on - before == 240 && proc.getLatencySamples() == before,
              juce::String(before) + " → " + juce::String(on) + " → " + juce::String(proc.getLatencySamples()) + " samples");
    }

    std::printf("\nPresets\n");
    check("the host sees every preset", proc.getNumPrograms() == static_cast<int>(crate::presets().size()),
          juce::String(proc.getNumPrograms()));
    for (int i = 0; i < proc.getNumPrograms(); ++i){
        proc.setCurrentProgram(i);
        const bool ok = runBlocks(proc, ph, 12, 256, peak, bad);
        check("preset \"" + proc.getProgramName(i) + "\"", ok && peak <= 1.0,
              "bad " + juce::String(bad) + " peak " + juce::String(peak, 3));
    }

    {   // every name reaches the host as written: UTF-8, dashes and all, not
        // read as ASCII into mojibake (which is how they used to arrive)
        const auto& all = crate::presets();
        juce::String wrong; bool anyDash = false;
        for (int i = 0; i < proc.getNumPrograms(); ++i){
            const juce::String n = proc.getProgramName(i);
            if (n.toStdString() != std::string(all[static_cast<size_t>(i)].name)
                || n.containsChar(static_cast<juce::juce_wchar>(0xE2)) || n.containsChar(static_cast<juce::juce_wchar>(0xC2)))
                wrong << " [" << n << "]";
            anyDash = anyDash || n.containsChar(static_cast<juce::juce_wchar>(0x2014));
        }
        check("every preset name reaches the host exactly as written", wrong.isEmpty(), wrong);
        (void)anyDash;
    }

    std::printf("\nState\n");
    proc.setCurrentProgram(3);
    juce::MemoryBlock state;
    proc.getStateInformation(state);
    const float before = proc.apvts.getRawParameterValue("dust")->load();
    proc.setCurrentProgram(0);
    proc.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    check("state round-trips through the host",
          std::abs(proc.apvts.getRawParameterValue("dust")->load() - before) < 1.0e-4f,
          juce::String(proc.apvts.getRawParameterValue("dust")->load())
              + " vs " + juce::String(before));

    std::printf("\nUndo, A/B and your presets\n");
    {
        CrateProcessor sp;
        auto& S = *sp.session;
        auto get = [&](const char* id){ return sp.apvts.getRawParameterValue(id)->load(); };
        auto drag = [&](const char* id, std::initializer_list<float> path){
            auto* prm = sp.apvts.getParameter(id);
            prm->beginChangeGesture();
            for (float v : path) prm->setValueNotifyingHost(prm->convertTo0to1(v));
            prm->endChangeGesture();
        };
        auto near = [](float a, float b){ return std::abs(a - b) < 0.02f; };
        const float dust0 = get("dust");

        check("a fresh instance has nothing to undo", !S.canUndo() && !S.canRedo());
        drag("dust", { 10.0f, 25.0f, 40.0f });
        check("a whole knob drag is one undo step", S.undoDepth() == 1 && near(get("dust"), 40.0f));
        S.undo();
        check("undo puts the knob back", near(get("dust"), dust0));
        S.redo();
        check("redo brings the change back", near(get("dust"), 40.0f));

        sp.setCurrentProgram(8);                                   // an S900 preset
        const bool s900 = static_cast<int>(get("machine")) == 1;
        S.undo();
        check("a preset load is one step, and undo leaves the preset",
              s900 && static_cast<int>(get("machine")) == 0 && near(get("dust"), 40.0f));

        drag("swing", { 58.0f });
        S.selectSlot(1);
        check("B opens as a copy of A", S.activeSlot() == 1 && near(get("swing"), 58.0f));
        drag("swing", { 64.0f });
        drag("bits", { 8.0f });
        S.selectSlot(0);
        check("A comes back as A", near(get("swing"), 58.0f) && near(get("bits"), 12.0f));
        S.selectSlot(1);
        check("B comes back as B", near(get("swing"), 64.0f) && near(get("bits"), 8.0f));

        juce::MemoryBlock st;
        sp.getStateInformation(st);
        CrateProcessor rp;
        rp.setStateInformation(st.getData(), static_cast<int>(st.getSize()));
        auto rget = [&](const char* id){ return rp.apvts.getRawParameterValue(id)->load(); };
        check("a session saved on B reopens on B", rp.session->activeSlot() == 1 && near(rget("bits"), 8.0f));
        check("  ... with A still held", (rp.session->selectSlot(0), near(rget("bits"), 12.0f) && near(rget("swing"), 58.0f)));
        check("  ... and the compare slot does not end up in the parameter tree",
              !rp.apvts.state.getChildWithName(session::Session::tag()).isValid());

        const auto dir = juce::File::createTempFile("crate-presets");
        sp.userPresets.setFolder(dir);
        drag("machine", { 1.0f });
        const bool saved = sp.saveUserPreset(dir.getChildFile("Dark S900.json"));
        const juce::String text = dir.getChildFile("Dark S900.json").loadFileAsString();
        check("your presets save as readable files, choices by name",
              saved && text.contains("\"machine\": \"S900\"") && text.contains("\"bits\": 8"), text.substring(0, 80));
        sp.setCurrentProgram(0);
        check("a saved preset loads", sp.loadUserPreset(dir.getChildFile("Dark S900.json"))
              && static_cast<int>(get("machine")) == 1 && near(get("bits"), 8.0f) && near(get("swing"), 64.0f)
              && sp.userPresetName() == "Dark S900");
        check("  ... and what it leaves out goes back to default", near(get("inGain"), 0.0f) && near(get("tune"), 0.0f));
        S.undo();
        check("  ... and is one undo step", static_cast<int>(get("machine")) == 0);
        check("a file that is not a patch is refused", !sp.loadPatch("not json") && !sp.loadPatch("{\"nothing\": 1}"));
        dir.deleteRecursively();
    }

    std::printf("\nEditor\n");
    const auto presetDir = juce::File::createTempFile("crate-editor-presets");
    proc.userPresets.setFolder(presetDir);
    {
        proc.setCurrentProgram(4);                       // something with feel dialled in
        std::unique_ptr<juce::AudioProcessorEditor> editor(proc.createEditor());
        check("editor is created", editor != nullptr);
        if (editor != nullptr){
            const int w = editor->getWidth(), h = editor->getHeight();
            std::printf("        editor size %d x %d\n", w, h);
            check("editor comes up big enough to work in", w >= 600 && h >= 340,
                  juce::String(w) + "x" + juce::String(h));
            // the bits-in-use meter, with a tone 12 dB under full scale playing:
            // it reads what the converter is given, Input included
            {
                juce::AudioBuffer<float> tone(2, 256); juce::MidiBuffer none;
                for (int blk = 0; blk < 8; ++blk){
                    for (int ch = 0; ch < 2; ++ch)
                        for (int i = 0; i < 256; ++i)
                            tone.setSample(ch, i, 0.25f * std::sin(2.0f * juce::MathConstants<float>::pi * 1000.0f * (blk * 256 + i) / 48000.0f));
                    proc.processBlock(tone, none);
                }
                std::function<BitsMeter*(juce::Component*)> findMeter = [&](juce::Component* c) -> BitsMeter* {
                    if (auto* m = dynamic_cast<BitsMeter*>(c)) return m;
                    for (auto* ch : c->getChildren()) if (auto* r = findMeter(ch)) return r;
                    return nullptr;
                };
                if (auto* m = findMeter(editor.get())){
                    m->refresh();
                    const double gain = juce::Decibels::decibelsToGain(proc.apvts.getRawParameterValue("inGain")->load());
                    const double want = crate::bitsInUse(proc.apvts.getRawParameterValue("bits")->load(), 0.25 * gain);
                    check("the bits meter reads what the converter is given", std::abs(m->shown() - want) < 0.05,
                          juce::String(m->shown(), 2) + " bits, want " + juce::String(want, 2));
                } else check("the bits meter is on the panel", false);
            }
            juce::Image shot(juce::Image::ARGB, w, h, true);
            {
                juce::Graphics g(shot);
                editor->paintEntireComponent(g, true);
            }
            int lit = 0;
            for (int y = 0; y < shot.getHeight(); y += 4)
                for (int x = 0; x < shot.getWidth(); x += 4)
                    if (shot.getPixelAt(x, y).getBrightness() > 0.05f) ++lit;
            check("editor renders something", lit > 10000, juce::String(lit) + " lit samples");

            // Shrinking the window must shrink the interface, not crop it: the
            // bottom-right corner of the design has to still be drawn. Render at
            // half size and compare the far corner against a blank one.
            {
                editor->setSize(540, 310);
                const int sw = editor->getWidth(), sh = editor->getHeight();
                juce::Image small(juce::Image::ARGB, sw, sh, true);
                {
                    juce::Graphics g(small);
                    editor->paintEntireComponent(g, true);
                }
                auto inked = [&](juce::Rectangle<int> r){
                    int n = 0;
                    for (int y = r.getY(); y < r.getBottom(); ++y)
                        for (int x = r.getX(); x < r.getRight(); ++x)
                            if (small.getPixelAt(x, y).getBrightness() < 0.75f) ++n;
                    return n;
                };
                const int corner = inked({ sw - sw / 4, sh - sh / 4, sw / 4, sh / 4 });
                check("shrunk editor still draws its bottom-right corner", corner > 200,
                      juce::String(sw) + "x" + juce::String(sh) + ", "
                          + juce::String(corner) + " inked pixels in the corner");
                {   // written next to the full-size shot: layout regressions do not
                    // fail assertions, they have to be looked at
                    juce::File half(juce::File::getCurrentWorkingDirectory()
                                        .getChildFile(shotPath).withFileExtension("")
                                        .getFullPathName() + "-small.png");
                    half.deleteFile();
                    if (auto stream = std::unique_ptr<juce::FileOutputStream>(half.createOutputStream())){
                        juce::PNGImageFormat png;
                        png.writeImageToStream(small, *stream);
                    }
                }
                // what a user's drag is held to: the window's own resize
                // constraint must be the design's ratio. (This used to compare
                // the size it had just set against a hard-coded 1080 x 620,
                // which passed whatever the design was.)
                const double want = w / (double) h;
                const double fixed = editor->getConstrainer() != nullptr
                                   ? editor->getConstrainer()->getFixedAspectRatio() : 0.0;
                check("resizing is held to the design's proportions", std::abs(fixed - want) < 0.01,
                      juce::String(fixed, 3) + " vs " + juce::String(want, 3));
                editor->setSize(w, h);
            }

            juce::File file(juce::File::getCurrentWorkingDirectory().getChildFile(shotPath));
            file.deleteFile();                            // createOutputStream appends
            if (auto stream = std::unique_ptr<juce::FileOutputStream>(file.createOutputStream())){
                juce::PNGImageFormat png;
                png.writeImageToStream(shot, *stream);
                std::printf("  screenshot %s\n", file.getFullPathName().toRawUTF8());
            }

            // the Tips switch and the built-in guide
            if (auto* ce = dynamic_cast<CrateEditor*>(editor.get())){
                const bool was = ce->tipsOn();
                ce->setTips(false);
                const bool off = !ce->tipsOn();
                ce->setTips(true);
                check("the Tips switch turns tooltips off and on", off && ce->tipsOn());
                ce->setTips(was);                            // leave the preference as it was

                ce->setGuideOpen(true);
                const int gh = ce->guideHeight();
                check("the guide opens, laid out from GUIDE.md, longer than the window so it scrolls",
                      gh > h, juce::String(gh) + " px of guide");
                juce::Image gshot(juce::Image::ARGB, w, h, true);
                {
                    juce::Graphics g(gshot);
                    editor->paintEntireComponent(g, true);
                }
                juce::File gfile(juce::File::getCurrentWorkingDirectory().getChildFile(shotPath)
                                     .withFileExtension("").getFullPathName() + "-guide.png");
                gfile.deleteFile();
                if (auto stream = std::unique_ptr<juce::FileOutputStream>(gfile.createOutputStream())){
                    juce::PNGImageFormat png;
                    png.writeImageToStream(gshot, *stream);
                }
                ce->setGuideOpen(false);
                check("and closes again", ce->guideHeight() == 0);
                {
                auto src = juce::Desktop::getInstance().getMainMouseSource();
                auto ev = [&](juce::Component* c, juce::Point<float> down, juce::Point<float> at){
                    return juce::MouseEvent(src, at, {}, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, c, c,
                                            juce::Time::getCurrentTime(), down, juce::Time::getCurrentTime(), 1, false);
                };
                auto get = [&](const char* id){ return proc.apvts.getRawParameterValue(id)->load(); };
                auto setP = [&](const char* id, float v){
                    if (auto* prm = proc.apvts.getParameter(id)) prm->setValueNotifyingHost(prm->convertTo0to1(v)); };
                // typing a value: click the readout, type, Enter. Each is one
                // undo step; text it cannot read leaves the box open and the
                // value alone; Escape puts it away unchanged
                {
                    std::function<void(juce::Component*, std::vector<KnobBox*>&)> knobs =
                        [&](juce::Component* c, std::vector<KnobBox*>& out){
                            if (auto* k = dynamic_cast<KnobBox*>(c)) out.push_back(k);
                            for (auto* ch : c->getChildren()) knobs(ch, out);
                        };
                    std::vector<KnobBox*> all; knobs(editor.get(), all);
                    auto knob = [&](const char* id) -> KnobBox* {
                        for (auto* k : all) if (k->id() == id) return k;
                        return nullptr;
                    };
                    auto type = [&](KnobBox* k, const char* text, bool enter = true){
                        const auto at = k->valueArea().getCentre().toFloat();
                        k->mouseDown(ev(k, at, at));
                        juce::TextEditor* box = nullptr;
                        for (auto* ch : k->getChildren())
                            if (auto* te = dynamic_cast<juce::TextEditor*>(ch)) box = te;
                        if (box == nullptr || !box->isVisible()) return false;
                        box->setText(text, false);
                        box->keyPressed(juce::KeyPress(enter ? juce::KeyPress::returnKey : juce::KeyPress::escapeKey));
                        // the text box answers Return and Escape with a posted message
                        juce::MessageManager::getInstance()->runDispatchLoopUntil(30);
                        return true;
                    };
                    auto* S = proc.session.get();
                    struct Case { const char* id; const char* text; float want; };
                    const Case cases[] = { { "fltFreq", "2.5k", 2500.0f }, { "fltFreq", "800 Hz", 800.0f }, { "fltDecay", "120 ms", 120.0f },
                                           { "fltDrive", "2.5x", 2.5f }, { "tune", "-3", -3.0f },
                                           { "outGain", "-6 dB", -6.0f }, { "outGain", "+40", 12.0f } };
                    juce::String bad;
                    for (const auto& c : cases){
                        auto* k = knob(c.id);
                        const size_t depth = S->undoDepth();
                        if (k == nullptr){ bad << " [" << c.id << ": no knob]"; continue; }
                        if (!type(k, c.text)){ bad << " [" << c.id << ": no box]"; continue; }
                        const float got = get(c.id);
                        if (std::abs(got - c.want) > 0.01f * std::max(1.0f, std::abs(c.want)) || S->undoDepth() != depth + 1)
                            bad << " [" << c.id << " '" << c.text << "': " << juce::String(got, 3)
                                << ", " << (int) (S->undoDepth() - depth) << " steps]";
                    }
                    check("typing a value sets it, in the panel's units, as one undo step", bad.isEmpty(), bad);
                    if (auto* k = knob("outGain")){
                        setP("outGain", 1.0f);
                        type(k, "loud");
                        bool open = false;
                        for (auto* ch : k->getChildren())
                            if (auto* te = dynamic_cast<juce::TextEditor*>(ch)) open = te->isVisible();
                        type(k, "-9", false);
                        check("text it cannot read is refused, and Escape changes nothing",
                              open && std::abs(get("outGain") - 1.0f) < 0.01f, juce::String(get("outGain"), 2));
                    }
                }
                }

                // the header: your presets in the menu, and the undo and A/B strip
                std::function<juce::Component*(juce::Component*, const std::type_info&)> findT =
                    [&](juce::Component* c, const std::type_info& t) -> juce::Component* {
                        if (typeid(*c) == t) return c;
                        for (auto* ch : c->getChildren()) if (auto* r = findT(ch, t)) return r;
                        return nullptr;
                    };
                // saved only now, so the screenshots above show a factory preset's name
                proc.saveUserPreset(presetDir.getChildFile("My break.json"));
                if (auto* box = dynamic_cast<session::PresetBox*>(findT(editor.get(), typeid(session::PresetBox)))){
                    box->beforePopup();
                    bool listed = false;
                    for (int i = 0; i < box->getNumItems(); ++i) listed |= box->getItemText(i) == "My break";
                    check("the preset menu lists your presets under the factory ones", listed);
                    box->setSelectedId(3, juce::sendNotificationSync);            // by id: the menu is grouped
                    check("  ... and choosing a factory one from it still loads it", proc.getCurrentProgram() == 2);
                    // headings, and favourites: add the preset showing, find it
                    // at the top under Favourites, load it from there, take it off
                    {
                        int headings = 0;
                        for (juce::PopupMenu::MenuItemIterator it(*box->getRootMenu()); it.next();)
                            if (it.getItem().isSectionHeader) ++headings;
                        check("the menu groups the factory presets under headings", headings >= 5, juce::String(headings) + " headings");
                        const int fav = 6;
                        box->setSelectedId(fav + 1, juce::sendNotificationSync);
                        auto idOf = [&](const juce::String& text){
                            for (int i = 0; i < box->getNumItems(); ++i) if (box->getItemText(i) == text) return box->getItemId(i);
                            return 0;
                        };
                        box->setSelectedId(idOf("Add this preset to favourites"), juce::sendNotificationSync);
                        const auto file = proc.userPresets.favouritesFile();
                        const juce::String name = proc.getProgramName(fav);
                        const bool saved = file.loadFileAsString().contains("f:" + name);
                        box->beforePopup();
                        const bool onTop = box->getNumItems() > 0 && box->getItemText(0) == name
                                           && box->getItemId(0) == session::PresetMenu::favouriteId(0);
                        box->setSelectedId(1, juce::sendNotificationSync);          // somewhere else
                        box->setSelectedId(session::PresetMenu::favouriteId(0), juce::sendNotificationSync);
                        const bool loads = proc.getCurrentProgram() == fav;
                        box->setSelectedId(idOf("Remove this preset from favourites"), juce::sendNotificationSync);
                        box->beforePopup();
                        const bool gone = !file.loadFileAsString().contains("f:" + name) && box->getItemText(0) != name;
                        check("a favourite is saved, listed first, loads from there, and comes off again",
                              saved && onTop && loads && gone,
                              juce::String(saved ? "" : "not saved ") + (onTop ? "" : "not on top ") + (loads ? "" : "does not load ") + (gone ? "" : "not removed"));
                    }
                    box->setSelectedId(3, juce::sendNotificationSync);

                    // the arrows either side of the menu, one preset at a time
                    auto ids = [&]{ return box->getSelectedId(); };
                    (void)ids;
                    std::vector<juce::Component*> arrows;
                    std::function<void(juce::Component*)> collect = [&](juce::Component* c){
                        if (dynamic_cast<session::ArrowButton*>(c)) arrows.push_back(c);
                        for (auto* ch : c->getChildren()) collect(ch);
                    };
                    collect(editor.get());
                    session::ArrowButton* prevB = nullptr; session::ArrowButton* nextB = nullptr;
                    for (auto* a : arrows){
                        auto* b = static_cast<session::ArrowButton*>(a);
                        if (b->getName() == "Next preset") nextB = b; else prevB = b;
                    }
                    check("there are previous and next arrows by the preset menu", prevB && nextB
                          && prevB->isVisible() && nextB->isVisible() && prevB->getRight() <= box->getX()
                          && nextB->getX() >= box->getRight());
                    if (prevB && nextB){
                        const int n = proc.getNumPrograms();
                        proc.setCurrentProgram(4);                 // inside the Machines group, which steps 3, 4, 5
                        nextB->triggerClick(); juce::MessageManager::getInstance()->runDispatchLoopUntil(30);
                        const bool fwd = proc.getCurrentProgram() == 5;
                        prevB->triggerClick(); prevB->triggerClick(); juce::MessageManager::getInstance()->runDispatchLoopUntil(30);
                        const bool back = proc.getCurrentProgram() == 3;
                        check("  ... next and previous step one preset each way", fwd && back,
                              "now " + juce::String(proc.getCurrentProgram()));
                        // in the menu's order, not the list's: headings come in the order
                        // their first preset does, so Character (SP, 45 on 33 is the third
                        // preset) is followed by Filter rhythm

                        proc.setCurrentProgram(25);
                        nextB->triggerClick(); juce::MessageManager::getInstance()->runDispatchLoopUntil(30);
                        check("  ... in the order the menu shows them, group after group",
                              proc.getProgramName(proc.getCurrentProgram()) == "Rhythm: gated sixteenths",
                              "now " + proc.getProgramName(proc.getCurrentProgram()));
                        proc.setCurrentProgram(n - 1);
                        nextB->triggerClick(); juce::MessageManager::getInstance()->runDispatchLoopUntil(30);
                        check("  ... past the last factory preset comes one of yours", proc.userPresetName() == "My break",
                              proc.userPresetName());
                        nextB->triggerClick(); juce::MessageManager::getInstance()->runDispatchLoopUntil(30);
                        check("  ... and past the last of yours it goes round to the first",
                              proc.getCurrentProgram() == 0 && proc.userPresetName().isEmpty());
                        prevB->triggerClick(); juce::MessageManager::getInstance()->runDispatchLoopUntil(30);
                        check("  ... and back again from the first", proc.userPresetName() == "My break");
                        proc.setCurrentProgram(0);
                    }
                } else check("the editor has a preset menu", false);
                auto* bar = ce->sessionBar.get();
                auto* prm = proc.apvts.getParameter("dust");
                const float was0 = prm->getValue();
                prm->beginChangeGesture(); prm->setValueNotifyingHost(0.9f); prm->endChangeGesture();
                bar->undo.triggerClick();
                juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
                check("the Undo button takes back a knob move", std::abs(prm->getValue() - was0) < 1.0e-4f);
                check("Ctrl or Cmd + Shift + Z is redo",
                      ce->keyPressed(juce::KeyPress('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0))
                      && std::abs(prm->getValue() - 0.9f) < 1.0e-3f);
                bar->slotB.triggerClick();
                juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
                bar->refresh();
                check("the B button switches to B and lights", proc.session->activeSlot() == 1
                      && bar->slotB.getToggleState() && bar->copy.getButtonText() == "B to A");
                bar->slotA.triggerClick();
                juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
            } else check("the editor is a CrateEditor", false);
        }
    }

    presetDir.deleteRecursively();
    proc.setPlayHead(nullptr);
    proc.releaseResources();
    std::printf("\n%d assertions passed, %d failed\n", passed, failures.size());
    for (const auto& f : failures) std::printf("  - %s\n", f.toRawUTF8());
    return failures.isEmpty() ? 0 : 1;
}
