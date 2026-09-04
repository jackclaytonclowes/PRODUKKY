// host_smoke.cpp — exercises the plugin the way a host does: prepare, run
// blocks, walk the presets, round-trip the state, import a browser patch, and
// render the editor into a PNG. The picture matters as much as the assertions:
// the DSP tests cannot tell you the interface came out looking right.
//
//   cmake --build build --target host_smoke && ./build/host_smoke shot.png
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Presets.h"
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
    check("nine factory presets", proc.getNumPrograms() == 9,
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
            check("editor comes up at its own design size",
                  w >= 900 && h >= 700, juce::String(w) + "x" + juce::String(h));
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
            juce::File file(juce::File::getCurrentWorkingDirectory().getChildFile(shotPath));
            file.deleteFile();                       // createOutputStream appends
            if (auto stream = std::unique_ptr<juce::FileOutputStream>(file.createOutputStream())){
                juce::PNGImageFormat png;
                png.writeImageToStream(shot, *stream);
                std::printf("  screenshot %s\n", file.getFullPathName().toRawUTF8());
            }
        }
    }

    proc.releaseResources();
    std::printf("\n%d assertions passed, %d failed\n", passed, failures.size());
    for (const auto& f : failures) std::printf("  - %s\n", f.toRawUTF8());
    return failures.isEmpty() ? 0 : 1;
}
