// Test du moteur de rendu portable : g++ -std=c++17 -O2 tests/render_test.cpp -o render_test
// Usage : ./render_test samples/test.glb [sortie.ppm] [taille]
#include <cstdio>
#include <fstream>
#include <iterator>
#include "../src/glb_render.h"

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s fichier.glb [out.ppm] [taille]\n", argv[0]); return 2; }
    std::ifstream f(argv[1], std::ios::binary);
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), {});
    int size = argc > 3 ? atoi(argv[3]) : 256;
    std::vector<uint32_t> px;
    auto dec = [](const uint8_t*, size_t, int& w, int& h, std::vector<uint32_t>& p) {  // texture factice (damier)
        w = h = 8; p.resize(64);
        for (int i = 0; i < 64; i++) p[i] = ((i / 8 + i % 8) & 1) ? 0xFFFFFFFF : 0xFF303030;
        return true;
    };
    if (!glbr::RenderGlbThumbnail(d.data(), d.size(), size, dec, px)) { fprintf(stderr, "ECHEC du rendu\n"); return 1; }
    size_t covered = 0;
    for (auto c : px) covered += (c >> 24) != 0;
    printf("OK : %zu pixels couverts sur %zu\n", covered, px.size());
    if (covered < px.size() / 50) { fprintf(stderr, "Image quasi vide\n"); return 1; }
    if (argc > 2) {
        FILE* o = fopen(argv[2], "wb");
        fprintf(o, "P6\n%d %d\n255\n", size, size);
        for (int i = 0; i < size * size; i++) {
            uint32_t c = px[i]; int a = c >> 24, bg = ((i % size / 16 + i / size / 16) & 1) ? 200 : 160;
            for (int s : {16, 8, 0}) { unsigned v = ((c >> s) & 255) + bg * (255 - a) / 255; fputc(v > 255 ? 255 : v, o); }
        }
        fclose(o);
    }
    return 0;
}
