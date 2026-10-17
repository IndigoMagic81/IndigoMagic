// The generators added in 2.4 for the families Geometric, Wireframe, Fractals, Retro Raster,
// Decorative, Topographic, Abstract and Procedural Textures. Every tile is seamless (integer
// frequencies and lattices on the unit torus); scenes are composed per display. Shapes are
// described analytically or drawn with the antialiased helpers, so the engine's supersampling
// gives clean edges at any Scale.
#include "genutil.h"

namespace {
// one output pixel, in tile (u) units
inline float onePx(const GenCtx& c) { return 1.0f / std::max(1, c.w); }
inline float frac(float x) { return x - std::floor(x); }
inline float rot2x(float x, float y, float a) { return x * std::cos(a) - y * std::sin(a); }
inline float rot2y(float x, float y, float a) { return x * std::sin(a) + y * std::cos(a); }
// a smooth tile-periodic source image for the raster patterns, 0 (light) .. 1 (dark)
float source(int kind, float u, float v, uint32_t seed) {
    switch (kind) {
    case 1: return sat(0.5f - 0.5f * std::cos(TAU * v) + 0.12f * fbm(u, v, 2, 3, 0.5f, seed));                        // bands
    case 2: return 0.5f + 0.5f * std::cos(TAU * u) * std::cos(TAU * v);                                             // rings
    case 3: return sat(0.5f + (std::sin(TAU * 2 * u) + std::sin(TAU * 3 * v) + std::sin(TAU * (2 * u + 2 * v)) + std::cos(TAU * (u - 2 * v))) / 6.0f);   // plasma
    default: return sat(0.5f + 0.8f * fbm(u, v, 3, 4, 0.5f, seed));                                                 // clouds
    }
}
float contrastT(float t, float con) { return sat((t - 0.5f) * con + 0.5f); }
// pointy-top hexagonal lattice that repeats exactly on the unit tile: nCols hexagons across.
// Returns the centre (in lattice units) and the local offset; metric units are hex widths.
struct Hex { float dx, dy, cx, cy; int col, row; float R, ap; };
Hex hexAt(float u, float v, int nCols) {
    int rows = std::max(2, 2 * (int)std::lround(nCols / 0.8660254f / 2));   // even: the half-shift repeats
    float rs = (float)nCols / rows;                                        // row spacing in hex widths (~0.866)
    float X = u * nCols, Y = v * rows * rs;
    Hex best{0, 0, 0, 0, 0, 0, rs / 1.5f, rs / 1.5f * 0.8660254f};
    float bd = 1e9f;
    int j0 = (int)std::floor(v * rows);
    for (int j = j0 - 1; j <= j0 + 1; j++) {
        float off = (pmod(j, 2)) ? 0.5f : 0.0f;
        int i0 = (int)std::floor(X - off);
        for (int i = i0 - 1; i <= i0 + 1; i++) {
            float cx = i + off + 0.5f, cy = (j + 0.5f) * rs;
            float dx = X - cx, dy = Y - cy, d = dx * dx + dy * dy;
            if (d < bd) { bd = d; best.dx = dx; best.dy = dy; best.cx = cx; best.cy = cy; best.col = pmod(i, nCols); best.row = pmod(j, rows); }
        }
    }
    return best;
}
// distance from the hexagon's edge (positive inside), pointy-top, apothem ap
float hexEdge(float dx, float dy, float ap) {
    float m = 0;
    for (int k = 0; k < 6; k++) { float a = k * PI_F / 3; m = std::max(m, dx * std::cos(a) + dy * std::sin(a)); }
    return ap - m;
}
}   // namespace

// =============================================================================================
// GEOMETRIC
// =============================================================================================
void gInterlockSquares(GenOut& o, const GenCtx& c) {
    // two lattices of square frames, half a cell apart, woven over and under at every crossing
    Field F(o, c); int n = (int)c.p[0]; float size = c.p[1], th = c.p[2], edge = c.p[3];
    F.each([&](float u, float v, int, int) {
        float x = u * n, y = v * n;
        float ax = frac(x) - 0.5f, ay = frac(y) - 0.5f;                   // frame A centred in the cell
        float bx = x - std::round(x), by = y - std::round(y);             // frame B centred on the corners
        float dA = std::fabs(std::max(std::fabs(ax), std::fabs(ay)) - size * 0.5f);
        float dB = std::fabs(std::max(std::fabs(bx), std::fabs(by)) - size * 0.5f);
        bool inA = dA < th * 0.5f, inB = dB < th * 0.5f;
        if (!inA && !inB) return lvl(0);
        int ia = (int)std::floor(x), ja = (int)std::floor(y);
        bool aOver = ((ia + ja + (ax > 0) + (ay > 0)) & 1) == 0;
        bool topA = inA && (!inB || aOver);
        float d = topA ? dA : dB;
        if (d > th * 0.5f - edge * th) return lvl(3);                     // the outline of the strand on top
        return topA ? lvl(1) : lvl(2);
    });
    o.normalize = false;
}
void gNestedDiamonds(GenOut& o, const GenCtx& c) {
    Field F(o, c); int n = (int)c.p[0], rings = (int)c.p[1]; float gap = c.p[2]; int offset = (int)c.p[3];
    F.each([&](float u, float v, int, int) {
        float y = v * n;
        float x = u * n + (offset && (pmod((int)std::floor(y), 2)) ? 0.5f : 0.0f);
        float lx = frac(x) - 0.5f, ly = frac(y) - 0.5f;
        float d = (std::fabs(lx) + std::fabs(ly)) * 2.0f;                 // 0 at the centre, 1 at the edge midpoints
        float r = d * rings;
        int band = (int)std::floor(r);
        float f = r - band;
        if (f < gap) return lvl(0);
        return lvl(1 + pmod(band, 3));
    });
    o.normalize = false;
}
void gBeveledTiles(GenOut& o, const GenCtx& c) {
    // square tiles with a lit bevel (light from the upper left), set in grout
    Field F(o, c); int n = (int)c.p[0]; float bev = c.p[1], grout = c.p[2]; int layout = (int)c.p[3]; float var = c.p[4];
    F.each([&](float u, float v, int, int) {
        float x, y;
        if (layout == 2) { x = (u + v) * n; y = (v - u) * n; }          // diamonds
        else { x = u * n; y = v * n; if (layout == 1 && (pmod((int)std::floor(y), 2))) x += 0.5f; }   // grid / running bond
        int ix = (int)std::floor(x), iy = (int)std::floor(y);
        float fx = x - ix, fy = y - iy;
        float l = fx, t = fy, r = 1 - fx, b = 1 - fy;
        float e = std::min(std::min(l, r), std::min(t, b));
        float tileV = 0.55f + var * 0.25f * (hf(hash3(pmod(ix, layout == 2 ? 2 * n : n), pmod(iy, layout == 2 ? 2 * n : n), c.seed)) - 0.5f);
        if (e < grout) return 0.02f;
        float be = e - grout;
        if (be < bev) {
            float lit = (e == l || e == t) ? 1.0f : 0.0f;                 // top / left faces catch the light
            float k = be / bev;                                          // 0 at the grout, 1 at the face
            return lit > 0.5f ? lerpf(0.98f, tileV, k * k) : lerpf(0.22f, tileV, k * k);
        }
        return tileV;
    });
    o.normalize = false;
}
void gIsoCubes2(GenOut& o, const GenCtx& c) {
    // tumbling blocks: every hexagon of a hex lattice split into the three faces of a cube
    Field F(o, c); int n = (int)c.p[0]; float line = c.p[1]; int style = (int)c.p[2];
    F.each([&](float u, float v, int, int) {
        Hex h = hexAt(u, v, n);
        float R = h.R;
        float a = std::atan2(-h.dy, h.dx) * 180.0f / PI_F;             // y up
        int face = (a > 30 && a < 150) ? 1 : ((a >= 150 || a < -90) ? 2 : 3);
        if (style == 1) {                                                // stairs: flip every other row
            if (pmod(h.row, 2)) face = face == 1 ? 1 : (face == 2 ? 3 : 2);
        }
        if (line > 0) {
            float d = hexEdge(h.dx, h.dy, h.ap);
            for (int k = 0; k < 3; k++) {
                float ang = (30 + 120 * k) * PI_F / 180;
                d = std::min(d, segDist(h.dx, h.dy, 0, 0, R * std::cos(ang), -R * std::sin(ang)));
            }
            if (d < line * 0.5f) return lvl(0);
        }
        return lvl(face);
    });
    o.normalize = false;
}
void gCairo(GenOut& o, const GenCtx& c) {
    // Cairo pentagonal tiling: each square of a checkerboard gets a short segment (horizontal on
    // one colour, vertical on the other) joined to the square's corners
    Field F(o, c); int n = (int)c.p[0]; float line = c.p[1], pinch = c.p[2];
    const float faceV[4] = {1.0f / 3, 2.0f / 3, 1.0f, 0.5f};
    F.each([&](float u, float v, int, int) {
        float x = u * n, y = v * n;
        int ci = (int)std::floor(x), cj = (int)std::floor(y);
        float best = 1e9f;
        for (int j = cj - 1; j <= cj + 1; j++)
            for (int i = ci - 1; i <= ci + 1; i++) {
                bool hor = ((i + j) & 1) == 0;
                float lx = x - (i + 0.5f), ly = y - (j + 0.5f);
                if (!hor) std::swap(lx, ly);
                float d = segDist(lx, ly, -pinch, 0, pinch, 0);
                d = std::min(d, segDist(lx, ly, -pinch, 0, -0.5f, -0.5f));
                d = std::min(d, segDist(lx, ly, -pinch, 0, -0.5f, 0.5f));
                d = std::min(d, segDist(lx, ly, pinch, 0, 0.5f, -0.5f));
                d = std::min(d, segDist(lx, ly, pinch, 0, 0.5f, 0.5f));
                best = std::min(best, d);
            }
        if (best < line * 0.5f) return 0.0f;
        // which region of its own square the point is in decides the pentagon's orientation
        bool hor = ((ci + cj) & 1) == 0;
        float lx = x - (ci + 0.5f), ly = y - (cj + 0.5f);
        if (!hor) std::swap(lx, ly);
        int region;   // 0 top quad, 1 bottom quad, 2 left triangle, 3 right triangle (in the rotated frame)
        if (lx < -pinch && std::fabs(ly) < (-pinch - lx) / (0.5f - pinch) * 0.5f) region = 2;
        else if (lx > pinch && std::fabs(ly) < (lx - pinch) / (0.5f - pinch) * 0.5f) region = 3;
        else region = ly < 0 ? 0 : 1;
        // horizontal: top quad A, bottom quad B, left tri -> D, right tri -> C
        // vertical (rotated frame): left quad C, right quad D, top tri B, bottom tri A
        static const int H[4] = {0, 1, 3, 2}, V[4] = {2, 3, 1, 0};
        int face = hor ? H[region] : V[region];
        return faceV[face];
    });
    o.normalize = false;
}

// =============================================================================================
// WIREFRAME
// =============================================================================================
void gWireMesh(GenOut& o, const GenCtx& c) {
    // a wire surface in perspective, drawn back to front: each quad is filled (hiding what is
    // behind it) and then outlined, so the mesh reads as solid
    Field F(o, c); F.fill(0.0f);
    int N = (int)c.p[0]; float hgt = c.p[1]; int waves = (int)c.p[2]; float cam = c.p[3]; int shape = (int)c.p[4];
    auto H = [&](float x, float z) {
        switch (shape) {
        case 1: return 0.6f * (x * x - (z - 1) * (z - 1)) * hgt;                                     // saddle
        case 2: return (fbm(fmodp(x * 0.25f + 0.5f, 1.0f), fmodp(z * 0.25f, 1.0f), 3, 4, 0.5f, c.seed) * 1.6f) * hgt;   // hills
        case 3: { float r = std::sqrt(x * x + (z - 1) * (z - 1)); return std::cos(r * waves * 2.2f) * std::exp(-r * 0.8f) * hgt * 1.4f; }   // ripple
        default: return (std::sin(x * waves * 1.6f) * std::cos(z * waves * 1.3f)) * hgt;            // egg crate
        }
    };
    float sc = F.h * 1.05f;
    auto P = [&](int i, int j, float& px, float& py) {
        float x = -1.6f + 3.2f * i / N, z = 2.0f * j / N;
        float y = H(x, z);
        float zz = z + 1.2f, yy = y - cam;
        px = F.w / 2.0f + x / zz * sc * 0.75f;
        py = F.h * 0.42f - yy / zz * sc * 0.55f;
    };
    std::vector<float> X((N + 1) * (N + 1)), Y(X.size());
    for (int j = 0; j <= N; j++) for (int i = 0; i <= N; i++) P(i, j, X[j * (N + 1) + i], Y[j * (N + 1) + i]);
    float lw = std::max(c.px, F.h / 700.0f);
    for (int j = N - 1; j >= 0; j--)
        for (int i = 0; i < N; i++) {
            int a = j * (N + 1) + i, b = a + 1, d = a + N + 1, e = d + 1;
            F.tri(X[a], Y[a], X[b], Y[b], X[e], Y[e], 0.0f);
            F.tri(X[a], Y[a], X[e], Y[e], X[d], Y[d], 0.0f);
            float br = 0.35f + 0.65f * (1.0f - (float)j / N);
            F.line(X[a], Y[a], X[b], Y[b], lw, br); F.line(X[a], Y[a], X[d], Y[d], lw, br);
            F.line(X[b], Y[b], X[e], Y[e], lw, br); F.line(X[d], Y[d], X[e], Y[e], lw, br);
        }
    o.normalize = false;
}
void gPerspectiveGrid(GenOut& o, const GenCtx& c) {
    // a ray-cast grid corridor: each surface's grid lines are antialiased by their on-screen width
    Field F(o, c); int cells = (int)c.p[0]; float lw = c.p[1], fog = c.p[2]; int view = (int)c.p[3];
    float W = 1.0f, Hh = 0.62f;
    auto hit = [&](float sx, float sy, float& s, float& t, float& dist) -> bool {
        // camera at origin looking down +z; screen plane z = 1
        float best = 1e9f; bool ok = false;
        auto plane = [&](float k, float d, bool vert, bool enabled) {   // surface x = k (vert) or y = k
            if (!enabled || std::fabs(d) < 1e-6f) return;
            float tt = k / d;
            if (tt <= 0 || tt >= best) return;
            best = tt; ok = true;
            s = vert ? sy * tt : sx * tt; t = tt;
        };
        bool walls = view == 0, ceil = view != 2;
        plane(-Hh, sy, false, true); plane(Hh, sy, false, ceil);
        plane(-W, sx, true, walls); plane(W, sx, true, walls);
        dist = best;
        return ok;
    };
    F.each([&](float u, float v, int, int) {
        FULLXY
        float sx = X * 1.2f, sy = -Y * 1.2f;
        float s, t, d;
        if (!hit(sx, sy, s, t, d)) return 0.0f;
        // derivative of the surface coordinates over one output pixel
        float e = 1.2f / F.h, s2, t2, d2, s3, t3, d3;
        hit(sx + e, sy, s2, t2, d2); hit(sx, sy + e, s3, t3, d3);
        float ds = std::max(std::fabs(s2 - s) + std::fabs(s3 - s), 1e-5f), dt = std::max(std::fabs(t2 - t) + std::fabs(t3 - t), 1e-5f);
        float gs = s * cells / 1.0f, gt = t * cells / 1.0f;
        float fs = std::fabs(frac(gs + 0.5f) - 0.5f) / cells, ft = std::fabs(frac(gt + 0.5f) - 0.5f) / cells;
        float cs = sat(0.5f - (fs - lw * 0.5f / cells) / ds), ct = sat(0.5f - (ft - lw * 0.5f / cells) / dt);
        // lines thinner than a pixel fade instead of shimmering
        cs *= std::min(1.0f, lw / cells / ds); ct *= std::min(1.0f, lw / cells / dt);
        float line = std::max(cs, ct);
        float haze = std::exp(-t * fog * 0.18f);
        return 0.12f * haze + line * (0.25f + 0.75f * haze);
    });
    o.normalize = false;
}
void gMoireLines(GenOut& o, const GenCtx& c) {
    // two (or three) crisp line screens at a small angle: interference fringes
    Field F(o, c); float lines = c.p[0], ang = c.p[1] * PI_F / 180, wd = c.p[2]; int style = (int)c.p[3];
    float px1 = 1.0f / F.h;   // one output pixel in screen-height units
    F.each([&](float u, float v, int, int) {
        FULLXY
        auto screen = [&](float a) { float q = (X * std::sin(a) + Y * std::cos(a)) * lines; float d = std::fabs(frac(q + 0.5f) - 0.5f) / lines; return aa(d - wd * 0.5f / lines, px1); };
        auto rings = [&](float ox) { float r = std::sqrt((X - ox) * (X - ox) + Y * Y) * lines; float d = std::fabs(frac(r + 0.5f) - 0.5f) / lines; return aa(d - wd * 0.5f / lines, px1); };
        float s1 = 0, s2 = 0;
        if (style == 0) { s1 = screen(0); s2 = screen(ang); }
        else if (style == 1) { s1 = rings(-0.05f - ang * 0.3f); s2 = rings(0.05f + ang * 0.3f); }
        else { s1 = screen(ang * 0.5f); s2 = rings(0); }
        return (1.0f - s1) * (1.0f - s2);   // paper shows only where neither screen has a line
    });
    o.normalize = false;
}
void gSineGrid(GenOut& o, const GenCtx& c) {
    Field F(o, c); int n = (int)c.p[0]; float amp = c.p[1]; int waves = (int)c.p[2]; float lw = c.p[3] * c.px;
    float cellPx = (float)F.w / n;
    F.each([&](float u, float v, int, int) {
        float qx = u * n + amp * std::sin(TAU * v * waves), qy = v * n + amp * std::sin(TAU * u * waves + 1.0f);
        // slope of each family, to keep the line width constant on screen
        float sx = std::sqrt(1 + std::pow(amp * TAU * waves / n * std::cos(TAU * v * waves), 2.0f));
        float sy = std::sqrt(1 + std::pow(amp * TAU * waves / n * std::cos(TAU * u * waves + 1.0f), 2.0f));
        float dx = std::fabs(frac(qx + 0.5f) - 0.5f) * cellPx / sx, dy = std::fabs(frac(qy + 0.5f) - 0.5f) * cellPx / sy;
        float l = std::max(cov(dx - lw * 0.5f), cov(dy - lw * 0.5f));
        float shade = 0.5f + 0.5f * std::sin(TAU * qx / n * waves) * std::sin(TAU * qy / n * waves);
        return 0.06f + 0.14f * shade + 0.8f * l;
    });
    o.normalize = false;
}
void gWirePolyhedra(GenOut& o, const GenCtx& c) {
    Field F(o, c); F.fill(0); int which = (int)c.p[0]; float size = c.p[1], rx = c.p[2] * PI_F / 180, ry = c.p[3] * PI_F / 180;
    struct V3 { float x, y, z; };
    auto build = [&](int kind, std::vector<V3>& V, std::vector<std::pair<int, int>>& E) {
        V.clear(); E.clear();
        const float g = 1.6180340f;
        if (kind == 0) {   // icosahedron
            float t[12][3] = {{-1, g, 0}, {1, g, 0}, {-1, -g, 0}, {1, -g, 0}, {0, -1, g}, {0, 1, g}, {0, -1, -g}, {0, 1, -g}, {g, 0, -1}, {g, 0, 1}, {-g, 0, -1}, {-g, 0, 1}};
            for (auto& p : t) V.push_back({p[0], p[1], p[2]});
        } else if (kind == 1) {   // dodecahedron
            float a = 1, b = 1 / g;
            for (int i = 0; i < 8; i++) V.push_back({(i & 1) ? a : -a, (i & 2) ? a : -a, (i & 4) ? a : -a});
            for (int s1 = -1; s1 <= 1; s1 += 2) for (int s2 = -1; s2 <= 1; s2 += 2) {
                V.push_back({0, s1 * b, s2 * g}); V.push_back({s1 * b, s2 * g, 0}); V.push_back({s1 * g, 0, s2 * b});
            }
        } else if (kind == 2) {   // octahedron
            float t[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
            for (auto& p : t) V.push_back({p[0] * 1.5f, p[1] * 1.5f, p[2] * 1.5f});
        } else {   // cube
            for (int i = 0; i < 8; i++) V.push_back({(i & 1) ? 1.f : -1.f, (i & 2) ? 1.f : -1.f, (i & 4) ? 1.f : -1.f});
        }
        // edges: every pair at the minimum distance
        float md = 1e9f;
        for (size_t i = 0; i < V.size(); i++) for (size_t j = i + 1; j < V.size(); j++) { float dx = V[i].x - V[j].x, dy = V[i].y - V[j].y, dz = V[i].z - V[j].z; md = std::min(md, dx * dx + dy * dy + dz * dz); }
        for (size_t i = 0; i < V.size(); i++) for (size_t j = i + 1; j < V.size(); j++) { float dx = V[i].x - V[j].x, dy = V[i].y - V[j].y, dz = V[i].z - V[j].z; if (dx * dx + dy * dy + dz * dz < md * 1.01f) E.push_back({(int)i, (int)j}); }
        float mr = 0; for (auto& p : V) mr = std::max(mr, std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z));
        for (auto& p : V) { p.x /= mr; p.y /= mr; p.z /= mr; }
    };
    auto draw = [&](int kind, float cx, float cy, float R, float ax, float ay) {
        std::vector<V3> V; std::vector<std::pair<int, int>> E;
        build(kind, V, E);
        std::vector<float> X(V.size()), Y(V.size()), Z(V.size());
        for (size_t i = 0; i < V.size(); i++) {
            float x = V[i].x, y = V[i].y, z = V[i].z;
            float y1 = y * std::cos(ax) - z * std::sin(ax), z1 = y * std::sin(ax) + z * std::cos(ax);
            float x2 = x * std::cos(ay) + z1 * std::sin(ay), z2 = -x * std::sin(ay) + z1 * std::cos(ay);
            float p = 3.0f / (3.0f + z2);
            X[i] = cx + x2 * R * p; Y[i] = cy - y1 * R * p; Z[i] = z2;
        }
        float lw = std::max(1.2f * c.px, F.h / 450.0f);
        for (auto& e : E) {
            float z = (Z[e.first] + Z[e.second]) * 0.5f;   // -1 near .. 1 far
            F.line(X[e.first], Y[e.first], X[e.second], Y[e.second], lw, 0.3f + 0.7f * sat(0.5f - z * 0.5f));
        }
        for (size_t i = 0; i < V.size(); i++) F.disc(X[i], Y[i], lw * 1.3f, 0.4f + 0.6f * sat(0.5f - Z[i] * 0.5f), true);
    };
    float R = F.h * size;
    if (which < 4) draw(which, F.w / 2.0f, F.h / 2.0f, R, rx, ry);
    else for (int k = 0; k < 4; k++) draw(k, F.w * (0.2f + 0.2f * k), F.h * (k & 1 ? 0.62f : 0.4f), R * 0.42f, rx + k * 0.4f, ry + k * 0.7f);
    o.normalize = false;
}

// =============================================================================================
// FRACTALS
// =============================================================================================
void gOrbitTrap(GenOut& o, const GenCtx& c) {
    // the Mandelbrot set coloured by how close each orbit comes to a "trap" shape
    Field F(o, c); double zoom = std::pow(2.0, c.p[0]), cx = c.p[1], cy = c.p[2]; int trap = (int)c.p[3], it = (int)c.p[4];
    F.each([&](float u, float v, int, int) {
        FULLXY
        double cr = cx + X * 3.0 / zoom, ci = cy + Y * 3.0 / zoom, zr = 0, zi = 0, best = 1e9;
        int i = 0;
        for (; i < it; i++) {
            double nr = zr * zr - zi * zi + cr; zi = 2 * zr * zi + ci; zr = nr;
            double d;
            if (trap == 1) d = std::min(std::fabs(zr), std::fabs(zi));                       // cross
            else if (trap == 2) d = std::fabs(std::sqrt(zr * zr + zi * zi) - 0.5);          // circle
            else d = std::sqrt((zr - 0.25) * (zr - 0.25) + zi * zi);                        // point
            best = std::min(best, d);
            if (zr * zr + zi * zi > 64) break;
        }
        float t = (float)std::exp(-best * 4.0);
        return i >= it ? 0.05f + 0.25f * t : t;
    });
    o.lo = 0.002f; o.hi = 0.998f;
}
void gJuliaGarden(GenOut& o, const GenCtx& c) {
    // a grid of Julia sets, each with the constant taken from its place on a circle of the Mandelbrot set
    Field F(o, c); int g = (int)c.p[0]; float spread = c.p[1]; int it = (int)c.p[2];
    F.each([&](float u, float v, int, int) {
        int cols = std::max(1, (int)std::lround(g * c.aspect));
        float x = u * cols, y = v * g;
        int ix = std::min(cols - 1, (int)x), iy = std::min(g - 1, (int)y);
        float lx = (x - ix - 0.5f) * 3.2f, ly = (y - iy - 0.5f) * 3.2f;
        float k = (ix + iy * cols + 0.5f) / (cols * g);
        double a = k * TAU;
        double jr = 0.7885 * std::cos(a) * (0.85 + 0.15 * spread), ji = 0.7885 * std::sin(a) * (0.85 + 0.15 * spread);
        double zr = lx, zi = ly; int i = 0;
        for (; i < it; i++) { double r2 = zr * zr, i2 = zi * zi; if (r2 + i2 > 64) break; zi = 2 * zr * zi + ji; zr = r2 - i2 + jr; }
        if (i >= it) return 1.0f;
        double m = zr * zr + zi * zi;
        float e = (float)(i + 1 - std::log(std::log(std::max(m, 1.0001)) * 0.5) / std::log(2.0));
        return sat(std::sqrt(std::max(0.0f, e)) / std::sqrt((float)it) * 1.25f) * 0.92f;
    });
    o.normalize = false;
}
void gPythagorasTree(GenOut& o, const GenCtx& c) {
    Field F(o, c); F.fill(0); int depth = (int)c.p[0]; float ang = c.p[1] * PI_F / 180; int outline = (int)c.p[2];
    std::function<void(float, float, float, float, int)> sq = [&](float ax, float ay, float bx, float by, int d) {
        // square on the base a-b (to its left when walking from a to b)
        float dx = bx - ax, dy = by - ay;
        float cx2 = bx + dy, cy2 = by - dx, dx2 = ax + dy, dy2 = ay - dx;
        float val = 0.25f + 0.75f * (float)(depth - d) / std::max(1, depth);
        if (std::sqrt(dx * dx + dy * dy) < 0.6f * c.px) return;
        if (outline) {
            float lw = std::max(c.px, std::sqrt(dx * dx + dy * dy) * 0.06f);
            F.line(ax, ay, bx, by, lw, val); F.line(bx, by, cx2, cy2, lw, val); F.line(cx2, cy2, dx2, dy2, lw, val); F.line(dx2, dy2, ax, ay, lw, val);
        } else {
            F.tri(ax, ay, bx, by, cx2, cy2, val); F.tri(ax, ay, cx2, cy2, dx2, dy2, val);
        }
        if (d <= 0) return;
        // the next two squares sit on the top edge, split at the apex of a right triangle
        float L = std::sqrt(dx * dx + dy * dy), ca = std::cos(ang);
        float ux = (cx2 - dx2) / L, uy = (cy2 - dy2) / L;   // along the top edge
        float nx = uy, ny = -ux;                              // outwards
        float l1 = L * ca;
        float px = dx2 + (ux * std::cos(ang) + nx * std::sin(ang)) * l1, py = dy2 + (uy * std::cos(ang) + ny * std::sin(ang)) * l1;
        sq(dx2, dy2, px, py, d - 1);
        sq(px, py, cx2, cy2, d - 1);
    };
    float s = F.h * 0.16f;
    sq(F.w / 2.0f - s / 2, F.h * 0.97f, F.w / 2.0f + s / 2, F.h * 0.97f, depth);
    o.normalize = false;
}
void gRecursiveSquares(GenOut& o, const GenCtx& c) {
    // whirling squares: each square inscribed in the last, turned a little; filled alternately
    Field F(o, c); int n = (int)c.p[0], depth = (int)c.p[1]; float twist = c.p[2], lw = c.p[3];
    float shrink = 1.0f / (std::cos(twist) + std::sin(twist));
    float cellPx = (float)F.w / n;
    F.each([&](float u, float v, int, int) {
        float x = frac(u * n) - 0.5f, y = frac(v * n) - 0.5f;
        int ix = (int)std::floor(u * n), iy = (int)std::floor(v * n);
        float dir = ((ix + iy) & 1) ? 1.0f : -1.0f;      // neighbours turn the other way
        float s = 0.5f, a = 0, best = 1e9f; int inside = 0;
        for (int k = 0; k < depth; k++) {
            float rx = rot2x(x, y, -a), ry = rot2y(x, y, -a);
            float m = std::max(std::fabs(rx), std::fabs(ry));
            best = std::min(best, std::fabs(m - s));
            if (m < s) inside = k + 1;
            s *= shrink; a += twist * dir;
        }
        if (best * cellPx < lw * c.px * 0.5f) return 1.0f;
        return 0.15f + 0.5f * (inside & 1) + 0.2f * (float)inside / depth;
    });
    o.normalize = false;
}
void gSierpinskiGasket(GenOut& o, const GenCtx& c) {
    Field F(o, c); int depth = (int)c.p[0]; int style = (int)c.p[1];
    F.each([&](float u, float v, int, int) {
        FULLXY
        // equilateral triangle, apex up, filling 90% of the height
        float hgt = 0.9f, side = hgt / 0.8660254f;
        float px = X / side + 0.5f, py = (0.45f - Y) / hgt;   // py: 0 bottom .. 1 apex
        float bx = px - py * 0.5f, by = py;                   // skewed: inside when bx>=0, by>=0, bx+by<=1
        if (bx < 0 || by < 0 || bx + by > 1) return lvl(0);
        for (int d = 0; d < depth; d++) {
            bx *= 2; by *= 2;
            if (bx >= 1) bx -= 1;
            else if (by >= 1) by -= 1;
            else if (bx + by > 1) return style ? lvl(1 + d % 3) : lvl(0);   // the removed middle triangle
        }
        return style ? lvl(0) : lvl(1 + (depth % 3));
    });
    o.normalize = false;
}
void gStainedGlass(GenOut& o, const GenCtx& c) {
    // Voronoi panes in lead came; each pane catches a little light from the upper left
    Field F(o, c); int n = (int)c.p[0]; float lead = c.p[1], jit = c.p[2];
    float cellPx = (float)F.w / n;
    F.each([&](float u, float v, int, int) {
        Worley w = worley(u, v, n, c.seed, jit);
        float edgePx = (w.f2 - w.f1) * 0.5f * cellPx;   // distance to the cell border, roughly
        if (edgePx < lead * c.px * 0.5f) return 0.0f;
        int k = (int)(hf(hash32(w.id)) * 3);
        float glow = 0.04f * (-w.dx - w.dy);
        return sat((1 + k) / 3.0f - 0.06f + glow);
    });
    o.normalize = false;
}

// =============================================================================================
// RETRO RASTER
// =============================================================================================
void gHalftone(GenOut& o, const GenCtx& c) {
    // amplitude-modulated screen at 45 degrees: dot area follows the source image
    Field F(o, c); int n = (int)c.p[0]; float con = c.p[1]; int shape = (int)c.p[2], src = (int)c.p[3];
    float cellPx = (float)F.w / n * 0.7071f;
    F.each([&](float u, float v, int, int) {
        float a = (u + v) * n, b = (v - u) * n;            // the 45 degree screen repeats on the tile
        float ca = std::floor(a) + 0.5f, cb = std::floor(b) + 0.5f;
        float cu = (ca - cb) / (2.0f * n), cv = (ca + cb) / (2.0f * n);   // the cell centre, back in u,v
        float t = contrastT(source(src, fmodp(cu, 1.0f), fmodp(cv, 1.0f), c.seed), con);
        float x = a - ca, y = b - cb, d, r;
        switch (shape) {
        case 1: d = std::max(std::fabs(x), std::fabs(y)); r = std::sqrt(t) * 0.5f; break;            // squares
        case 2: d = (std::fabs(x) + std::fabs(y)) * 0.5f; r = std::sqrt(t) * 0.5f; break;           // diamonds
        case 3: d = std::fabs(x); r = t * 0.5f; break;                                              // lines
        default: d = std::sqrt(x * x + y * y); r = std::sqrt(t / PI_F); break;                       // round
        }
        return (d - r) * cellPx < 0 ? lvl(1) : lvl(0);
    });
    o.normalize = false;
}
void gBayerDither(GenOut& o, const GenCtx& c) {
    // 8x8 ordered dither of a smooth image into four colours, on big square pixels
    Field F(o, c); int pix = std::max(1, (int)c.p[0]); int src = (int)c.p[1]; int levels = (int)c.p[2];
    int gw = std::max(1, c.dw / pix);
    F.each([&](float u, float v, int, int) {
        int gx = std::min(gw - 1, (int)(u * gw)), gy = std::min(gw - 1, (int)(v * gw));
        float t = source(src, (gx + 0.5f) / gw, (gy + 0.5f) / gw, c.seed);
        float s = t * (levels - 1);
        int i = std::min(levels - 2, (int)s);
        float th = (bayer8(gx, gy) + 0.5f) / 64.0f;
        int k = (s - i) > th ? i + 1 : i;
        return lvl(levels == 4 ? k : (levels == 3 ? std::min(3, k * 3 / 2) : k * 3));
    });
    o.normalize = false;
}
void gStipple(GenOut& o, const GenCtx& c) {
    // ink dots on a jittered grid; more of them where the source is dark
    Field F(o, c); int n = (int)c.p[0]; float size = c.p[1], con = c.p[2]; int src = (int)c.p[3];
    float cellPx = (float)F.w / n;
    F.each([&](float u, float v, int, int) {
        float x = u * n, y = v * n;
        int cx = (int)std::floor(x), cy = (int)std::floor(y);
        for (int j = -1; j <= 1; j++)
            for (int i = -1; i <= 1; i++) {
                int ix = cx + i, iy = cy + j;
                uint32_t h = hash3(pmod(ix, n), pmod(iy, n), c.seed);
                float px = ix + 0.15f + 0.7f * hf(h), py = iy + 0.15f + 0.7f * hf(hash32(h ^ 0x9e37));
                float r = size * 0.5f * (0.75f + 0.5f * hf(hash32(h + 5)));
                float d = std::sqrt((x - px) * (x - px) + (y - py) * (y - py));
                if ((d - r) * cellPx >= 0) continue;
                float t = contrastT(source(src, fmodp(px / n, 1.0f), fmodp(py / n, 1.0f), c.seed), con);
                if (hf(hash32(h + 17)) <= t) return lvl(1);
            }
        return lvl(0);
    });
    o.normalize = false;
}
void gCrosshatch(GenOut& o, const GenCtx& c) {
    // engraving: up to four hatch directions, each switched on below a darkness threshold
    Field F(o, c); int n = (int)c.p[0]; float wd = c.p[1], con = c.p[2]; int src = (int)c.p[3];
    float cellPx = (float)F.w / n;
    F.each([&](float u, float v, int, int) {
        float t = contrastT(source(src, u, v, c.seed), con);
        float q[4] = {u * n, v * n, (u + v) * n, (u - v) * n};
        float scale[4] = {1, 1, 0.7071f, 0.7071f};
        const float thr[4] = {0.15f, 0.4f, 0.62f, 0.82f};
        for (int k = 0; k < 4; k++) {
            if (t < thr[k]) break;
            float d = std::fabs(frac(q[k] + 0.5f) - 0.5f) * cellPx * scale[k];
            float w = wd * cellPx * (0.6f + 0.8f * (t - thr[k]));
            if (d < w * 0.5f) return lvl(1);
        }
        return lvl(0);
    });
    o.normalize = false;
}
void gLineScreen(GenOut& o, const GenCtx& c) {
    // banknote engraving: wavy lines that swell where the source is dark
    Field F(o, c); int n = (int)c.p[0]; float wave = c.p[1], con = c.p[2]; int src = (int)c.p[3];
    float cellPx = (float)F.w / n;
    F.each([&](float u, float v, int, int) {
        float q = v * n + wave * std::sin(TAU * u * 3) + wave * 0.5f * std::sin(TAU * (u * 5 + v * 2));
        float t = contrastT(source(src, u, v, c.seed), con);
        float d = std::fabs(frac(q + 0.5f) - 0.5f) * cellPx;
        float w = cellPx * (0.08f + 0.85f * t);
        return d < w * 0.5f ? lvl(1) : lvl(0);
    });
    o.normalize = false;
}

// =============================================================================================
// DECORATIVE
// =============================================================================================
void gDecoArches(GenOut& o, const GenCtx& c) {
    // rows of stepped rainbow arches, each row overlapping the one behind it
    Field F(o, c); int n = (int)c.p[0], bands = (int)c.p[1]; float gap = c.p[2];
    F.each([&](float u, float v, int, int) {
        float x = u * n, y = v * n;
        // rows are drawn back to front: the arch of the row below covers the row above
        for (int dj = 1; dj >= 0; dj--) {
            int row = (int)std::floor(y) + dj;
            float off = pmod(row, 2) ? 0.5f : 0.0f;
            float cx = std::floor(x - off) + 0.5f + off, cy = (float)row;
            for (int di = -1; di <= 1; di++) {
                float ax = cx + di, dx = x - ax, dy = y - cy;
                if (dy > 0) continue;   // arches stand on their base line
                float r = std::sqrt(dx * dx + dy * dy) / 0.62f;
                if (r >= 1) continue;
                float b = r * bands;
                int k = (int)std::floor(b);
                if (b - k < gap) return lvl(0);
                return lvl(1 + pmod(k + row, 3));
            }
        }
        return lvl(0);
    });
    o.normalize = false;
}
void gDecoFans(GenOut& o, const GenCtx& c) {
    // seigaiha-style fans: overlapping scales, each pleated with ribs and rings
    Field F(o, c); int n = (int)c.p[0], ribs = (int)c.p[1], rings = (int)c.p[2]; float rim = c.p[3];
    float cellPx = (float)F.w / n;
    F.each([&](float u, float v, int, int) {
        float x = u * n, y = v * n * 2.0f;   // rows half a fan apart
        int r0 = (int)std::floor(y);
        for (int row = r0 + 1; row >= r0 - 1; row--) {   // nearer rows (lower) on top
            float off = pmod(row, 2) ? 0.5f : 0.0f;
            float cx = std::floor(x - off) + 0.5f + off, cy = (float)row + 1.0f;
            for (int di = -1; di <= 1; di++) {
                float dx = x - (cx + di), dy = (y - cy) * 0.5f;
                float r = std::sqrt(dx * dx + dy * dy);
                if (r >= 0.5f || dy > 0) continue;
                float rp = (0.5f - r) * cellPx;
                if (rp < rim * c.px) return lvl(3);                                // rim
                float a = std::atan2(-dy, dx) / PI_F;                                 // 0..1 across the fan
                float rib = std::fabs(frac(a * ribs + 0.5f) - 0.5f) / ribs * PI_F * r * cellPx;
                if (rib < 0.6f * c.px * 1.2f && r > 0.12f) return lvl(2);
                float ring = std::fabs(frac(r / 0.5f * rings + 0.5f) - 0.5f) * 0.5f / rings * cellPx;
                if (ring < 0.7f * c.px && r > 0.1f) return lvl(2);
                return lvl(1);
            }
        }
        return lvl(0);
    });
    o.normalize = false;
}
void gDecoSunburst(GenOut& o, const GenCtx& c) {
    // Art Deco sunrise: a stepped sun on the horizon throwing alternating rays
    Field F(o, c); int rays = (int)c.p[0], rings = (int)c.p[1]; float hor = c.p[2];
    F.each([&](float u, float v, int, int) {
        float X = (u - 0.5f) * c.aspect, Y = v - hor;
        float r = std::sqrt(X * X + Y * Y), a = std::atan2(-Y, X);
        if (Y > 0) return pmod((int)std::floor(Y * 18), 2) ? lvl(3) : lvl(2);   // the sea below the horizon
        float sun = 0.22f;
        if (r < sun) {
            int k = (int)std::floor(r / sun * rings);
            return lvl(1 + (k & 1) * 2);
        }
        if (r < sun * 1.08f) return lvl(0);
        int k = (int)std::floor(a / PI_F * rays);
        return (k & 1) ? lvl(1) : lvl(0);
    });
    o.normalize = false;
}
void gRosette(GenOut& o, const GenCtx& c) {
    // layered petals with N-fold symmetry, each layer turned half a petal from the last
    Field F(o, c); int petals = (int)c.p[0], layers = (int)c.p[1]; float point = c.p[2], lw = c.p[3];
    F.each([&](float u, float v, int, int) {
        FULLXY
        float r = std::sqrt(X * X + Y * Y), th = std::atan2(Y, X);
        float val = 0.05f, px1 = 1.0f / F.h;
        for (int k = 0; k < layers; k++) {
            float R = 0.46f * (1.0f - k * 0.8f / layers);
            float ph = th * petals + k * PI_F;
            float shape = std::pow(std::fabs(std::cos(ph * 0.5f)), point);
            float edge = R * (0.55f + 0.45f * shape);
            float d = r - edge;
            if (d < 0) {
                val = 0.3f + 0.7f * (float)(k + 1) / layers * (0.75f + 0.25f * r / edge);
                if (d > -lw * c.px * px1) val = 1.0f;   // the petal's outline
            }
        }
        if (r < 0.03f) val = 1.0f;
        return val;
    });
    o.normalize = false;
}
void gKaleidoscope2(GenOut& o, const GenCtx& c) {
    // mirrored wedges of a field of coloured shards
    Field F(o, c); int mirrors = (int)c.p[0], shards = (int)c.p[1]; float zoom = c.p[2], lead = c.p[3];
    float px1 = 1.0f / F.h;
    F.each([&](float u, float v, int, int) {
        FULLXY
        float r = std::sqrt(X * X + Y * Y), th = std::atan2(Y, X);
        float wedge = PI_F / mirrors;
        th = fmodp(th, 2 * wedge);
        if (th > wedge) th = 2 * wedge - th;   // mirror
        float x = r * std::cos(th) * zoom, y = r * std::sin(th) * zoom;
        Worley w = worley(fmodp(x + 0.37f, 1.0f), fmodp(y + 0.61f, 1.0f), shards, c.seed, 0.95f);
        float edge = (w.f2 - w.f1) * 0.5f / shards / zoom;   // distance to a shard border, in screen heights
        if (edge < lead * c.px * px1 * 0.5f) return lvl(0);
        return lvl(1 + (int)(hf(hash32(w.id)) * 2.999f));
    });
    o.normalize = false;
}

// =============================================================================================
// TOPOGRAPHIC
// =============================================================================================
namespace {
// iso-lines of a field already stored in F.o.f (in level units): returns line coverage per pixel
void isoLines(Field& F, std::vector<float>& t, float widthPx, int indexEvery, std::vector<float>& cov2, std::vector<float>& idx) {
    int W = F.w, H = F.h;
    cov2.assign(t.size(), 0); idx.assign(t.size(), 0);
    bool wrap = F.c.tile;
    ParallelFor(H, [&](int y) {
        for (int x = 0; x < W; x++) {
            auto T = [&](int xx, int yy) { if (wrap) return t[(size_t)pmod(yy, H) * W + pmod(xx, W)]; return t[(size_t)std::clamp(yy, 0, H - 1) * W + std::clamp(xx, 0, W - 1)]; };
            float v0 = T(x, y);
            float gx = 0.5f * (T(x + 1, y) - T(x - 1, y)), gy = 0.5f * (T(x, y + 1) - T(x, y - 1));
            float g = std::sqrt(gx * gx + gy * gy) + 1e-6f;
            int l = (int)std::floor(v0 + 0.5f);
            float d = std::fabs(v0 - l) / g;
            bool isIdx = indexEvery > 0 && pmod(l, indexEvery) == 0;
            float w = widthPx * (isIdx ? 2.0f : 1.0f);
            cov2[(size_t)y * W + x] = sat(0.5f - (d - w * 0.5f));
            idx[(size_t)y * W + x] = isIdx ? 1.0f : 0.0f;
        }
    });
}
}   // namespace
void gContourLines(GenOut& o, const GenCtx& c) {
    Field F(o, c); int levels = (int)c.p[0]; float lw = c.p[1]; int every = (int)c.p[2], oct = (int)c.p[3];
    F.each([&](float u, float v, int, int) { return (fbm(u, v, 2, oct, 0.5f, c.seed) * 0.5f + 0.5f) * levels * 1.6f; });
    std::vector<float> t = o.f, cv, ix;
    isoLines(F, t, lw * c.px, every, cv, ix);
    for (size_t i = 0; i < o.f.size(); i++) {
        float h = t[i] / (levels * 1.6f);
        float bg = 0.08f + 0.18f * h;
        o.f[i] = lerpf(bg, ix[i] > 0 ? 1.0f : 0.72f, cv[i]);
    }
    o.normalize = false;
}
void gElevationBands(GenOut& o, const GenCtx& c) {
    // hypsometric tint: stepped elevation colours with relief shading from the north-west
    Field F(o, c); int bands = (int)c.p[0]; float relief = c.p[1]; int oct = (int)c.p[2]; float lines = c.p[3];
    F.each([&](float u, float v, int, int) { return sat(fbm(u, v, 2, oct, 0.5f, c.seed) * 0.9f + 0.5f); });
    std::vector<float> h = o.f;
    int W = F.w, Hh = F.h;
    std::vector<float> t(h.size());
    for (size_t i = 0; i < h.size(); i++) t[i] = h[i] * bands;
    std::vector<float> cv, ix;
    isoLines(F, t, 0.8f * c.px, 0, cv, ix);
    float scale = (float)W / 256.0f;   // relief strength independent of resolution
    ParallelFor(Hh, [&](int y) {
        for (int x = 0; x < W; x++) {
            auto Hs = [&](int xx, int yy) { return h[(size_t)pmod(yy, Hh) * W + pmod(xx, W)]; };
            float gx = (Hs(x + 1, y) - Hs(x - 1, y)) * scale * 40.0f, gy = (Hs(x, y + 1) - Hs(x, y - 1)) * scale * 40.0f;
            float shade = sat(0.5f - (gx + gy) * 0.35f);
            float band = (std::floor(h[(size_t)y * W + x] * bands) + 0.5f) / bands;
            float val = lerpf(band, band * (0.55f + 0.9f * shade), relief);
            val = lerpf(val, val * 0.6f, cv[(size_t)y * W + x] * lines);
            o.f[(size_t)y * W + x] = sat(val);
        }
    });
    o.normalize = false;
}
void gRidgelines(GenOut& o, const GenCtx& c) {
    // stacked waveforms (the 1979 pulsar plot look): each line hides the ones behind it
    Field F(o, c); int n = (int)c.p[0]; float amp = c.p[1], width = c.p[2], lw = c.p[3];
    std::vector<uint32_t> seeds(n);
    for (int i = 0; i < n; i++) seeds[i] = hash3(i, 0, c.seed);
    float top = 0.16f, bot = 0.86f, step = (bot - top) / std::max(1, n - 1), px1 = 1.0f / F.h;
    auto curve = [&](int i, float X) {
        float env = std::exp(-X * X / (2 * (width * 0.25f) * (width * 0.25f)));
        uint32_t s = seeds[i];
        float bumps = 0;
        for (int k = 0; k < 4; k++) {
            float cx = (hf(hash32(s + k * 7)) - 0.5f) * width * 0.8f, w = 0.015f + 0.05f * hf(hash32(s + k * 13));
            bumps += hf(hash32(s + k * 29)) * std::exp(-(X - cx) * (X - cx) / (2 * w * w));
        }
        float noise = 0.25f * (fbm(fmodp(X * 2 + 0.5f, 1.0f), (i + 0.5f) / n, 6, 3, 0.5f, s) + 0.5f);
        return (bumps * 0.7f + noise) * env * amp * step * 9.0f;
    };
    std::vector<float> yc((size_t)n * F.w);
    int W = F.w;
    ParallelFor(n, [&](int i) { for (int x = 0; x < W; x++) yc[(size_t)i * W + x] = top + i * step - curve(i, ((x + 0.5f) / W - 0.5f) * c.aspect); });
    F.each([&](float, float v, int x, int) {
        for (int i = n - 1; i >= 0; i--) {   // front (bottom) first
            float ycv = yc[(size_t)i * W + x];
            float d = (v - ycv) / px1;    // pixels below the line
            float t = lw * c.px * 0.5f;
            if (std::fabs(d) < t + 0.5f) return std::max(0.06f, sat(t + 0.5f - std::fabs(d)));
            if (d > 0) return 0.06f;     // hidden behind this line's fill
        }
        return 0.06f;
    });
    o.normalize = false;
}
void gFlowLines(GenOut& o, const GenCtx& c) {
    // parallel lines carried along by a smooth warp: contours of a flowing field
    Field F(o, c); int n = (int)c.p[0]; float warp = c.p[1], lw = c.p[2]; int sc = (int)c.p[3];
    F.each([&](float u, float v, int, int) {
        float w1 = fbm(u, v, sc, 3, 0.5f, c.seed), w2 = fbm(u, v, sc, 3, 0.5f, c.seed + 9);
        return v * n + warp * n * 0.12f * (w1 + 0.5f * w2) + 0.3f * std::sin(TAU * u * 2);
    });
    std::vector<float> t = o.f, cv, ix;
    isoLines(F, t, lw * c.px, 0, cv, ix);
    for (size_t i = 0; i < o.f.size(); i++) {
        float band = frac(t[i] / 3.0f);
        o.f[i] = lerpf(0.06f + 0.1f * band, 0.35f + 0.65f * (0.5f + 0.5f * std::sin(t[i] * 0.7f)), cv[i]);
    }
    o.normalize = false;
}

// =============================================================================================
// ABSTRACT
// =============================================================================================
void gLowPoly(GenOut& o, const GenCtx& c) {
    // a jittered triangulation with heights, flat-shaded: faceted crystal terrain
    Field F(o, c); int n = (int)c.p[0]; float jit = c.p[1], relief = c.p[2], edges = c.p[3];
    std::vector<float> VX((size_t)n * n), VY(VX.size()), VZ(VX.size());
    for (int j = 0; j < n; j++)
        for (int i = 0; i < n; i++) {
            uint32_t h = hash3(i, j, c.seed);
            size_t k = (size_t)j * n + i;
            VX[k] = (hf(h) - 0.5f) * jit; VY[k] = (hf(hash32(h ^ 0x51ed)) - 0.5f) * jit;
            VZ[k] = fbm(i / (float)n, j / (float)n, 2, 3, 0.5f, c.seed + 3) * 1.6f + 0.5f;
        }
    auto vert = [&](int i, int j, float& x, float& y, float& z) {
        size_t k = (size_t)pmod(j, n) * n + pmod(i, n);
        x = i + 0.5f + VX[k]; y = j + 0.5f + VY[k]; z = VZ[k];
    };
    float cellPx = (float)F.w / n;
    F.each([&](float u, float v, int, int) {
        float x = u * n, y = v * n;
        int ci = (int)std::floor(x - 0.5f), cj = (int)std::floor(y - 0.5f);
        for (int j = cj - 1; j <= cj + 1; j++)
            for (int i = ci - 1; i <= ci + 1; i++) {
                float P[4][3];
                vert(i, j, P[0][0], P[0][1], P[0][2]); vert(i + 1, j, P[1][0], P[1][1], P[1][2]);
                vert(i, j + 1, P[2][0], P[2][1], P[2][2]); vert(i + 1, j + 1, P[3][0], P[3][1], P[3][2]);
                bool diag = hash3(pmod(i, n), pmod(j, n), c.seed + 11) & 1;
                int T[2][3] = {{0, 1, 3}, {0, 3, 2}};
                if (diag) { T[0][0] = 0; T[0][1] = 1; T[0][2] = 2; T[1][0] = 1; T[1][1] = 3; T[1][2] = 2; }
                for (auto& t : T) {
                    float* A = P[t[0]]; float* B = P[t[1]]; float* C = P[t[2]];
                    auto e = [](float* a, float* b, float px, float py) { return (b[0] - a[0]) * (py - a[1]) - (b[1] - a[1]) * (px - a[0]); };
                    float e0 = e(A, B, x, y), e1 = e(B, C, x, y), e2 = e(C, A, x, y);
                    if (!((e0 >= 0 && e1 >= 0 && e2 >= 0) || (e0 <= 0 && e1 <= 0 && e2 <= 0))) continue;
                    float ux = B[0] - A[0], uy = B[1] - A[1], uz = (B[2] - A[2]) * relief, vx = C[0] - A[0], vy = C[1] - A[1], vz = (C[2] - A[2]) * relief;
                    float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
                    float l = std::sqrt(nx * nx + ny * ny + nz * nz) + 1e-6f;
                    if (nz < 0) { nx = -nx; ny = -ny; nz = -nz; }
                    float lit = sat((nx * -0.5f + ny * -0.55f + nz * 0.67f) / l);
                    float hgt = (A[2] + B[2] + C[2]) / 3;
                    float val = sat(0.15f + 0.45f * lit + 0.4f * hgt * 0.8f);
                    // distance to the triangle's edges, in pixels, for the glinting edges
                    float de = std::min({segDist(x, y, A[0], A[1], B[0], B[1]), segDist(x, y, B[0], B[1], C[0], C[1]), segDist(x, y, C[0], C[1], A[0], A[1])}) * cellPx;
                    return lerpf(val, sat(val + 0.25f), edges * cov(de - 0.5f * c.px));
                }
            }
        return 0.5f;
    });
    o.normalize = false;
}
void gHoneycomb2(GenOut& o, const GenCtx& c) {
    // hexagonal cells with rounded wax walls, each cell filled to its own level
    Field F(o, c); int n = (int)c.p[0]; float wall = c.p[1], fill = c.p[2];
    float hexPx = (float)F.w / n;
    F.each([&](float u, float v, int, int) {
        Hex h = hexAt(u, v, n);
        float e = hexEdge(h.dx, h.dy, h.ap);   // in hex widths
        float ePx = e * hexPx;
        float w = wall * hexPx;
        if (ePx < w * 0.5f) {   // the wall: a rounded ridge
            float k = ePx / (w * 0.5f);
            return 0.55f + 0.45f * std::sqrt(std::max(0.0f, 1 - (1 - k) * (1 - k)));
        }
        uint32_t id = hash3(h.col, h.row, c.seed);
        float lev = 0.1f + 0.3f * hf(id) * fill;
        float r = std::sqrt(h.dx * h.dx + h.dy * h.dy) / h.R;
        return lev + 0.12f * (1 - r);
    });
    o.normalize = false;
}
void gBlobs(GenOut& o, const GenCtx& c) {
    // lava-lamp blobs: circles melted together with a smooth minimum, outlined
    Field F(o, c); int n = (int)c.p[0]; float size = c.p[1], k = c.p[2], line = c.p[3];
    Rng r(c.seed);
    struct B { float x, y, r; };
    std::vector<B> bs;
    for (int i = 0; i < n; i++) bs.push_back({r.uni(), r.uni(), size * (0.5f + 0.7f * r.uni()) * 0.12f});
    float px1 = onePx(c);
    F.each([&](float u, float v, int, int) {
        float d = 1e9f;
        for (auto& b : bs) {
            float dx = wrapd(u - b.x), dy = wrapd(v - b.y);
            float di = std::sqrt(dx * dx + dy * dy) - b.r;
            float kk = std::max(1e-4f, k * 0.06f);   // smooth minimum (polynomial)
            float hh = sat(0.5f + 0.5f * (di - d) / kk);
            d = lerpf(di, d, hh) - kk * hh * (1 - hh);
        }
        float lw = line * c.px * px1;
        if (std::fabs(d) < lw * 0.5f + px1) return lerpf(d < 0 ? 0.75f : 0.1f, 1.0f, aa(std::fabs(d) - lw * 0.5f, px1));
        if (d < 0) return 0.45f + 0.35f * sat(-d / 0.08f);
        return 0.1f + 0.06f * sat(d / 0.2f);
    });
    o.normalize = false;
}
void gCircuit(GenOut& o, const GenCtx& c) {
    // printed-circuit traces on a grid with 45-degree bends, pads at the ends and vias
    Field F(o, c); F.fill(lvl(0));
    int g = (int)c.p[0], traces = (int)c.p[1]; float wd = c.p[2], pads = c.p[3];
    Rng r(c.seed);
    std::vector<uint8_t> used((size_t)g * g, 0);
    float cell = (float)F.w / g, lw = std::max(c.px, wd * cell);
    static const int DX[8] = {1, 1, 0, -1, -1, -1, 0, 1}, DY[8] = {0, 1, 1, 1, 0, -1, -1, -1};
    for (int t = 0; t < traces; t++) {
        int x = r.irange(0, g - 1), y = r.irange(0, g - 1);
        if (used[(size_t)y * g + x]) continue;
        int dir = r.irange(0, 3) * 2;   // start straight
        int len = r.irange(3, std::max(4, g / 2));
        std::vector<std::pair<int, int>> path{{x, y}};
        used[(size_t)y * g + x] = 1;
        for (int s = 0; s < len; s++) {
            if (r.uni() < 0.25f) dir = (dir + (r.uni() < 0.5f ? 1 : 7)) & 7;
            int nx = x + DX[dir], ny = y + DY[dir];
            size_t k = (size_t)pmod(ny, g) * g + pmod(nx, g);
            if (used[k]) break;
            used[k] = 1;
            x = nx; y = ny;
            path.push_back({x, y});
        }
        if (path.size() < 2) continue;
        for (size_t i = 0; i + 1 < path.size(); i++)
            F.line((path[i].first + 0.5f) * cell, (path[i].second + 0.5f) * cell, (path[i + 1].first + 0.5f) * cell, (path[i + 1].second + 0.5f) * cell, lw, lvl(1));
        for (auto e : {path.front(), path.back()}) {
            float px_ = (e.first + 0.5f) * cell, py_ = (e.second + 0.5f) * cell;
            if (r.uni() < pads) { F.disc(px_, py_, lw * 1.3f, lvl(2), true); F.disc(px_, py_, lw * 0.5f, lvl(3), true); }
            else F.disc(px_, py_, lw * 0.9f, lvl(2), true);
        }
    }
    // a few lonely vias
    for (int i = 0; i < traces / 3; i++) {
        float px_ = (r.irange(0, g - 1) + 0.5f) * cell, py_ = (r.irange(0, g - 1) + 0.5f) * cell;
        F.disc(px_, py_, lw * 0.9f, lvl(2), true); F.disc(px_, py_, lw * 0.4f, lvl(3), true);
    }
    o.normalize = false;
}

// =============================================================================================
// PROCEDURAL TEXTURES
// =============================================================================================
void gTurbulence(GenOut& o, const GenCtx& c) {
    Field F(o, c); int sc = (int)c.p[0], oct = (int)c.p[1]; float rough = c.p[2];
    F.each([&](float u, float v, int, int) {
        float s = 0, a = 1, nrm = 0; int p = sc;
        for (int k = 0; k < oct; k++) { s += a * std::fabs(perlin(u * p, v * p, p, p, c.seed + k * 131U)); nrm += a; a *= rough; p *= 2; }
        return s / nrm;
    });
    o.lo = 0.005f; o.hi = 0.995f;
}
void gFractalNoise(GenOut& o, const GenCtx& c) {
    Field F(o, c); int sc = (int)c.p[0], oct = (int)c.p[1]; float rough = c.p[2]; int style = (int)c.p[3];
    F.each([&](float u, float v, int, int) {
        if (style == 1) return ridged(u, v, sc, oct, c.seed);
        float f = fbm(u, v, sc, oct, rough, c.seed);
        return style == 2 ? std::fabs(f) : f;
    });
    o.lo = 0.005f; o.hi = 0.995f;
}
void gDistortedGrid(GenOut& o, const GenCtx& c) {
    Field F(o, c); int n = (int)c.p[0]; float warp = c.p[1], lw = c.p[2]; int fill = (int)c.p[3];
    float cellPx = (float)F.w / n;
    F.each([&](float u, float v, int, int) {
        float wu = warp * 0.35f * fbm(u, v, 2, 3, 0.5f, c.seed) / n * 3, wv = warp * 0.35f * fbm(u, v, 2, 3, 0.5f, c.seed + 7) / n * 3;
        float x = (u + wu) * n, y = (v + wv) * n;
        float dx = std::fabs(frac(x + 0.5f) - 0.5f) * cellPx, dy = std::fabs(frac(y + 0.5f) - 0.5f) * cellPx;
        float l = std::max(cov(dx - lw * c.px * 0.5f), cov(dy - lw * c.px * 0.5f));
        float ck = (((int)std::floor(x) + (int)std::floor(y)) & 1) ? 0.35f : 0.18f;
        float base = fill == 0 ? 0.12f : ck;
        if (fill == 1) return base;
        return lerpf(base, 1.0f, l);
    });
    o.normalize = false;
}
void gLiquidMarble(GenOut& o, const GenCtx& c) {
    Field F(o, c); int veins = (int)c.p[0]; float turb = c.p[1], sharp = c.p[2];
    F.each([&](float u, float v, int, int) {
        float t = 0, a = 1; int p = 2;
        for (int k = 0; k < 5; k++) { t += a * std::fabs(perlin(u * p, v * p, p, p, c.seed + k * 71U)); a *= 0.5f; p *= 2; }
        float s = std::sin(TAU * (u * veins + v * veins * 0.5f) + turb * 8.0f * t);
        return std::pow(1.0f - std::fabs(s), sharp);
    });
    o.lo = 0.002f; o.hi = 0.998f;
}
