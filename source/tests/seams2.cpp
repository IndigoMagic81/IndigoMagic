// Seam check on final RGB tiles: compares colour change across the wrap seam with the average
// change between neighbouring interior pixels, for every tiled pattern at several tile sizes.
#include "../src/core.h"
#include <cstdio>
#include <cmath>
static float dif(uint32_t a, uint32_t b) { return (float)(abs((int)((a >> 16) & 255) - (int)((b >> 16) & 255)) + abs((int)((a >> 8) & 255) - (int)((b >> 8) & 255)) + abs((int)(a & 255) - (int)(b & 255))); }
int main() {
    EngineInit();
    const auto& P = Patterns();
    int flagged = 0, total = 0;
    for (size_t i = 0; i < P.size(); i++) {
        if (P[i].kind != K_DTILE) continue;
        int sizes[4][2] = {{0, 1}, {128, 1}, {384, 1}, {0, 3}};   // tile size, Scale
        for (auto& sz : sizes) {
            int s = sz[0];
            if (s && !P[i].usesTile) continue;
            Config c = DefaultConfig((int)i); c.tileSize = s; c.zoom = sz[1];
            auto t = RenderTile(c);
            int W = t->w, H = t->h;
            // mean colour change across every column boundary and every row boundary; a seam is
            // only suspicious if it stands out from all of the interior boundaries
            std::vector<double> col(W, 0), row(H, 0);
            for (int x = 0; x < W; x++) { for (int y = 0; y < H; y++) col[x] += dif(t->px[y * W + x], t->px[y * W + (x + 1) % W]); col[x] /= H; }
            for (int y = 0; y < H; y++) { for (int x = 0; x < W; x++) row[y] += dif(t->px[y * W + x], t->px[((y + 1) % H) * W + x]); row[y] /= W; }
            double mx = 0, my = 0, in = 0;
            for (int x = 0; x + 1 < W; x++) { mx = std::max(mx, col[x]); in += col[x]; }
            for (int y = 0; y + 1 < H; y++) my = std::max(my, row[y]);
            in /= std::max(1, W - 1);
            double sx = col[W - 1], sy = row[H - 1];
            double r = std::max(sx / (mx * 1.15 + 1.0), sy / (my * 1.15 + 1.0));
            total++;
            if (r > 1.0) { flagged++; printf("%-22ls tile %3d: interior %6.2f max %6.2f/%6.2f seamX %6.2f seamY %6.2f ratio %.2f\n", P[i].name.c_str(), W, in, mx, my, sx, sy, r); }
        }
    }
    printf("checked %d tiles, flagged %d\n", total, flagged);
    return 0;
}
