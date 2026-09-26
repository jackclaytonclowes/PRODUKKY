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

    std::printf("\nEditor\n");
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
            } else check("the editor is a FractureEditor", false);
        }
    }

    proc.releaseResources();
    std::printf("\n%d assertions passed, %d failed\n", passed, failures.size());
    for (const auto& f : failures) std::printf("  - %s\n", f.toRawUTF8());
    return failures.isEmpty() ? 0 : 1;
}
