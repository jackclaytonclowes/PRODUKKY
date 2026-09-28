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
    proc.saveUserPreset(presetDir.getChildFile("My bass.json"));
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
                if (auto* box = dynamic_cast<session::PresetBox*>(findT(editor.get(), typeid(session::PresetBox)))){
                    box->beforePopup();
                    bool listed = false;
                    for (int i = 0; i < box->getNumItems(); ++i) listed |= box->getItemText(i) == "My bass";
                    check("the preset menu lists your presets under the factory ones", listed
                          && box->getNumItems() > static_cast<int>(fracture::factoryPresets().size()));
                    box->setSelectedItemIndex(3, juce::sendNotificationSync);
                    check("  ... and choosing a factory one from it still loads it", proc.getCurrentProgram() == 3);
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
            } else check("the editor is a FractureEditor", false);
        }
    }

    presetDir.deleteRecursively();
    proc.releaseResources();
    std::printf("\n%d assertions passed, %d failed\n", passed, failures.size());
    for (const auto& f : failures) std::printf("  - %s\n", f.toRawUTF8());
    return failures.isEmpty() ? 0 : 1;
}
