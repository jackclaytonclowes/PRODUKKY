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

    std::printf("\nPresets\n");
    check("twenty-one starting points", proc.getNumPrograms() == 21, juce::String(proc.getNumPrograms()));
    for (int i = 0; i < proc.getNumPrograms(); ++i){
        proc.setCurrentProgram(i);
        const bool ok = runBlocks(proc, ph, 12, 256, peak, bad);
        check("preset \"" + proc.getProgramName(i) + "\"", ok && peak <= 1.0,
              "bad " + juce::String(bad) + " peak " + juce::String(peak, 3));
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

    std::printf("\nEditor\n");
    {
        proc.setCurrentProgram(4);                       // something with feel dialled in
        std::unique_ptr<juce::AudioProcessorEditor> editor(proc.createEditor());
        check("editor is created", editor != nullptr);
        if (editor != nullptr){
            const int w = editor->getWidth(), h = editor->getHeight();
            std::printf("        editor size %d x %d\n", w, h);
            check("editor comes up big enough to work in", w >= 600 && h >= 340,
                  juce::String(w) + "x" + juce::String(h));
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
                check("shrunk editor keeps the design proportions",
                      std::abs(sw / (double) sh - (1080 / (double) 620)) < 0.02,
                      juce::String(sw / (double) sh, 3) + " vs " + juce::String((1080 / (double) 620), 3));
                editor->setSize(w, h);
            }

            juce::File file(juce::File::getCurrentWorkingDirectory().getChildFile(shotPath));
            file.deleteFile();                            // createOutputStream appends
            if (auto stream = std::unique_ptr<juce::FileOutputStream>(file.createOutputStream())){
                juce::PNGImageFormat png;
                png.writeImageToStream(shot, *stream);
                std::printf("  screenshot %s\n", file.getFullPathName().toRawUTF8());
            }
        }
    }

    proc.setPlayHead(nullptr);
    proc.releaseResources();
    std::printf("\n%d assertions passed, %d failed\n", passed, failures.size());
    for (const auto& f : failures) std::printf("  - %s\n", f.toRawUTF8());
    return failures.isEmpty() ? 0 : 1;
}
