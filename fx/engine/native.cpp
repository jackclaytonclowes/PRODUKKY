// native.cpp — the same wrapper as the browser's, compiled for this machine,
// for fx/engine/parity.mjs to compare the WebAssembly build against. Reads
// "index value" pairs on stdin, renders the parity signal, writes floats.
#include "fracture-wasm.cpp"
#include <cstdio>
int main(int argc, char** argv){
    const int blocks = argc > 1 ? std::atoi(argv[1]) : 200;
    fx_init(48000.0);
    int i; float v;
    while (std::scanf("%d %f", &i, &v) == 2) fx_set(i, v);
    fx_transport(120.0, 0.0, 0);
    unsigned s = 7;
    for (int b = 0; b < blocks; ++b){
        for (int k = 0; k < 128; ++k){
            s = s * 1103515245u + 12345u;
            const float x = (static_cast<float>(s >> 9) / 4194304.0f - 1.0f) * 0.4f;
            buffer[0][k] = x; buffer[1][k] = -0.5f * x;
        }
        fx_process(128);
        std::fwrite(buffer[0], 4, 128, stdout); std::fwrite(buffer[1], 4, 128, stdout);
    }
}
