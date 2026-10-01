// host_smoke.cpp — exercises the plugin the way a host does: prepare, run
// blocks, walk the presets, round-trip the state, import a browser patch, and
// render the editor into a PNG. The picture matters as much as the assertions:
// the DSP tests cannot tell you the interface came out looking right.
//
//   cmake --build build --target host_smoke && ./build/host_smoke shot.png
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "FactoryPresets.h"
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

static bool runBlocks(FractureProcessor& p, int blocks, int blockSize, double& peak, long& bad){
    juce::AudioBuffer<float> buffer(2, blockSize);
    juce::MidiBuffer midi;
    juce::Random rng(4242);
    peak = 0.0; bad = 0;
    for (int b = 0; b < blocks; ++b){
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < blockSize; ++i)
                buffer.setSample(ch, i, rng.nextFloat() - 0.5f);
        p.processBlock(buffer, midi);
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
    const juce::String shotPath = argc > 1 ? argv[1] : "fracture-plugin.png";

    std::printf("\nInstantiation\n");
    FractureProcessor proc;
    check("parameters exposed to the host",
          proc.getParameters().size() == fracture::Params::get().count(),
          juce::String(proc.getParameters().size()) + " of "
              + juce::String(fracture::Params::get().count()));
    proc.setPlayConfigDetails(2, 2, 48000.0, 256);
    proc.prepareToPlay(48000.0, 256);
    check("latency reported to the host", proc.getLatencySamples() > 0,
          juce::String(proc.getLatencySamples()) + " samples");

    double peak = 0.0; long bad = 0;
    check("runs blocks without producing NaN", runBlocks(proc, 40, 256, peak, bad),
          juce::String(bad) + " non-finite");
    check("output stays inside full scale", peak <= 1.0, juce::String(peak, 3));

    std::printf("\nPresets\n");
    // the host must see the whole menu: the browser's presets and the plugin's own
    check("the host sees every factory preset",
          proc.getNumPrograms() == static_cast<int>(fracture::factoryPresets().size())
              && proc.getNumPrograms() > static_cast<int>(fracture::presets().size()),
          juce::String(proc.getNumPrograms()));
    for (int i = 0; i < proc.getNumPrograms(); ++i){
        proc.setCurrentProgram(i);
        const bool ok = runBlocks(proc, 12, 256, peak, bad);
        check("preset \"" + proc.getProgramName(i) + "\"", ok && peak <= 1.0,
              "bad " + juce::String(bad) + " peak " + juce::String(peak, 3));
    }

    {   // every name reaches the host as written: UTF-8, dashes and all, not
        // read as ASCII into mojibake (which is how they used to arrive)
        const auto& all = fracture::factoryPresets();
        juce::String wrong; bool anyDash = false;
        for (int i = 0; i < proc.getNumPrograms(); ++i){
            const juce::String n = proc.getProgramName(i);
            if (n.toStdString() != std::string(all[static_cast<size_t>(i)].name)
                || n.containsChar(static_cast<juce::juce_wchar>(0xE2)) || n.containsChar(static_cast<juce::juce_wchar>(0xC2)))
                wrong << " [" << n << "]";
            anyDash = anyDash || n.containsChar(static_cast<juce::juce_wchar>(0x2014));
        }
        check("every preset name reaches the host exactly as written", wrong.isEmpty(), wrong);
        check("  ... em dashes included", anyDash);
    }

    std::printf("\nState\n");
    proc.setCurrentProgram(3);
    juce::MemoryBlock state;
    proc.getStateInformation(state);
    const float before = proc.apvts.getRawParameterValue("d0a")->load();
    proc.setCurrentProgram(0);
    proc.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    check("state round-trips through the host",
          std::abs(proc.apvts.getRawParameterValue("d0a")->load() - before) < 1.0e-4f,
          juce::String(proc.apvts.getRawParameterValue("d0a")->load()) + " vs " + juce::String(before));

    const juce::String patch = R"({"bands":"2","d0a":18,"m0a":"wrap","sb0":true,"fltType":"bp","fbAmt":40})";
    check("a patch copied from the browser version loads", proc.loadBrowserPatch(patch));
    check("  ... its choice values map by name",
          static_cast<int>(proc.apvts.getRawParameterValue("m0a")->load()) == 9
              && static_cast<int>(proc.apvts.getRawParameterValue("bands")->load()) == 1
              && static_cast<int>(proc.apvts.getRawParameterValue("fltType")->load()) == 3);
    check("  ... its drive value survives the taper",
          std::abs(proc.apvts.getRawParameterValue("d0a")->load() - 18.0f) < 0.05f,
          juce::String(proc.apvts.getRawParameterValue("d0a")->load()));
    const juce::String out = proc.saveBrowserPatch();
    check("  ... and comes back out as the same patch",
          out.contains("\"m0a\": \"wrap\"") && out.contains("\"bands\": \"2\""), out.substring(0, 90));

    check("still finite after all that", runBlocks(proc, 12, 128, peak, bad),
          juce::String(bad) + " non-finite");

    std::printf("\nUndo, A/B and your presets\n");
    {
        FractureProcessor sp;
        auto& S = *sp.session;
        auto get = [&](const char* id){ return sp.apvts.getRawParameterValue(id)->load(); };
        // what a knob does: a gesture around one or more changes
        auto drag = [&](const char* id, std::initializer_list<float> path){
            auto* prm = sp.apvts.getParameter(id);
            prm->beginChangeGesture();
            for (float v : path) prm->setValueNotifyingHost(prm->convertTo0to1(v));
            prm->endChangeGesture();
        };
        auto near = [](float a, float b){ return std::abs(a - b) < 0.02f; };
        const float d0 = get("d0a");

        check("a fresh instance has nothing to undo", !S.canUndo() && !S.canRedo());
        drag("d0a", { 4.0f, 7.0f, 12.0f, 20.0f });
        check("a whole knob drag is one undo step", S.undoDepth() == 1 && near(get("d0a"), 20.0f),
              juce::String(static_cast<int>(S.undoDepth())) + " steps");
        S.undo();
        check("undo puts the knob back", near(get("d0a"), d0), juce::String(get("d0a")));
        S.redo();
        check("redo brings the change back", near(get("d0a"), 20.0f), juce::String(get("d0a")));

        {   // controls that move several parameters in one movement
            const size_t before = S.undoDepth();
            const float step2 = get("rhStep2"), padX = get("xyX");
            auto* s2 = sp.apvts.getParameter("rhStep2"); auto* s3 = sp.apvts.getParameter("rhStep3");
            s2->beginChangeGesture(); s2->setValueNotifyingHost(0.2f);
            s3->beginChangeGesture(); s2->endChangeGesture(); s3->setValueNotifyingHost(0.8f);
            s3->endChangeGesture();
            auto* x = sp.apvts.getParameter("xyX"); auto* y = sp.apvts.getParameter("xyY");
            x->beginChangeGesture(); y->beginChangeGesture();
            x->setValueNotifyingHost(0.3f); y->setValueNotifyingHost(0.7f);
            x->endChangeGesture(); y->endChangeGesture();
            check("a stroke across the steps, and a drag on the pad, are one step each",
                  S.undoDepth() == before + 2, juce::String(static_cast<int>(S.undoDepth() - before)) + " steps");
            S.undo(); S.undo();
            check("  ... and undo takes each back whole", near(get("rhStep2"), step2) && near(get("xyX"), padX)
                  && !near(step2, 20.0f));
        }

        sp.setCurrentProgram(5);
        const float presetDrive = get("d0a");
        S.undo();
        check("a preset load is one step, and undo leaves the preset", near(get("d0a"), 20.0f)
              && !near(presetDrive, 20.0f), juce::String(get("d0a")));
        S.redo();

        const juce::String patch = R"({"d0a":9,"fbAmt":30})";
        check("a paste is one undo step", sp.pastePatch(patch) && near(get("d0a"), 9.0f));
        S.undo();
        check("  ... and undo takes the whole paste back", near(get("d0a"), presetDrive)
              && !near(get("fbAmt"), 30.0f));

        drag("d0a", { 10.0f });
        S.selectSlot(1);
        check("B opens as a copy of A", S.activeSlot() == 1 && near(get("d0a"), 10.0f));
        drag("d0a", { 30.0f });
        drag("fbAmt", { 55.0f });
        S.selectSlot(0);
        check("A comes back as A", near(get("d0a"), 10.0f) && !near(get("fbAmt"), 55.0f));
        S.selectSlot(1);
        check("B comes back as B", near(get("d0a"), 30.0f) && near(get("fbAmt"), 55.0f));
        S.undo();
        check("undo on B takes back B's last change, not the switch",
              S.activeSlot() == 1 && near(get("d0a"), 30.0f) && !near(get("fbAmt"), 55.0f));
        S.redo();

        juce::MemoryBlock st;
        sp.getStateInformation(st);
        FractureProcessor rp;
        auto rget = [&](const char* id){ return rp.apvts.getRawParameterValue(id)->load(); };
        rp.setStateInformation(st.getData(), static_cast<int>(st.getSize()));
        check("a session saved on B reopens on B", rp.session->activeSlot() == 1 && near(rget("d0a"), 30.0f));
        check("  ... with A still held", (rp.session->selectSlot(0), near(rget("d0a"), 10.0f)));
        check("  ... and with nothing to undo from before it was opened", !rp.session->canUndo());
        check("  ... and the compare slot does not end up in the parameter tree",
              !rp.apvts.state.getChildWithName(session::Session::tag()).isValid());
        if (auto xml = sp.apvts.copyState().createXml()){          // as an older build would have saved it
            juce::MemoryBlock old;
            juce::AudioProcessor::copyXmlToBinary(*xml, old);
            FractureProcessor op;
            op.setStateInformation(old.getData(), static_cast<int>(old.getSize()));
            check("a session saved before A/B existed opens on A",
                  op.session->activeSlot() == 0 && std::abs(op.apvts.getRawParameterValue("d0a")->load() - 30.0f) < 0.02f);
        }

        const auto dir = juce::File::createTempFile("fracture-presets");
        sp.userPresets.setFolder(dir);
        drag("d0a", { 14.0f });
        const bool s10 = sp.saveUserPreset(dir.getChildFile("Kick 10.json"));
        drag("d0a", { 3.0f });
        const bool s2 = sp.saveUserPreset(dir.getChildFile("Kick 2.json"));
        const auto files = sp.userPresets.list();
        check("your presets save as files", s10 && s2 && files.size() == 2);
        check("  ... listed the way people number them (2 before 10)",
              files.size() == 2 && files[0].getFileName() == "Kick 2.json");
        check("  ... as browser patches, so they paste into the browser version too",
              juce::JSON::parse(dir.getChildFile("Kick 10.json").loadFileAsString()).getDynamicObject() != nullptr
                  && dir.getChildFile("Kick 10.json").loadFileAsString().contains("\"d0a\""));
        check("a saved preset loads", sp.loadUserPreset(dir.getChildFile("Kick 10.json"))
              && near(get("d0a"), 14.0f) && sp.userPresetName() == "Kick 10");
        S.undo();
        check("  ... and is one undo step", near(get("d0a"), 3.0f));
        check("a file that is not a patch is refused", !sp.loadUserPreset(dir.getChildFile("missing.json")));
        check("a typed name is made safe for a file", session::UserPresets::safeName(" a/b:c ") == "abc"
              && session::UserPresets::safeName("   ") == "Untitled",
              session::UserPresets::safeName(" a/b:c "));
        sp.setCurrentProgram(0);
        check("choosing a factory preset stops showing yours", sp.userPresetName().isEmpty());
        dir.deleteRecursively();
    }

    std::printf("\nEditor\n");
    const auto presetDir = juce::File::createTempFile("fracture-editor-presets");
    proc.userPresets.setFolder(presetDir);
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor(proc.createEditor());
        check("editor is created", editor != nullptr);
        if (editor != nullptr){
            const int w = editor->getWidth(), h = editor->getHeight();
            std::printf("        editor size %d x %d\n", w, h);
            check("editor comes up big enough to work in",
                  w >= 600 && h >= 600, juce::String(w) + "x" + juce::String(h));
            juce::Image shot(juce::Image::ARGB, w, h, true);
            {
                juce::Graphics g(shot);
                editor->paintEntireComponent(g, true);
            }
            // a blank render would mean the paint path never ran
            int lit = 0;
            for (int y = 0; y < shot.getHeight(); y += 4)
                for (int x = 0; x < shot.getWidth(); x += 4)
                    if (shot.getPixelAt(x, y).getBrightness() > 0.05f) ++lit;
            check("editor renders something", lit > 20000, juce::String(lit) + " lit samples");

            // Shrinking the window must shrink the interface, not crop it: the
            // bottom-right corner of the design has to still be drawn. Render at
            // half size and compare the far corner against a blank one.
            {
                editor->setSize(w / 2, h / 2);
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
                // the size it had just set against a hard-coded 1320 x 1190,
                // which passed whatever the design was.)
                const double want = w / (double) h;
                const double fixed = editor->getConstrainer() != nullptr
                                   ? editor->getConstrainer()->getFixedAspectRatio() : 0.0;
                check("resizing is held to the design's proportions", std::abs(fixed - want) < 0.01,
                      juce::String(fixed, 3) + " vs " + juce::String(want, 3));
                editor->setSize(w, h);
            }

            juce::File file(juce::File::getCurrentWorkingDirectory().getChildFile(shotPath));
            file.deleteFile();                       // createOutputStream appends
            if (auto stream = std::unique_ptr<juce::FileOutputStream>(file.createOutputStream())){
                juce::PNGImageFormat png;
                png.writeImageToStream(shot, *stream);
                std::printf("  screenshot %s\n", file.getFullPathName().toRawUTF8());
            }

            // the Scope's harmonic readout: a 440 Hz tone through Tube in one
            // band, and the line names the note and a 2nd harmonic in dB
            {
                std::function<Scope*(juce::Component*)> findScope = [&](juce::Component* c) -> Scope* {
                    if (auto* sc = dynamic_cast<Scope*>(c)) return sc;
                    for (auto* ch : c->getChildren()) if (auto* r = findScope(ch)) return r;
                    return nullptr;
                };
                auto setP = [&](const char* id, float v){
                    if (auto* prm = proc.apvts.getParameter(id)) prm->setValueNotifyingHost(prm->convertTo0to1(v)); };
                setP("bands", 0); setP("m0a", static_cast<float>(fracture::Mode::Tube)); setP("d0a", 4.0f);
                setP("sb0", 0.0f); setP("fltType", 0.0f); setP("crMix", 0.0f); setP("fbAmt", 0.0f);
                // the processor writes a frame only once the last was taken, so
                // take whatever earlier tests left before playing the tone
                if (auto* sc = findScope(editor.get())) sc->refresh();
                juce::AudioBuffer<float> tone(2, 512); juce::MidiBuffer none; int t = 0;
                for (int blk = 0; blk < 24; ++blk){
                    for (int ch = 0; ch < 2; ++ch)
                        for (int i = 0; i < 512; ++i)
                            tone.setSample(ch, i, 0.4f * std::sin(2.0f * juce::MathConstants<float>::pi * 440.0f * (t + i) / 48000.0f));
                    t += 512;
                    proc.processBlock(tone, none);
                }
                if (auto* sc = findScope(editor.get())){
                    sc->refresh();
                    const juce::String text = sc->readoutText();
                    check("the Scope reads the note and the harmonics Tube adds",
                          text.startsWith("A4") && text.contains("2nd -") && !text.contains("2nd - "), text);
                    auto img = sc->createComponentSnapshot(sc->getLocalBounds(), true, 2.0f);
                    juce::File f(juce::File::getCurrentWorkingDirectory().getChildFile(shotPath).withFileExtension("")
                                     .getFullPathName() + "-scope.png");
                    f.deleteFile();
                    if (auto st = std::unique_ptr<juce::FileOutputStream>(f.createOutputStream()))
                        juce::PNGImageFormat().writeImageToStream(img, *st);
                } else check("the Scope is on the panel", false);
            }

            // the mouse controls, driven with real mouse events: nothing else in
            // the suite touches them, and a drag that moved the wrong parameter
            // or the wrong way would pass every other check
            {
                std::function<juce::Component*(juce::Component*, const std::type_info&)> find =
                    [&](juce::Component* c, const std::type_info& t) -> juce::Component* {
                        if (typeid(*c) == t) return c;
                        for (auto* ch : c->getChildren()) if (auto* r = find(ch, t)) return r;
                        return nullptr;
                    };
                auto src = juce::Desktop::getInstance().getMainMouseSource();
                auto ev = [&](juce::Component* c, juce::Point<float> down, juce::Point<float> at,
                              juce::ModifierKeys mods = {}, int clicks = 1){
                    return juce::MouseEvent(src, at, mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, c, c,
                                            juce::Time::getCurrentTime(), down, juce::Time::getCurrentTime(),
                                            clicks, at != down);
                };
                auto get = [&](const char* id){ return proc.apvts.getRawParameterValue(id)->load(); };
                auto setP = [&](const char* id, float v){
                    if (auto* prm = proc.apvts.getParameter(id)) prm->setValueNotifyingHost(prm->convertTo0to1(v)); };

                if (auto* fv = find(editor.get(), typeid(FilterView))){
                    setP("fltFreq", 1000.0f); setP("fltQ", 2.0f);
                    const auto c = fv->getLocalBounds().getCentre().toFloat();
                    const float w = (float) fv->getWidth(), hgt = (float) fv->getHeight();
                    fv->mouseDown(ev(fv, c, c));
                    fv->mouseDrag(ev(fv, c, c + juce::Point<float>(w * 0.25f, -hgt * 0.25f)));
                    fv->mouseUp(ev(fv, c, c + juce::Point<float>(w * 0.25f, -hgt * 0.25f)));
                    const float f1 = get("fltFreq"), q1 = get("fltQ");
                    check("dragging the filter display right and up raises cutoff and resonance",
                          f1 > 1500.0f && q1 > 2.5f, juce::String(f1, 0) + " Hz, Q " + juce::String(q1, 2));
                    // shift is a quarter of the move
                    setP("fltFreq", 1000.0f);
                    fv->mouseDown(ev(fv, c, c));
                    fv->mouseDrag(ev(fv, c, c + juce::Point<float>(w * 0.25f, 0.0f), juce::ModifierKeys::shiftModifier));
                    fv->mouseUp(ev(fv, c, c));
                    const float fFine = get("fltFreq");
                    check("and Shift makes it a fine move", fFine > 1000.0f && fFine < f1,
                          juce::String(fFine, 0) + " Hz against " + juce::String(f1, 0));
                    fv->mouseDoubleClick(ev(fv, c, c, {}, 2));
                    check("a double-click puts cutoff and resonance back",
                          std::abs(get("fltFreq") - 1200.0f) < 1.0f && std::abs(get("fltQ") - 0.7f) < 0.01f,
                          juce::String(get("fltFreq"), 0) + " Hz, Q " + juce::String(get("fltQ"), 2));
                } else check("the filter display is on the panel", false);

                if (auto* xy = find(editor.get(), typeid(XYPad))){
                    const auto b = xy->getLocalBounds().toFloat();
                    // the pad's field is inset 14 px; aim at three quarters across, a quarter up
                    const juce::Point<float> at(14.0f + (b.getWidth() - 28.0f) * 0.75f, 14.0f + (b.getHeight() - 28.0f) * 0.75f);
                    xy->mouseDown(ev(xy, at, at));
                    xy->mouseUp(ev(xy, at, at));
                    check("clicking the pad sets X across and Y up from the bottom",
                          std::abs(get("xyX") - 75.0f) < 1.5f && std::abs(get("xyY") - 25.0f) < 1.5f,
                          "X " + juce::String(get("xyX"), 1) + ", Y " + juce::String(get("xyY"), 1));
                    setP("xyX", 0.0f); setP("xyY", 0.0f);
                } else check("the XY pad is on the panel", false);

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
                    const Case cases[] = { { "fltFreq", "2.5k", 2500.0f }, { "fltFreq", "800 Hz", 800.0f },
                                           { "d0a", "12", 3.98107f }, { "fbNote", "C#4", 61.0f },
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

            // the drawn filter: a resonant notch, processed once so the engine
            // publishes it, painted to its own PNG to be looked at
            {
                for (auto [id, v] : { std::pair<const char*, float>{ "fltType", 4.0f }, { "fltFreq", 1400.0f },
                                      { "fltQ", 2.0f }, { "fltCirc", 1.0f }, { "fltPoles", 2.0f } })
                    if (auto* prm = proc.apvts.getParameter(id)) prm->setValueNotifyingHost(prm->convertTo0to1(v));
                juce::AudioBuffer<float> buf(2, 512); buf.clear(); juce::MidiBuffer midi;
                proc.processBlock(buf, midi);
                const auto st = proc.filterState();
                check("the editor is told the filter's live state", st.type == 4 && std::abs(st.freq - 1400.0) < 1.0,
                      juce::String(st.type) + " at " + juce::String(st.freq));
                juce::Image fshot(juce::Image::ARGB, w, h, true);
                { juce::Graphics g(fshot); editor->paintEntireComponent(g, true); }
                juce::File ffile(juce::File::getCurrentWorkingDirectory().getChildFile(shotPath)
                                     .withFileExtension("").getFullPathName() + "-filter.png");
                ffile.deleteFile();
                if (auto stream = std::unique_ptr<juce::FileOutputStream>(ffile.createOutputStream())){
                    juce::PNGImageFormat png; png.writeImageToStream(fshot, *stream);
                }
            }

            // the Tips switch and the built-in guide
            if (auto* fe = dynamic_cast<FractureEditor*>(editor.get())){
                const bool was = fe->tipsOn();
                fe->setTips(false);
                const bool off = !fe->tipsOn();
                fe->setTips(true);
                check("the Tips switch turns tooltips off and on", off && fe->tipsOn());
                fe->setTips(was);

                fe->setGuideOpen(true);
                const int gh = fe->guideHeight();
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
                fe->setGuideOpen(false);
                check("and closes again", fe->guideHeight() == 0);

                // the header: your presets in the menu, and the undo and A/B strip
                std::function<juce::Component*(juce::Component*, const std::type_info&)> findT =
                    [&](juce::Component* c, const std::type_info& t) -> juce::Component* {
                        if (typeid(*c) == t) return c;
                        for (auto* ch : c->getChildren()) if (auto* r = findT(ch, t)) return r;
                        return nullptr;
                    };
                // saved only now, so the screenshots above show a factory preset's name
                proc.saveUserPreset(presetDir.getChildFile("My bass.json"));
                if (auto* box = dynamic_cast<session::PresetBox*>(findT(editor.get(), typeid(session::PresetBox)))){
                    box->beforePopup();
                    bool listed = false;
                    for (int i = 0; i < box->getNumItems(); ++i) listed |= box->getItemText(i) == "My bass";
                    check("the preset menu lists your presets under the factory ones", listed
                          && box->getNumItems() > static_cast<int>(fracture::factoryPresets().size()));
                    box->setSelectedItemIndex(3, juce::sendNotificationSync);
                    check("  ... and choosing a factory one from it still loads it", proc.getCurrentProgram() == 3);

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
                        proc.setCurrentProgram(3);
                        nextB->triggerClick(); juce::MessageManager::getInstance()->runDispatchLoopUntil(30);
                        const bool fwd = proc.getCurrentProgram() == 4;
                        prevB->triggerClick(); prevB->triggerClick(); juce::MessageManager::getInstance()->runDispatchLoopUntil(30);
                        const bool back = proc.getCurrentProgram() == 2;
                        check("  ... next and previous step one preset each way", fwd && back,
                              "now " + juce::String(proc.getCurrentProgram()));
                        proc.setCurrentProgram(n - 1);
                        nextB->triggerClick(); juce::MessageManager::getInstance()->runDispatchLoopUntil(30);
                        check("  ... past the last factory preset comes one of yours", proc.userPresetName() == "My bass",
                              proc.userPresetName());
                        nextB->triggerClick(); juce::MessageManager::getInstance()->runDispatchLoopUntil(30);
                        check("  ... and past the last of yours it goes round to the first",
                              proc.getCurrentProgram() == 0 && proc.userPresetName().isEmpty());
                        prevB->triggerClick(); juce::MessageManager::getInstance()->runDispatchLoopUntil(30);
                        check("  ... and back again from the first", proc.userPresetName() == "My bass");
                        proc.setCurrentProgram(0);
                    }
                } else check("the editor has a preset menu", false);
                auto* bar = fe->sessionBar.get();
                auto* prm = proc.apvts.getParameter("d0a");
                const float was0 = prm->getValue();
                prm->beginChangeGesture(); prm->setValueNotifyingHost(0.9f); prm->endChangeGesture();
                bar->refresh();
                const bool undoLit = bar->undo.isEnabled();
                bar->undo.triggerClick();
                juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
                check("the Undo button takes back a knob move", undoLit && std::abs(prm->getValue() - was0) < 1.0e-4f);
                bar->redo.triggerClick();
                juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
                check("the Redo button puts it back", std::abs(prm->getValue() - 0.9f) < 1.0e-3f);
                check("Ctrl or Cmd + Z is undo", fe->keyPressed(juce::KeyPress('z', juce::ModifierKeys::commandModifier, 0))
                      && std::abs(prm->getValue() - was0) < 1.0e-4f);
                bar->slotB.triggerClick();
                juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
                bar->refresh();
                check("the B button switches to B and lights", proc.session->activeSlot() == 1
                      && bar->slotB.getToggleState() && !bar->slotA.getToggleState()
                      && bar->copy.getButtonText() == "B to A");
                bar->slotA.triggerClick();
                juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
                check("  ... and A switches back", proc.session->activeSlot() == 0);

                // the harmonic table: drawn with the mouse, one stroke one undo
                fe->setTableOpen(true);
                auto* te = fe->tableEditor();
                check("the Harmonic table opens over the panels", te != nullptr && te->isVisible()
                      && te->getWidth() > 1000);
                if (te != nullptr){
                    auto* strip = te->strip(1);                      // frame 2
                    auto src = juce::Desktop::getInstance().getMainMouseSource();
                    auto mev = [&](juce::Point<float> down, juce::Point<float> at, int clicks = 1){
                        return juce::MouseEvent(src, at, {}, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, strip, strip,
                                                juce::Time::getCurrentTime(), down, juce::Time::getCurrentTime(), clicks, at != down);
                    };
                    const auto L = strip->lane().toFloat();
                    const float bw = L.getWidth() / fracture::tableHarmonics;
                    auto barX = [&](int k){ return L.getX() + (k + 0.5f) * bw; };
                    auto val = [&](int k){ return proc.apvts.getRawParameterValue("tb2h" + juce::String(k + 1))->load(); };
                    const float before2 = val(1), before7 = val(6);
                    const size_t depth = proc.session->undoDepth();
                    // a stroke from bar 2 near the top to bar 7 near the bottom
                    const juce::Point<float> a { barX(1), L.getY() + 2.0f }, b { barX(6), L.getBottom() - 2.0f };
                    strip->mouseDown(mev(a, a));
                    for (int i = 1; i <= 10; ++i){
                        const auto q = a + (b - a) * (i / 10.0f);
                        strip->mouseDrag(mev(a, q));
                    }
                    strip->mouseUp(mev(a, b));
                    // every bar the stroke crosses takes the height the line has
                    // there (the last point in a bar wins, as in Serum), so they
                    // fall steadily from top to bottom
                    bool falling = true;
                    for (int k = 2; k <= 6; ++k) falling = falling && val(k) < val(k - 1) - 10.0f;
                    check("drawing across frame 2 sets every bar the stroke crosses", falling && val(1) > 60.0f && val(6) < -90.0f,
                          "bars 2 to 7: " + juce::String(val(1), 0) + " " + juce::String(val(2), 0) + " " + juce::String(val(3), 0)
                              + " " + juce::String(val(4), 0) + " " + juce::String(val(5), 0) + " " + juce::String(val(6), 0));
                    check("  ... and the whole stroke is one undo step", proc.session->undoDepth() == depth + 1,
                          juce::String(static_cast<int>(proc.session->undoDepth() - depth)) + " steps");
                    proc.session->undo();
                    check("  ... which undo takes back", std::abs(val(1) - before2) < 1e-3f && std::abs(val(6) - before7) < 1e-3f);
                    strip->mouseDoubleClick(mev({ barX(2), L.getY() + 5.0f }, { barX(2), L.getY() + 5.0f }, 2));
                    check("a double-click zeroes a bar", std::abs(val(2)) < 1e-3f);
                    proc.session->undo();
                    // Start from: frame 3 filled with Tube, the rest left alone, one undo step
                    {
                        auto bar = [&](int f, int k){ return proc.apvts.getRawParameterValue("tb" + juce::String(f + 1) + "h" + juce::String(k + 1))->load(); };
                        float was[fracture::tableFrames][fracture::tableHarmonics];
                        for (int f = 0; f < fracture::tableFrames; ++f)
                            for (int k = 0; k < fracture::tableHarmonics; ++k) was[f][k] = bar(f, k);
                        const size_t d0 = proc.session->undoDepth();
                        te->startFrom(2, static_cast<int>(fracture::Mode::Tube));
                        double want[fracture::tableHarmonics];
                        fracture::barsFromShaper(static_cast<int>(fracture::Mode::Tube), fracture::startFromDrive, want);
                        float worst = 0.0f; bool othersKept = true;
                        for (int k = 0; k < fracture::tableHarmonics; ++k){
                            worst = std::max(worst, std::abs(bar(2, k) - static_cast<float>(want[k] * 100.0)));
                            for (int f : { 0, 1, 3 }) othersKept = othersKept && bar(f, k) == was[f][k];
                        }
                        check("Start from Tube fills frame 3 with Tube's harmonics, and only frame 3",
                              worst < 0.05f && othersKept && std::abs(bar(2, 1)) > 3.0f,
                              "worst bar off by " + juce::String(worst, 3) + ", 2nd harmonic " + juce::String(bar(2, 1), 1) + "%");
                        check("  ... as one undo step, which undo takes back",
                              proc.session->undoDepth() == d0 + 1 && (proc.session->undo(), bar(2, 1) == was[2][1] && bar(2, 0) == was[2][0]),
                              juce::String(static_cast<int>(proc.session->undoDepth() - d0)) + " steps");
                        te->startFrom(-1, static_cast<int>(fracture::Mode::Soft));
                        // gentle to hard: the 3rd harmonic grows frame by frame
                        const bool growing = std::abs(bar(0, 2)) < std::abs(bar(1, 2)) && std::abs(bar(1, 2)) < std::abs(bar(2, 2))
                                             && std::abs(bar(2, 2)) < std::abs(bar(3, 2));
                        check("  ... and all four frames from Soft go from gentle to hard",
                              growing && te->startButton.isVisible() && !te->startButton.getBounds().isEmpty(),
                              "3rd harmonic " + juce::String(bar(0, 2), 0) + " " + juce::String(bar(1, 2), 0) + " "
                                  + juce::String(bar(2, 2), 0) + " " + juce::String(bar(3, 2), 0) + "%");
                        proc.session->undo();
                    }
                    // routing an LFO to Position from the editor
                    for (int k = 0; k < 6; ++k)
                        if (auto* q = proc.apvts.getParameter("mS" + juce::String(k))) q->setValueNotifyingHost(0.0f);
                    te->wobble.triggerClick();
                    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
                    const auto& dests = fracture::Params::get().dests();
                    const int dst = 1 + static_cast<int>(std::find(dests.begin(), dests.end(), fracture::Ids::get().tblPos) - dests.begin());
                    check("Wobble with LFO 1 routes LFO 1 to Table position in a free slot",
                          static_cast<int>(proc.apvts.getRawParameterValue("mS0")->load()) == 1
                              && static_cast<int>(proc.apvts.getRawParameterValue("mD0")->load()) == dst
                              && std::abs(proc.apvts.getRawParameterValue("mA0")->load() - 50.0f) < 0.5f);
                    // a picture of it, with a band on Table and the LFO moving
                    if (auto* m = proc.apvts.getParameter("m0a")) m->setValueNotifyingHost(m->convertTo0to1(static_cast<float>(fracture::Mode::Table)));
                    if (auto* q = proc.apvts.getParameter("tblPos")) q->setValueNotifyingHost(q->convertTo0to1(45.0f));
                    // the LFO the button routed would pull the picture wherever its
                    // phase happens to be; with it at zero the shot is a steady 45%
                    if (auto* q = proc.apvts.getParameter("mA0")) q->setValueNotifyingHost(q->convertTo0to1(0.0f));
                    runBlocks(proc, 40, 256, peak, bad);
                    juce::Image tshot(juce::Image::ARGB, w, h, true);
                    { juce::Graphics g(tshot); editor->paintEntireComponent(g, true); }
                    juce::File tfile(juce::File::getCurrentWorkingDirectory().getChildFile(shotPath)
                                         .withFileExtension("").getFullPathName() + "-table.png");
                    tfile.deleteFile();
                    if (auto stream = std::unique_ptr<juce::FileOutputStream>(tfile.createOutputStream())){
                        juce::PNGImageFormat png; png.writeImageToStream(tshot, *stream);
                    }
                    check("the Table runs clean in the plugin", bad == 0 && peak <= 1.0, "peak " + juce::String(peak, 3));
                }
                fe->setTableOpen(false);
                check("  ... and closes again", fe->tableEditor() == nullptr);
            } else check("the editor is a FractureEditor", false);
        }
    }

    presetDir.deleteRecursively();
    proc.releaseResources();
    std::printf("\n%d assertions passed, %d failed\n", passed, failures.size());
    for (const auto& f : failures) std::printf("  - %s\n", f.toRawUTF8());
    return failures.isEmpty() ? 0 : 1;
}
