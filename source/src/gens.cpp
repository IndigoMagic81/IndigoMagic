// The 100 original generators: fractals and chaos maths, procedural texture, demoscene / early CGI,
// neon-90s graphics and text-friendly textures. Every tile generator is seamless (periodic
// noise / integer frequencies / wrapped rasterisation); full-screen ones compose per display.
#include "genutil.h"

// ---------------------------------------------------------------------------------------------
// SILICON DREAMS - mathematics, fractals, abstract systems (40)
// ---------------------------------------------------------------------------------------------
static float escapeSmooth(double zr, double zi, double cr, double ci, int maxIt, int mode) {
    int i = 0;
    for (; i < maxIt; i++) {
        double r2 = zr * zr, i2 = zi * zi;
        if (r2 + i2 > 256.0) break;
        double nr = r2 - i2 + cr;
        double ni = (mode == 1 ? absf((float)(2 * zr * zi)) : 2 * zr * zi) + ci;
        if (mode == 1) { nr = r2 - i2 + cr; }
        zr = nr; zi = ni;
        if (mode == 1) { zr = absf((float)zr); zi = absf((float)zi); zr = zr; }
    }
    if (i >= maxIt) return -1.0f;
    double m = zr * zr + zi * zi;
    return (float)(i + 1 - std::log(std::log(m) * 0.5) / std::log(2.0));
}
static void gMandelbrot(GenOut& o, const GenCtx& c) {
    Field F(o, c);
    double zoom = std::pow(2.0, c.p[0]), cx = c.p[2], cy = c.p[3]; int it = (int)c.p[1];
    F.each([&](float u, float v, int, int) {
        FULLXY
        double cr = cx + X * 3.0 / zoom, ci = cy + Y * 3.0 / zoom;
        float e = escapeSmooth(0, 0, cr, ci, it, 0);
        return e < 0 ? 0.0f : 0.15f + 0.85f * fmodp(std::sqrt(e) * 0.35f, 1.0f);
    });
    o.normalize = false;
}
static void gJulia(GenOut& o, const GenCtx& c) {
    Field F(o, c);
    double zoom = std::pow(2.0, c.p[2]); int it = (int)c.p[3];
    F.each([&](float u, float v, int, int) {
        FULLXY
        float e = escapeSmooth(X * 3.2 / zoom, Y * 3.2 / zoom, c.p[0], c.p[1], it, 0);
        return e < 0 ? 1.0f : fmodp(std::sqrt(e) * 0.3f, 1.0f) * 0.9f;
    });
    o.normalize = false;
}
static void gNewton(GenOut& o, const GenCtx& c) {
    Field F(o, c);
    int deg = (int)c.p[0]; double zoom = std::pow(2.0, c.p[1]);
    F.each([&](float u, float v, int, int) {
        FULLXY
        double zr = X * 3 / zoom, zi = Y * 3 / zoom; int i;
        for (i = 0; i < 40; i++) {
            // z^deg via polar
            double r = std::sqrt(zr * zr + zi * zi), th = std::atan2(zi, zr);
            if (r < 1e-9) break;
            double rn = std::pow(r, deg - 1);
            double pr = rn * r * std::cos(deg * th) - 1, pi_ = rn * r * std::sin(deg * th);
            double dr = deg * rn * std::cos((deg - 1) * th), di = deg * rn * std::sin((deg - 1) * th);
            double den = dr * dr + di * di;
            double qr = (pr * dr + pi_ * di) / den, qi = (pi_ * dr - pr * di) / den;
            zr -= qr; zi -= qi;
            if (qr * qr + qi * qi < 1e-12) break;
        }
        double th = std::atan2(zi, zr); int root = (int)std::lround(th / (TAU / deg)); root = pmod(root, deg);
        float base = (root + 0.5f) / deg;
        return base * 0.75f + 0.25f * (1.0f - std::min(1.0f, i / 20.0f));
    });
    o.normalize = false;
}
static void gLyapunov(GenOut& o, const GenCtx& c) {
    Field F(o, c);
    const char* seq = "AABAB"; int sl = 5;
    F.each([&](float u, float v, int, int) {
        double a = 2.0 + u * 2.0, b = 2.0 + (1 - v) * 2.0, x = 0.5, lam = 0;
        for (int i = 0; i < 90; i++) { double r = seq[i % sl] == 'A' ? a : b; x = r * x * (1 - x); if (i >= 10) lam += std::log(absf((float)(r * (1 - 2 * x))) + 1e-12); }
        lam /= 80;
        return (float)(lam < 0 ? 0.5 - std::min(0.5, -lam * 0.6) : 0.5 + std::min(0.5, lam * 0.9));
    });
    o.lo = 0.0f; o.hi = 1.0f;
}
static void gSierpinski(GenOut& o, const GenCtx& c) {
    Field F(o, c); int depth = (int)c.p[0];
    F.each([&](float u, float v, int, int) {
        int x = (int)(u * 243), y = (int)(v * 243); int lev = 0;
        for (int d = 0; d < depth; d++) { if (x % 3 == 1 && y % 3 == 1) return lvl(3 - std::min(3, d)); x /= 3; y /= 3; lev++; }
        return lvl(0);
    });
    o.normalize = false;
}
static void hilbertXY(int n, int d, int& x, int& y) {
    x = y = 0;
    for (int s = 1; s < n; s *= 2) {
        int rx = 1 & (d / 2), ry = 1 & (d ^ rx);
        if (ry == 0) { if (rx == 1) { x = s - 1 - x; y = s - 1 - y; } std::swap(x, y); }
        x += s * rx; y += s * ry; d /= 4;
    }
}
static void gHilbert(GenOut& o, const GenCtx& c) {
    Field F(o, c); int order = (int)c.p[0]; int n = 1 << order; float cell = (float)F.w / n, t = std::max(c.px, cell * c.p[1]);
    F.fill(0);
    int px = 0, py = 0;
    for (int d = 0; d < n * n; d++) {
        int x, y; hilbertXY(n, d, x, y);
        if (d) F.line((px + 0.5f) * cell, (py + 0.5f) * cell, (x + 0.5f) * cell, (y + 0.5f) * cell, t, 0.6f + 0.4f * (1.0f - std::fabs(2.0f * d / (n * n) - 1.0f)));
        px = x; py = y;
    }
    o.lo = 0; o.hi = 1; o.normalize = false;
}
static void gLissajous(GenOut& o, const GenCtx& c) {
    Field F(o, c); F.fill(0); Rng r(c.seed);
    int lines = (int)c.p[2];
    for (int k = 0; k < lines; k++) {
        float A = c.p[0] + k * 0.0f, B = c.p[1], ph = r.uni() * TAU, amp = 0.42f - k * 0.02f;
        std::vector<std::pair<float, float>> pts;
        for (int i = 0; i <= 1500; i++) {
            float t = i / 1500.0f * TAU;
            pts.push_back({F.w * (0.5f + amp * std::sin(A * t + ph) * (c.aspect > 1 ? 1 : c.aspect)), F.h * (0.5f + amp * std::sin(B * t) * (c.aspect > 1 ? 1 / c.aspect * c.aspect / c.aspect : 1))});
        }
        F.polyline(pts, std::max(c.px, F.h / 400.0f), 0.35f + 0.65f * (k + 1.0f) / lines);
    }
    F.blur(1); o.normalize = false; o.lo = 0; o.hi = 1;
}
static void gHarmonograph(GenOut& o, const GenCtx& c) {
    Field F(o, c); F.fill(0); Rng r(c.seed);
    float damp = c.p[0], f1 = c.p[1], f2 = c.p[2];
    float p1 = r.uni() * TAU, p2 = r.uni() * TAU, p3 = r.uni() * TAU, p4 = r.uni() * TAU;
    std::vector<std::pair<float, float>> pts;
    for (int i = 0; i < 12000; i++) {
        float t = i * 0.012f, e = std::exp(-damp * t);
        float x = e * (std::sin(f1 * t + p1) + 0.5f * std::sin(f2 * t + p2)), y = e * (std::sin(f2 * t + p3) + 0.5f * std::sin(f1 * 1.01f * t + p4));
        pts.push_back({F.w / 2.0f + x * F.h * 0.3f, F.h / 2.0f + y * F.h * 0.3f});
    }
    for (size_t i = 0; i + 1 < pts.size(); i++) F.line(pts[i].first, pts[i].second, pts[i + 1].first, pts[i + 1].second, std::max(c.px, F.h / 600.0f), 0.3f + 0.7f * (1 - i / (float)pts.size()));
    F.blur(1); o.normalize = false;
}
static void gPhyllotaxis(GenOut& o, const GenCtx& c) {
    Field F(o, c); F.fill(0);
    int n = (int)c.p[0]; float sp = c.p[1] * F.h / 500.0f, ds = c.p[2];
    for (int i = 0; i < n; i++) {
        float ang = i * 2.39996323f, r = sp * std::sqrt((float)i);
        float x = F.w / 2.0f + r * std::cos(ang), y = F.h / 2.0f + r * std::sin(ang);
        F.disc(x, y, std::max(c.px, ds * sp * std::sqrt((float)i) / std::sqrt((float)n) + c.px), 0.3f + 0.7f * (i / (float)n), true);
    }
    o.normalize = false;
}
static void gMoire(GenOut& o, const GenCtx& c) {
    // two families of concentric rings, slightly apart: their product blooms into moire fringes
    Field F(o, c); float sp = c.p[0], off = c.p[1];
    F.each([&](float u, float v, int, int) {
        FULLXY
        float d1 = std::sqrt((X + off * 0.5f) * (X + off * 0.5f) + (Y + off * 0.15f) * (Y + off * 0.15f));
        float d2 = std::sqrt((X - off * 0.5f) * (X - off * 0.5f) + (Y - off * 0.15f) * (Y - off * 0.15f));
        float r1 = 0.5f + 0.5f * std::cos(d1 * sp * TAU), r2 = 0.5f + 0.5f * std::cos(d2 * sp * TAU);
        return r1 * r2;
    });
    o.lo = 0; o.hi = 1;
}
static void gChladni(GenOut& o, const GenCtx& c) {
    Field F(o, c); float n = c.p[0], m = c.p[1];
    F.each([&](float u, float v, int, int) {
        float x = u * c.aspect, y = v;
        float z = std::cos(n * PI_F * x) * std::cos(m * PI_F * y) - std::cos(m * PI_F * x) * std::cos(n * PI_F * y);
        return 1.0f - sat(absf(z) * 2.5f);
    });
    o.normalize = false;
}
static void gRose(GenOut& o, const GenCtx& c) {
    Field F(o, c); F.fill(0); float k = c.p[0]; int petals = (int)c.p[1];
    for (int j = 0; j < petals; j++) {
        std::vector<std::pair<float, float>> pts;
        float sc = F.h * 0.46f * (1 - j * 0.12f);
        for (int i = 0; i <= 3000; i++) { float t = i / 3000.0f * TAU * 4; float rr = std::cos(k * t) * sc; pts.push_back({F.w / 2.0f + rr * std::cos(t), F.h / 2.0f + rr * std::sin(t)}); }
        F.polyline(pts, std::max(c.px, F.h / 350.0f), 1.0f - j * 0.2f);
    }
    F.blur(1); o.normalize = false;
}
static void gDomainWarp(GenOut& o, const GenCtx& c) {
    Field F(o, c); int sc = (int)c.p[0]; float wp = c.p[1]; int oct = (int)c.p[2];
    F.each([&](float u, float v, int, int) {
        float q1 = fbm(u, v, sc, oct, 0.5f, c.seed + 1), q2 = fbm(u, v, sc, oct, 0.5f, c.seed + 2);
        float r1 = fbm(u + wp * q1, v + wp * q2, sc, oct, 0.5f, c.seed + 3), r2 = fbm(u + wp * q2 + 0.3f, v + wp * q1 + 0.7f, sc, oct, 0.5f, c.seed + 4);
        return fbm(u + wp * r1, v + wp * r2, sc, oct, 0.5f, c.seed + 5);
    });
}
static void gReactionDiffusion(GenOut& o, const GenCtx& c) {
    // Gray-Scott on a torus (Karl Sims' formulation: Da=1, Db=0.5, 3x3 Laplacian, dt=1), grown on
    // the design grid and drawn smoothly at the output size
    Field F(o, c); int N = std::max(16, c.dw); float feed = c.p[0], kill = c.p[1]; int steps = (int)c.p[2];
    size_t NN = (size_t)N * N;
    std::vector<float> A(NN, 1.0f), B(NN, 0.0f), A2(NN), B2(NN);
    Rng r(c.seed);
    int blobs = std::max(10, N * N / 500);
    for (int k = 0; k < blobs; k++) {
        int cx = r.irange(0, N - 1), cy = r.irange(0, N - 1), rad = r.irange(2, 5);
        for (int j = -rad; j <= rad; j++)
            for (int i = -rad; i <= rad; i++)
                if (i * i + j * j <= rad * rad) { size_t q = (size_t)pmod(cy + j, N) * N + pmod(cx + i, N); B[q] = 1.0f; A[q] = 0.5f; }
    }
    for (int s = 0; s < steps; s++) {
        for (int y = 0; y < N; y++) {
            const float *Am = &A[(size_t)pmod(y - 1, N) * N], *A0 = &A[(size_t)y * N], *Ap = &A[(size_t)pmod(y + 1, N) * N];
            const float *Bm = &B[(size_t)pmod(y - 1, N) * N], *B0 = &B[(size_t)y * N], *Bp = &B[(size_t)pmod(y + 1, N) * N];
            for (int x = 0; x < N; x++) {
                int xm = x ? x - 1 : N - 1, xp = x + 1 < N ? x + 1 : 0;
                float la = 0.2f * (A0[xm] + A0[xp] + Am[x] + Ap[x]) + 0.05f * (Am[xm] + Am[xp] + Ap[xm] + Ap[xp]) - A0[x];
                float lb = 0.2f * (B0[xm] + B0[xp] + Bm[x] + Bp[x]) + 0.05f * (Bm[xm] + Bm[xp] + Bp[xm] + Bp[xp]) - B0[x];
                float a = A0[x], b = B0[x], ab = a * b * b;
                A2[(size_t)y * N + x] = sat(a + la - ab + feed * (1 - a));
                B2[(size_t)y * N + x] = sat(b + 0.5f * lb + ab - (kill + feed) * b);
            }
        }
        A.swap(A2); B.swap(B2);
    }
    std::vector<float> d(NN);
    for (size_t i = 0; i < NN; i++) d[i] = B[i] - 0.5f * A[i];
    F.fromDesign(d, N, N);
    o.lo = 0.005f; o.hi = 0.995f;
}
static void gAutomaton(GenOut& o, const GenCtx& c) {
    Field F(o, c); int rule = (int)c.p[0], cell = std::max(1, (int)c.p[1]); int cols = std::max(1, c.dw / cell), rows = std::max(1, c.dh / cell);
    float cw = (float)F.w / cols, ch = (float)F.h / rows;
    std::vector<uint8_t> row(cols, 0), nr(cols); Rng r(c.seed);
    for (int i = 0; i < cols; i++) row[i] = r.uni() < 0.3f ? 1 : 0;
    F.fill(lvl(0));
    for (int y = 0; y < rows; y++) {
        for (int x = 0; x < cols; x++) if (row[x]) F.rect(x * cw, y * ch, (x + 1) * cw, (y + 1) * ch, ((x + y) % 2) ? lvl(3) : lvl(2));
        for (int x = 0; x < cols; x++) { int idx = row[pmod(x - 1, cols)] << 2 | row[x] << 1 | row[pmod(x + 1, cols)]; nr[x] = (rule >> idx) & 1; }
        row.swap(nr);
    }
}
static void gLife(GenOut& o, const GenCtx& c) {
    Field F(o, c); float dens = c.p[0]; int gens = (int)c.p[1], cell = std::max(1, (int)c.p[2]);
    int cols = std::max(1, c.dw / cell), rows = std::max(1, c.dh / cell);
    float cw = (float)F.w / cols, ch = (float)F.h / rows;
    std::vector<uint8_t> g((size_t)cols * rows), g2(g), age((size_t)cols * rows, 0); Rng r(c.seed);
    for (auto& v : g) v = r.uni() < dens ? 1 : 0;
    for (int s = 0; s < gens; s++) {
        for (int y = 0; y < rows; y++) for (int x = 0; x < cols; x++) {
            int n = 0; for (int j = -1; j <= 1; j++) for (int i = -1; i <= 1; i++) if (i || j) n += g[(size_t)pmod(y + j, rows) * cols + pmod(x + i, cols)];
            size_t k = (size_t)y * cols + x; g2[k] = (g[k] && (n == 2 || n == 3)) || (!g[k] && n == 3);
            age[k] = g2[k] ? std::min(255, age[k] + 1) : 0;
        }
        g.swap(g2);
    }
    F.fill(lvl(0));
    float gap = cell > 1 ? c.px : 0;   // a one-design-pixel gap between cells
    for (int y = 0; y < rows; y++) for (int x = 0; x < cols; x++) { size_t k = (size_t)y * cols + x; if (g[k]) F.rect(x * cw, y * ch, (x + 1) * cw - gap, (y + 1) * ch - gap, age[k] > 3 ? lvl(1) : (age[k] > 1 ? lvl(2) : lvl(3))); }
}
static void gDLA(GenOut& o, const GenCtx& c) {
    // aggregation on the design grid, drawn smoothly at the output size
    Field F(o, c); int N = std::max(16, c.dw); int parts = (int)c.p[0]; float stick = c.p[1];
    std::vector<uint8_t> grid((size_t)N * N, 0); std::vector<float> age((size_t)N * N, 0);
    Rng r(c.seed);
    for (int k = 0; k < 4; k++) { int x = r.irange(0, N - 1), y = r.irange(0, N - 1); grid[(size_t)y * N + x] = 1; }
    int placed = 0;
    for (int p = 0; p < parts * 40 && placed < parts; p++) {
        int x = r.irange(0, N - 1), y = r.irange(0, N - 1);
        if (grid[(size_t)y * N + x]) continue;
        for (int st = 0; st < N * 8; st++) {
            int d = r.irange(0, 3); x = pmod(x + (d == 0) - (d == 1), N); y = pmod(y + (d == 2) - (d == 3), N);
            bool nb = grid[(size_t)y * N + pmod(x + 1, N)] || grid[(size_t)y * N + pmod(x - 1, N)] || grid[(size_t)pmod(y + 1, N) * N + x] || grid[(size_t)pmod(y - 1, N) * N + x];
            if (nb && r.uni() < stick) { grid[(size_t)y * N + x] = 1; age[(size_t)y * N + x] = (float)placed / parts; placed++; break; }
        }
    }
    std::vector<float> d((size_t)N * N), t(d.size());
    for (size_t i = 0; i < d.size(); i++) d[i] = grid[i] ? 0.4f + 0.6f * age[i] : 0.0f;
    for (int y = 0; y < N; y++)   // the old 3x3 softening, on the design grid
        for (int x = 0; x < N; x++) {
            float s = 0;
            for (int j = -1; j <= 1; j++) for (int i = -1; i <= 1; i++) s += d[(size_t)pmod(y + j, N) * N + pmod(x + i, N)];
            t[(size_t)y * N + x] = s / 9;
        }
    F.fromDesign(t, N, N);
    for (auto& v : o.f) v = std::max(0.0f, v);
    o.normalize = false;
}
static void gMandala(GenOut& o, const GenCtx& c) {
    Field F(o, c); int sym = (int)c.p[0]; float rings = c.p[1];
    F.each([&](float u, float v, int, int) {
        float x = wrapd(u) * 2, y = wrapd(v) * 2;   // centred at tile corners -> wraps
        float r = std::sqrt(x * x + y * y), th = std::atan2(y, x);
        float a = fmodp(th, TAU / sym); a = absf(a - PI_F / sym);
        float f = std::cos(r * rings * TAU) * 0.5f + 0.5f * std::cos(a * sym * 3 + r * 8);
        return f * smooth(1.2f, 0.2f, r) + 0.2f * fbm(u, v, 8, 2, 0.5f, c.seed);
    });
}
static void gTruchet(GenOut& o, const GenCtx& c) {
    Field F(o, c); int cells = (int)c.p[0]; float lw = c.p[1]; int style = (int)c.p[2];
    F.each([&](float u, float v, int, int) {
        float x = u * cells, y = v * cells; int cx = (int)x, cy = (int)y; float fx = x - cx, fy = y - cy;
        uint32_t h = hash3(cx, cy, c.seed); bool flip = h & 1;
        float d;
        if (style == 0) {   // quarter arcs
            if (flip) fx = 1 - fx;
            float d1 = absf(std::sqrt(fx * fx + fy * fy) - 0.5f), d2 = absf(std::sqrt((1 - fx) * (1 - fx) + (1 - fy) * (1 - fy)) - 0.5f);
            d = std::min(d1, d2);
        } else {            // diagonals
            d = flip ? absf(fx - fy) * 0.7071f : absf(fx + fy - 1) * 0.7071f;
        }
        float line = 1.0f - smooth(lw * 0.4f, lw * 0.6f, d);
        return 0.15f + 0.85f * line * (0.7f + 0.3f * hf(hash32(h)));
    });
    o.normalize = false;
}
static void attractor(GenOut& o, const GenCtx& c, int type) {
    // iterate the map, then fit the orbit's 0.5..99.5 percentile box to the frame
    Field F(o, c); F.fill(0); Rng r(c.seed);
    double a = c.p[0], b = c.p[1], cc = c.p[2], d = c.p[3]; double x = type == 3 ? 4.0 : 0.1, y = type == 3 ? 0.0 : 0.1;
    int n = std::max(400000, std::min(F.w * F.h, 8000000));
    std::vector<float> pts; pts.reserve((size_t)n * 2);
    for (int i = 0; i < n + 100; i++) {
        double nx, ny;
        if (type == 0) { nx = std::sin(a * y) - std::cos(b * x); ny = std::sin(cc * x) - std::cos(d * y); }                    // De Jong
        else if (type == 1) { nx = std::sin(a * y) + cc * std::cos(a * x); ny = std::sin(b * x) + d * std::cos(b * y); }        // Clifford
        else if (type == 2) { nx = y - (x < 0 ? -1 : 1) * std::sqrt(std::fabs(b * x - cc)); ny = a - x; }                       // Hopalong
        else {   // Gumowski-Mira: mu = a, alpha = b (standard conservative form)
            auto fm = [&](double v) { return a * v + 2 * (1 - a) * v * v / (1 + v * v); };
            nx = y + b * (1 - 0.05 * y * y) * y + fm(x); ny = -x + fm(nx);
        }
        if (!std::isfinite(nx) || !std::isfinite(ny) || std::fabs(nx) > 1e6 || std::fabs(ny) > 1e6) { nx = r.range(-0.5f, 0.5f); ny = r.range(-0.5f, 0.5f); }
        x = nx; y = ny;
        if (i >= 100) { pts.push_back((float)x); pts.push_back((float)y); }
    }
    size_t m = pts.size() / 2;
    std::vector<float> xs, ys;
    for (size_t i = 0; i < m; i += std::max<size_t>(1, m / 40000)) { xs.push_back(pts[i * 2]); ys.push_back(pts[i * 2 + 1]); }
    auto pct = [](std::vector<float>& v, float q) { size_t k = (size_t)(q * (v.size() - 1)); std::nth_element(v.begin(), v.begin() + k, v.end()); return v[k]; };
    float x0 = pct(xs, 0.005f), x1 = pct(xs, 0.995f), y0 = pct(ys, 0.005f), y1 = pct(ys, 0.995f);
    float sx = (x1 - x0) > 1e-9f ? (F.w * 0.86f) / (x1 - x0) : 1, sy = (y1 - y0) > 1e-9f ? (F.h * 0.86f) / (y1 - y0) : 1;
    float sc = std::min(sx, sy), mx = (x0 + x1) / 2, my = (y0 + y1) / 2;
    for (size_t i = 0; i < m; i++) F.add((int)(F.w / 2.0f + (pts[i * 2] - mx) * sc), (int)(F.h / 2.0f + (pts[i * 2 + 1] - my) * sc), 1.0f);
    for (auto& v : o.f) v = std::log(1 + v);
    F.blur(1); o.lo = 0.0f; o.hi = 0.995f; o.gamma = 0.7f;
}
static void gDeJong(GenOut& o, const GenCtx& c) { attractor(o, c, 0); }
static void gClifford(GenOut& o, const GenCtx& c) { attractor(o, c, 1); }
static void gHopalong(GenOut& o, const GenCtx& c) { attractor(o, c, 2); }
static void gMira(GenOut& o, const GenCtx& c) { attractor(o, c, 3); }
static void gFern(GenOut& o, const GenCtx& c) {
    Field F(o, c); F.fill(0); Rng r(c.seed); double x = 0, y = 0;
    int n = (int)std::min(8.0e6, c.p[0] * 1000.0 * std::max(1.0f, c.px * c.px));   // same density at any resolution
    for (int i = 0; i < n; i++) {
        float p = r.uni(); double nx, ny;
        if (p < 0.01) { nx = 0; ny = 0.16 * y; }
        else if (p < 0.86) { nx = 0.85 * x + 0.04 * y; ny = -0.04 * x + 0.85 * y + 1.6; }
        else if (p < 0.93) { nx = 0.2 * x - 0.26 * y; ny = 0.23 * x + 0.22 * y + 1.6; }
        else { nx = -0.15 * x + 0.28 * y; ny = 0.26 * x + 0.24 * y + 0.44; }
        x = nx; y = ny;
        F.add((int)(F.w / 2.0 + x * F.h * 0.09), (int)(F.h * 0.98 - y * F.h * 0.095), 1.0f);
    }
    for (auto& v : o.f) v = std::log(1 + v);
    if (c.px > 1.5f) F.blur(1);
    o.gamma = 0.6f; o.lo = 0; o.hi = 0.99f;
}
static void gKoch(GenOut& o, const GenCtx& c) {
    Field F(o, c); F.fill(0); int depth = (int)c.p[0];
    std::vector<std::pair<float, float>> pts;
    float R = F.h * 0.42f;
    for (int i = 0; i < 3; i++) pts.push_back({F.w / 2.0f + R * std::cos(-PI_F / 2 + i * TAU / 3), F.h / 2.0f + R * 0.12f + R * std::sin(-PI_F / 2 + i * TAU / 3)});
    for (int d = 0; d < depth; d++) {
        std::vector<std::pair<float, float>> np;
        for (size_t i = 0; i < pts.size(); i++) {
            auto A = pts[i], B = pts[(i + 1) % pts.size()];
            float dx = B.first - A.first, dy = B.second - A.second;
            std::pair<float, float> P{A.first + dx / 3, A.second + dy / 3}, Q{A.first + 2 * dx / 3, A.second + 2 * dy / 3};
            std::pair<float, float> M{(P.first + Q.first) / 2 + (dy) * 0.288675f, (P.second + Q.second) / 2 - (dx) * 0.288675f};
            np.push_back(A); np.push_back(P); np.push_back(M); np.push_back(Q);
        }
        pts.swap(np);
    }
    for (int k = 0; k < 3; k++) {
        std::vector<std::pair<float, float>> s;
        float f = 1 - k * 0.3f;
        for (auto& p : pts) s.push_back({F.w / 2.0f + (p.first - F.w / 2.0f) * f, F.h / 2.0f + (p.second - F.h / 2.0f) * f});
        F.polyline(s, std::max(c.px, F.h / 400.0f), 1.0f - k * 0.3f, true);
    }
    F.blur(1); o.normalize = false;
}
static void gDragon(GenOut& o, const GenCtx& c) {
    Field F(o, c); F.fill(0); int it = (int)c.p[0];
    std::vector<int> turns = {1};
    for (int i = 1; i < it; i++) { std::vector<int> t = turns; t.push_back(1); for (int k = (int)turns.size() - 1; k >= 0; k--) t.push_back(-turns[k]); turns.swap(t); }
    float step = F.h * 0.9f / std::pow(2.0f, it / 2.0f) * 1.2f;
    float x = F.w * 0.42f, y = F.h * 0.33f; int dir = 0; const int dx[4] = {1, 0, -1, 0}, dy[4] = {0, 1, 0, -1};
    std::vector<std::pair<float, float>> pts{{x, y}};
    for (size_t i = 0; i <= turns.size(); i++) { x += dx[dir] * step; y += dy[dir] * step; pts.push_back({x, y}); if (i < turns.size()) dir = pmod(dir + turns[i], 4); }
    // centre it
    float mnx = 1e9, mxx = -1e9, mny = 1e9, mxy = -1e9; for (auto& p : pts) { mnx = std::min(mnx, p.first); mxx = std::max(mxx, p.first); mny = std::min(mny, p.second); mxy = std::max(mxy, p.second); }
    float ox = F.w / 2.0f - (mnx + mxx) / 2, oy = F.h / 2.0f - (mny + mxy) / 2;
    for (auto& p : pts) { p.first += ox; p.second += oy; }
    for (size_t i = 0; i + 1 < pts.size(); i++) F.line(pts[i].first, pts[i].second, pts[i + 1].first, pts[i + 1].second, std::max(c.px, step * 0.3f), 0.4f + 0.6f * (float)i / pts.size());
    o.normalize = false;
}
static void gVoronoi(GenOut& o, const GenCtx& c) {
    Field F(o, c); int n = (int)c.p[0]; float edge = c.p[1]; int mode = (int)c.p[2];
    F.each([&](float u, float v, int, int) {
        Worley w = worley(u, v, n, c.seed, 1.0f);
        float e = smooth(0, edge * 0.1f, w.f2 - w.f1);
        float cellv = mode == 0 ? hf(w.id) : (mode == 1 ? 1.0f - sat(w.f1 * 1.6f) : 0.5f);
        return (0.25f + 0.75f * cellv) * e;
    });
    o.lo = 0; o.hi = 1; o.normalize = false;
}
static void gNeighborWeb(GenOut& o, const GenCtx& c) {
    Field F(o, c); F.fill(0); Rng r(c.seed); int n = (int)c.p[0], links = (int)c.p[1];
    std::vector<std::pair<float, float>> pts;
    for (int i = 0; i < n; i++) pts.push_back({r.uni(), r.uni()});
    for (int i = 0; i < n; i++) {
        std::vector<std::pair<float, int>> ds;
        for (int j = 0; j < n; j++) if (j != i) { float dx = wrapd(pts[j].first - pts[i].first), dy = wrapd(pts[j].second - pts[i].second); ds.push_back({dx * dx + dy * dy, j}); }
        std::sort(ds.begin(), ds.end());
        for (int k = 0; k < links && k < (int)ds.size(); k++) {
            int j = ds[k].second; float dx = wrapd(pts[j].first - pts[i].first), dy = wrapd(pts[j].second - pts[i].second);
            F.line(pts[i].first * F.w, pts[i].second * F.h, (pts[i].first + dx) * F.w, (pts[i].second + dy) * F.h, std::max(c.px, F.w / 256.0f), 0.55f);
        }
    }
    for (auto& p : pts) F.disc(p.first * F.w, p.second * F.h, std::max(2.0f * c.px, F.w / 90.0f), 1.0f, true);
    o.normalize = false;
}
static void gFlowField(GenOut& o, const GenCtx& c) {
    Field F(o, c); F.fill(0); Rng r(c.seed); int n = (int)c.p[0]; int len = (int)c.p[1];
    for (int i = 0; i < n; i++) {
        float x = r.uni() * F.w, y = r.uni() * F.h; float br = 0.4f + 0.6f * r.uni();
        for (int s = 0; s < len; s++) {
            float ang = fbm(fmodp(x / F.w, 1.0f), fmodp(y / F.h, 1.0f), 3, 3, 0.5f, c.seed) * TAU * 1.5f;
            float nx = x + std::cos(ang) * 1.5f * c.px, ny = y + std::sin(ang) * 1.5f * c.px;
            F.line(x, y, nx, ny, 1.2f * c.px, br * (1 - (float)s / len) + 0.2f);
            x = nx; y = ny;
        }
    }
    F.blur(1); o.normalize = false; o.lo = 0; o.hi = 1;
}
// Periodic hexagonal lattice on the unit tile: n columns, rows forced to an integer so the
// lattice repeats exactly at the tile edge. Returns hex "radius" distance (0 centre .. 0.5 edge)
// and a cell id that is identical across the seam.
static inline float hexCell(float u, float v, int n, uint32_t seed, uint32_t& id) {
    int m = std::max(1, (int)std::lround(n * 1.1547f));
    float x = u * n, y = v * m, best = 9;
    id = 0;
    for (int k = 0; k < 2; k++) {
        float o2 = k * 0.5f;
        float cx = std::floor(x - o2) + o2 + 0.5f, cy = std::floor(y - o2) + o2 + 0.5f;
        float dx = x - cx, dy = y - cy;
        float d = std::max(absf(dx) * 0.866f + absf(dy) * 0.5f, absf(dy));
        if (d < best) { best = d; id = hash3(pmod((int)std::floor(cx), n), pmod((int)std::floor(cy), m), seed + k * 7919U); }
    }
    return best;
}
static void gHexPulse(GenOut& o, const GenCtx& c) {
    Field F(o, c); int n = (int)c.p[0]; float pulse = std::round(c.p[1]);
    F.each([&](float u, float v, int, int) {
        uint32_t id; float best = hexCell(u, v, n, c.seed, id);
        float ph = hf(id) * TAU;
        float fillv = 0.5f + 0.5f * std::sin(ph + pulse * (u + v) * TAU);
        return fillv * (1 - smooth(0.42f, 0.5f, best)) + 0.05f;
    });
    o.normalize = false;
}
static void gQuasicrystal(GenOut& o, const GenCtx& c) {
    Field F(o, c); int fold = (int)c.p[0]; float fr = c.p[1];
    F.each([&](float u, float v, int, int) {
        float s = 0;
        for (int k = 0; k < fold; k++) { float a = k * PI_F / fold; int kx = (int)std::lround(std::cos(a) * fr), ky = (int)std::lround(std::sin(a) * fr); s += std::cos(TAU * (kx * u + ky * v)); }
        return s / fold;
    });
}
static void gDataStream(GenOut& o, const GenCtx& c) {
    Field F(o, c); int cwd = std::max(2, (int)c.p[0]); float dens = c.p[1];
    int cols = std::max(1, c.dw / cwd), rows = std::max(1, c.dh / cwd);
    float cw = (float)F.w / cols, ch = (float)F.h / rows, g = c.px;
    F.fill(lvl(0)); Rng r(c.seed);
    for (int x = 0; x < cols; x++) {
        int y = r.irange(0, rows - 1), len = r.irange(rows / 4, rows);
        if (r.uni() > dens) continue;
        for (int k = 0; k < len; k++) { int yy = pmod(y + k, rows); float v = k == len - 1 ? lvl(3) : (k > len - 5 ? lvl(2) : lvl(1)); if (r.uni() < 0.85f) F.rect(x * cw + g, yy * ch + g, (x + 1) * cw - g, (yy + 1) * ch - g, v); }
    }
}

// ---------------------------------------------------------------------------------------------
// EARLY CGI / WORKSTATION (20)
// ---------------------------------------------------------------------------------------------
static void gWireTerrain(GenOut& o, const GenCtx& c) {
    Field F(o, c); F.fill(0); int grid = (int)c.p[0]; float hgt = c.p[1], hor = c.p[2];
    auto H = [&](int i, int j) { return fbm(i / (float)grid, j / (float)grid * 0.5f, 3, 4, 0.5f, c.seed) * hgt; };
    auto P = [&](int i, int j, float& px, float& py) {
        float x = (i / (float)grid - 0.5f) * 2.4f, z = 0.3f + j / (float)grid * 3.0f, y = -0.35f + H(i, j);
        float d = 1.0f / z; px = F.w / 2.0f + x * d * F.h * 0.9f; py = F.h * hor - y * d * F.h * 0.9f;
    };
    for (int j = grid; j >= 0; j--) for (int i = 0; i < grid; i++) {
        float ax, ay, bx, by; P(i, j, ax, ay); P(i + 1, j, bx, by); float br = 0.35f + 0.65f * (1 - j / (float)grid);
        F.line(ax, ay, bx, by, std::max(c.px, F.h / 600.0f), br);
        if (j < grid) { P(i, j + 1, bx, by); F.line(ax, ay, bx, by, std::max(c.px, F.h / 600.0f), br); }
    }
    o.normalize = false;
}
static void gCheckerHorizon(GenOut& o, const GenCtx& c) {
    Field F(o, c); float size = c.p[0], hor = c.p[1];
    F.each([&](float u, float v, int, int) {
        float y = v - hor;
        if (y <= 0.002f) return 0.35f + 0.5f * sat(-y * 3.0f) * 0.6f;   // sky gradient
        float z = 0.3f / y, x = (u - 0.5f) * c.aspect * z;
        int ck = ((int)std::floor(x * size) + (int)std::floor(z * size)) & 1;
        float fog = sat(y * 6.0f);
        return lerpf(0.45f, ck ? 0.95f : 0.08f, fog);
    });
    o.normalize = false;
}
static void gPlasmaTunnel(GenOut& o, const GenCtx& c) {
    Field F(o, c); float twist = c.p[0], rings = c.p[1];
    F.each([&](float u, float v, int, int) {
        FULLXY float r = std::sqrt(X * X + Y * Y) + 1e-4f, th = std::atan2(Y, X);
        float d = 0.25f / r;
        return 0.5f + 0.5f * std::sin(d * rings + th * twist) * 0.6f + 0.4f * std::sin(th * 3 + d * 2) * sat(r * 3);
    });
}
static void gRotozoom(GenOut& o, const GenCtx& c) {
    // rotated checkerboard built on integer wave vectors (kx,ky) so it repeats exactly at the tile edge
    Field F(o, c); float ang = c.p[0] * PI_F / 180; int zoom = std::max(1, (int)std::lround(c.p[1])); int cells = (int)c.p[2];
    int kx = (int)std::lround(std::cos(ang) * cells), ky = (int)std::lround(std::sin(ang) * cells);
    if (!kx && !ky) kx = 1;
    F.each([&](float u, float v, int, int) {
        float a = (kx * u + ky * v) * zoom, b = (-ky * u + kx * v) * zoom;
        int ck = ((int)std::floor(a) + (int)std::floor(b)) & 1;
        float fa = a - std::floor(a), fb = b - std::floor(b);
        float edge = std::min(std::min(fa, 1 - fa), std::min(fb, 1 - fb));
        return (ck ? 0.85f : 0.2f) * (0.8f + 0.2f * smooth(0, 0.08f, edge));
    });
    o.normalize = false;
}
static void gStarfield(GenOut& o, const GenCtx& c) {
    Field F(o, c); F.fill(0); Rng r(c.seed); int n = (int)(c.p[0] * (float)c.dw * c.dh / 2500.0f); float streak = c.p[1];
    float unit = F.h / 1080.0f;
    for (int i = 0; i < n; i++) {
        float x = r.uni() * F.w, y = r.uni() * F.h, br = 0.4f + 0.6f * r.uni() * r.uni();
        float dx = x - F.w / 2.0f, dy = y - F.h / 2.0f, dl = std::sqrt(dx * dx + dy * dy) + 1e-3f;
        float len = dl * streak * 0.18f * br;
        F.line(x, y, x + dx / dl * len, y + dy / dl * len, std::max(1.0f, (br > 0.85f ? 2.2f : 1.2f) * unit * 1.5f), br);
        if (br > 0.8f) F.disc(x, y, std::max(1.2f, 2.0f * unit), br, true);
    }
    // a faint core glow
    float rad = F.h * 0.25f;
    F.each([&](float u, float v, int x, int y) { float dx = x - F.w / 2.0f, dy = y - F.h / 2.0f; float g = std::exp(-(dx * dx + dy * dy) / (rad * rad)) * 0.18f; return std::max(o.f[(size_t)y * F.w + x], g); });
    o.normalize = false;
}
static void gVectorTunnel(GenOut& o, const GenCtx& c) {
    Field F(o, c); F.fill(0); int seg = (int)c.p[0]; int depth = (int)c.p[1];
    for (int d = 0; d < depth; d++) {
        float z = 1.0f + d * 0.6f, sc = F.h * 0.7f / z, br = 1 - d / (float)depth;
        std::vector<std::pair<float, float>> pts;
        float rot = d * 0.08f;
        for (int i = 0; i < seg; i++) { float a = i * TAU / seg + rot; pts.push_back({F.w / 2.0f + sc * std::cos(a) + d * F.w * 0.01f, F.h / 2.0f + sc * std::sin(a) - d * F.h * 0.01f}); }
        F.polyline(pts, std::max(c.px, F.h / 350.0f), br, true);
        if (d + 1 < depth) {
            float z2 = 1.0f + (d + 1) * 0.6f, sc2 = F.h * 0.7f / z2;
            for (int i = 0; i < seg; i++) { float a = i * TAU / seg + rot, a2 = a + 0.08f; F.line(pts[i].first, pts[i].second, F.w / 2.0f + sc2 * std::cos(a2) + (d + 1) * F.w * 0.01f, F.h / 2.0f + sc2 * std::sin(a2) - (d + 1) * F.h * 0.01f, std::max(c.px, F.h / 500.0f), br * 0.6f); }
        }
    }
    F.blur(1); o.normalize = false;
}
static void gLensFlare(GenOut& o, const GenCtx& c) {
    Field F(o, c); float lx = c.p[0], ly = c.p[1], size = c.p[2];
    F.each([&](float u, float v, int, int) {
        float a = c.aspect, dx = (u - lx) * a, dy = v - ly, d = std::sqrt(dx * dx + dy * dy);
        float core = std::exp(-d * d / (size * size * 0.05f)) + 0.35f * std::exp(-d / (size * 0.9f));
        float th = std::atan2(dy, dx); float rays = std::pow(absf(std::cos(th * 6)), 30.0f) * std::exp(-d / (size * 1.6f)) * 0.5f;
        float ghosts = 0;
        for (int k = 1; k <= 5; k++) { float t = -0.3f * k + 0.2f; float gx = (0.5f - lx) * a * (1 + t) + lx * a, gy = (0.5f - ly) * (1 + t) + ly; float gd = std::sqrt((u * a - gx) * (u * a - gx) + (v - gy) * (v - gy)); ghosts += 0.18f * smooth(size * 0.12f * k, size * 0.1f * k, gd) * (0.5f + 0.5f * smooth(size * 0.07f * k, size * 0.1f * k, gd)); }
        float bg = 0.08f + 0.1f * (1 - v);
        return sat(bg + core + rays + ghosts);
    });
    o.normalize = false;
}
static void gGouraud(GenOut& o, const GenCtx& c) {
    Field F(o, c); int cells = (int)c.p[0]; float jit = c.p[1]; Rng r(c.seed);
    std::vector<float> px((cells + 1) * (cells + 1)), py(px.size()), pz(px.size());
    for (int j = 0; j <= cells; j++) for (int i = 0; i <= cells; i++) { int k = j * (cells + 1) + i; uint32_t h = hash3(pmod(i, cells), pmod(j, cells), c.seed); px[k] = (i + (hf(h) - 0.5f) * jit) / cells; py[k] = (j + (hf(hash32(h)) - 0.5f) * jit) / cells; pz[k] = hf(hash32(h ^ 77)); }
    F.fill(0.3f);
    for (int j = 0; j < cells; j++) for (int i = 0; i < cells; i++) {
        int a = j * (cells + 1) + i, b = a + 1, d = a + cells + 1, e = d + 1;
        auto shade = [&](int p, int q, int s) { float nx = (py[q] - py[p]) * (pz[s] - pz[p]) - (pz[q] - pz[p]) * (py[s] - py[p]); float ny = (pz[q] - pz[p]) * (px[s] - px[p]) - (px[q] - px[p]) * (pz[s] - pz[p]); float nz = (px[q] - px[p]) * (py[s] - py[p]) - (py[q] - py[p]) * (px[s] - px[p]); float l = std::sqrt(nx * nx + ny * ny + nz * nz) + 1e-6f; return sat(0.5f + 0.5f * (nx * -0.5f + ny * -0.6f + nz * 0.62f) / l); };
        bool diag = hash3(i, j, c.seed + 5) & 1;
        if (diag) { F.tri(px[a] * F.w, py[a] * F.h, px[b] * F.w, py[b] * F.h, px[e] * F.w, py[e] * F.h, shade(a, b, e)); F.tri(px[a] * F.w, py[a] * F.h, px[e] * F.w, py[e] * F.h, px[d] * F.w, py[d] * F.h, shade(a, e, d)); }
        else { F.tri(px[a] * F.w, py[a] * F.h, px[b] * F.w, py[b] * F.h, px[d] * F.w, py[d] * F.h, shade(a, b, d)); F.tri(px[b] * F.w, py[b] * F.h, px[e] * F.w, py[e] * F.h, px[d] * F.w, py[d] * F.h, shade(b, e, d)); }
    }
    o.normalize = false;
}
static void gVoxelHills(GenOut& o, const GenCtx& c) {
    // Comanche-style voxel-space renderer: front-to-back columns with an occlusion buffer,
    // slope lighting and distance haze; the camera sits low so peaks break the horizon
    Field F(o, c); float hgt = c.p[0]; int det = (int)c.p[1];
    const float horizon = 0.46f, skyLo = 0.66f;
    int W = F.w, H = F.h; float* out = o.f.data();
    for (int y = 0; y < H; y++) {
        float t = y / (H * horizon);
        float v = t < 1 ? 1.0f - 0.34f * t : skyLo;
        for (int x = 0; x < W; x++) out[(size_t)y * W + x] = v;
    }
    float camH = hgt * 1.05f;
    auto height = [&](float wx, float z) {
        float h = sat(fbm(fmodp(wx * 0.30f + 0.5f, 1.0f), fmodp(z * 0.30f, 1.0f), 3, det, 0.5f, c.seed) * 1.7f + 0.45f);
        return h * std::sqrt(h) * hgt * 2.4f;
    };
    ParallelFor(W, [&](int x) {
        float u = (x + 0.5f) / W; int top = H;
        for (int zi = 1; zi <= 520; zi++) {
            float z = 0.02f + zi * 0.0072f;
            float wx = (u - 0.5f) * c.aspect * z * 1.4f;
            float wh = height(wx, z);
            int sy = (int)(H * horizon + (camH - wh) / z * H * 0.30f);
            if (sy < top) {
                float behind = height(wx, z + 0.03f);
                float light = sat(0.5f + (wh - behind) / (hgt + 1e-3f) * 6.0f);   // faces turned to the viewer are lit
                float val = 0.04f + 0.30f * (wh / (hgt * 2.4f)) + 0.36f * light;
                val = lerpf(val, skyLo, smooth(0.15f, 1.0f, z / 3.8f));    // haze towards the horizon
                for (int y = std::max(0, sy); y < top; y++) out[(size_t)y * W + x] = val;
                top = std::max(0, sy);
                if (top == 0) break;
            }
        }
    });
    o.normalize = false;
}
static void gWireSphere(GenOut& o, const GenCtx& c) {
    Field F(o, c); F.fill(0); int lat = (int)c.p[0], lon = (int)c.p[1]; float R = c.p[2] * F.h;
    float tilt = 0.4f;
    auto P = [&](float th, float ph, float& px, float& py, float& z) { float x = std::cos(th) * std::cos(ph), y = std::sin(ph), zz = std::sin(th) * std::cos(ph); float y2 = y * std::cos(tilt) - zz * std::sin(tilt); z = y * std::sin(tilt) + zz * std::cos(tilt); px = F.w / 2.0f + x * R; py = F.h / 2.0f - y2 * R; };
    for (int i = 1; i < lat; i++) { float ph = -PI_F / 2 + PI_F * i / lat; for (int k = 0; k < 120; k++) { float a = k * TAU / 120, b = (k + 1) * TAU / 120; float ax, ay, az, bx, by, bz; P(a, ph, ax, ay, az); P(b, ph, bx, by, bz); F.line(ax, ay, bx, by, std::max(c.px, F.h / 500.0f), 0.3f + 0.7f * sat(az + 0.6f)); } }
    for (int i = 0; i < lon; i++) { float th = i * TAU / lon; for (int k = 0; k < 60; k++) { float a = -PI_F / 2 + PI_F * k / 60, b = -PI_F / 2 + PI_F * (k + 1) / 60; float ax, ay, az, bx, by, bz; P(th, a, ax, ay, az); P(th, b, bx, by, bz); F.line(ax, ay, bx, by, std::max(c.px, F.h / 500.0f), 0.3f + 0.7f * sat(az + 0.6f)); } }
    F.blur(1); o.normalize = false;
}
static void gRibbons(GenOut& o, const GenCtx& c) {
    Field F(o, c); F.fill(0.05f); Rng r(c.seed); int n = (int)c.p[0]; float wd = c.p[1] * F.h;
    for (int k = 0; k < n; k++) {
        float p0x = -0.1f * F.w, p0y = r.uni() * F.h, p1x = F.w * 0.33f, p1y = r.uni() * F.h, p2x = F.w * 0.66f, p2y = r.uni() * F.h, p3x = F.w * 1.1f, p3y = r.uni() * F.h;
        float base = 0.35f + 0.6f * (k + 1.0f) / n;
        for (int i = 0; i < 400; i++) {
            float t = i / 400.0f, t2 = (i + 1) / 400.0f;
            auto bez = [&](float t, float& x, float& y) { float mt = 1 - t; x = mt * mt * mt * p0x + 3 * mt * mt * t * p1x + 3 * mt * t * t * p2x + t * t * t * p3x; y = mt * mt * mt * p0y + 3 * mt * mt * t * p1y + 3 * mt * t * t * p2y + t * t * t * p3y; };
            float x, y, x2, y2; bez(t, x, y); bez(t2, x2, y2);
            float shade = base * (0.6f + 0.4f * std::sin(t * 9 + k));
            F.line(x, y, x2, y2, wd * (0.6f + 0.4f * std::sin(t * 5)), shade);
        }
    }
    o.normalize = false;
}
static void gPipeMaze(GenOut& o, const GenCtx& c) {
    Field F(o, c); int cells = (int)c.p[0]; float wd = c.p[1];
    F.each([&](float u, float v, int, int) {
        float x = u * cells, y = v * cells; int cx = (int)x, cy = (int)y; float fx = x - cx - 0.5f, fy = y - cy - 0.5f;
        uint32_t h = hash3(cx, cy, c.seed);
        // connections: right/down decided per cell edge (shared with neighbour via hash of edge)
        bool R = hash3(cx, cy, c.seed + 1) & 1, D = hash3(cx, cy, c.seed + 2) & 1, L = hash3(pmod(cx - 1, cells), cy, c.seed + 1) & 1, U = hash3(cx, pmod(cy - 1, cells), c.seed + 2) & 1;
        float d = 9;
        if (R) d = std::min(d, segDist(fx, fy, 0, 0, 0.6f, 0)); if (L) d = std::min(d, segDist(fx, fy, 0, 0, -0.6f, 0));
        if (D) d = std::min(d, segDist(fx, fy, 0, 0, 0, 0.6f)); if (U) d = std::min(d, segDist(fx, fy, 0, 0, 0, -0.6f));
        if (!(R || L || U || D)) d = std::sqrt(fx * fx + fy * fy);
        float t = d / wd; if (t > 1) return 0.08f;
        float tube = std::sqrt(1 - t * t);   // cylindrical shading
        return 0.25f + 0.75f * tube * (0.6f + 0.4f * hf(h));
    });
    o.normalize = false;
}
static void gFractalTree(GenOut& o, const GenCtx& c) {
    Field F(o, c); F.fill(0); int depth = (int)c.p[0]; float spread = c.p[1] * PI_F / 180; Rng r(c.seed);
    std::function<void(float, float, float, float, int)> br = [&](float x, float y, float ang, float len, int d) {
        if (d == 0 || len < c.px) return;
        float nx = x + std::cos(ang) * len, ny = y + std::sin(ang) * len;
        F.line(x, y, nx, ny, std::max(c.px, d * F.h / 900.0f), 0.3f + 0.7f * (1 - d / (float)depth));
        float j = (r.uni() - 0.5f) * 0.3f;
        br(nx, ny, ang - spread + j, len * 0.72f, d - 1); br(nx, ny, ang + spread + j, len * 0.72f, d - 1);
        if (d % 3 == 0) br(nx, ny, ang + j, len * 0.6f, d - 1);
    };
    br(F.w / 2.0f, F.h * 0.98f, -PI_F / 2, F.h * 0.25f, depth);
    o.normalize = false;
}
static void gTorusKnot(GenOut& o, const GenCtx& c) {
    Field F(o, c); F.fill(0); int p = (int)c.p[0], q = (int)c.p[1]; float th = c.p[2] * F.h;
    std::vector<std::tuple<float, float, float>> pts;
    for (int i = 0; i <= 3000; i++) { float t = i / 3000.0f * TAU; float r = 2 + std::cos(q * t); float x = r * std::cos(p * t), y = r * std::sin(p * t), z = -std::sin(q * t); float y2 = y * 0.8f - z * 0.6f, z2 = y * 0.6f + z * 0.8f; pts.push_back({F.w / 2.0f + x * F.h * 0.14f, F.h / 2.0f + y2 * F.h * 0.14f, z2}); }
    std::vector<int> order(pts.size() - 1); for (size_t i = 0; i < order.size(); i++) order[i] = (int)i;
    std::sort(order.begin(), order.end(), [&](int a, int b) { return std::get<2>(pts[a]) < std::get<2>(pts[b]); });
    for (int i : order) { auto& A = pts[i]; auto& B = pts[i + 1]; float z = std::get<2>(A); F.line(std::get<0>(A), std::get<1>(A), std::get<0>(B), std::get<1>(B), th * (0.7f + 0.3f * z), 0.35f + 0.65f * sat(0.5f + z * 0.5f)); }
    o.normalize = false;
}
static void gRaytraceBalls(GenOut& o, const GenCtx& c) {
    Field F(o, c); int n = (int)c.p[0]; Rng r(c.seed);
    struct S { float x, z, rad; }; std::vector<S> ss; for (int i = 0; i < n; i++) ss.push_back({(r.uni() - 0.5f) * 3.0f, 2.0f + r.uni() * 4.0f, 0.3f + 0.3f * r.uni()});
    F.each([&](float u, float v, int, int) {
        float dx = (u - 0.5f) * c.aspect, dy = -(v - 0.5f), dz = 1.0f; float l = std::sqrt(dx * dx + dy * dy + 1); dx /= l; dy /= l; dz /= l;
        float best = 1e9; float val = -1;
        for (auto& s : ss) { float cx = s.x, cy = -0.6f + s.rad, cz = s.z; float ocx = -cx, ocy = 0.1f - cy, ocz = -cz; float b = ocx * dx + ocy * dy + ocz * dz, cc = ocx * ocx + ocy * ocy + ocz * ocz - s.rad * s.rad; float disc = b * b - cc; if (disc < 0) continue; float t = -b - std::sqrt(disc); if (t > 0 && t < best) { best = t; float px = dx * t, py = 0.1f + dy * t, pz = dz * t; float nx = (px - cx) / s.rad, ny = (py - cy) / s.rad, nz = (pz - cz) / s.rad; float diff = sat(nx * -0.5f + ny * 0.7f + nz * -0.5f); float spec = std::pow(sat(nx * -0.4f + ny * 0.8f + nz * -0.45f), 40.0f); val = 0.2f + 0.6f * diff + spec; } }
        if (val >= 0) return sat(val);
        if (dy < -0.001f) { float t = (-0.6f - 0.1f) / dy; float px = dx * t, pz = dz * t; if (t < best) { int ck = ((int)std::floor(px) + (int)std::floor(pz)) & 1; float sh = 1.0f; for (auto& s : ss) { float ddx = px - s.x, ddz = pz - s.z; if (std::sqrt(ddx * ddx + ddz * ddz) < s.rad * 1.1f) sh = 0.5f; } return sat((ck ? 0.9f : 0.25f) * sh * sat(1.2f - t * 0.08f)); } }
        return 0.5f + 0.3f * sat(dy * 2);
    });
    o.normalize = false;
}

// ---------------------------------------------------------------------------------------------
// NEON 90s (20) - Memphis shapes, squiggles, zigzags, confetti, synthwave
// ---------------------------------------------------------------------------------------------
static void gMemphis(GenOut& o, const GenCtx& c) {
    Field F(o, c); F.fill(lvl(0)); Rng r(c.seed); int n = (int)c.p[0]; float st = c.p[1] * F.w / 128;
    for (int i = 0; i < n; i++) {
        int kind = r.irange(0, 3); float x = r.uni() * F.w, y = r.uni() * F.h, lv = lvl(r.irange(1, 3));
        if (kind == 0) {   // squiggle
            std::vector<std::pair<float, float>> pts; float ang = r.uni() * TAU, len = F.w * (0.12f + 0.15f * r.uni());
            for (int k = 0; k <= 30; k++) { float t = k / 30.0f; float px = x + std::cos(ang) * len * t - std::sin(ang) * std::sin(t * TAU * 2) * len * 0.12f, py = y + std::sin(ang) * len * t + std::cos(ang) * std::sin(t * TAU * 2) * len * 0.12f; pts.push_back({px, py}); }
            F.polyline(pts, st, lv);
        } else if (kind == 1) { float s = F.w * (0.03f + 0.05f * r.uni()); float a = r.uni() * TAU; F.tri(x + s * std::cos(a), y + s * std::sin(a), x + s * std::cos(a + 2.1f), y + s * std::sin(a + 2.1f), x + s * std::cos(a + 4.2f), y + s * std::sin(a + 4.2f), lv); }
        else if (kind == 2) { F.disc(x, y, F.w * (0.01f + 0.02f * r.uni()), lv); }
        else { float a = r.uni() * TAU, len = F.w * 0.1f; for (int k = -2; k <= 2; k++) F.line(x + std::cos(a + PI_F / 2) * k * st * 2, y + std::sin(a + PI_F / 2) * k * st * 2, x + std::cos(a + PI_F / 2) * k * st * 2 + std::cos(a) * len, y + std::sin(a + PI_F / 2) * k * st * 2 + std::sin(a) * len, st * 0.7f, lv); }
    }
    o.normalize = false;
}
static void gZigzag(GenOut& o, const GenCtx& c) {
    Field F(o, c); float amp = c.p[0]; int pitch = (int)c.p[1]; float th = c.p[2];
    F.each([&](float u, float v, int, int) {
        float x = u * pitch, y = v * pitch * 1.0f;
        float z = absf(fmodp(x, 1.0f) * 2 - 1) * amp;   // triangle wave
        float d = fmodp(y - z, 1.0f);
        int band = (int)std::floor((y - z) * 1.0f);
        float line = d < th ? 1.0f : 0.0f;
        return line > 0 ? lvl(1 + pmod(band, 3)) : lvl(0);
    });
    o.normalize = false;
}
static void gConfetti(GenOut& o, const GenCtx& c) {
    Field F(o, c); F.fill(lvl(0)); Rng r(c.seed); int n = (int)c.p[0]; float sz = c.p[1] * F.w / 100;
    for (int i = 0; i < n; i++) { float x = r.uni() * F.w, y = r.uni() * F.h, s = sz * (0.5f + r.uni()), a = r.uni() * TAU; float lv = lvl(r.irange(1, 3)); F.tri(x + s * std::cos(a), y + s * std::sin(a), x + s * std::cos(a + 2.094f), y + s * std::sin(a + 2.094f), x + s * std::cos(a + 4.189f), y + s * std::sin(a + 4.189f), lv); }
    o.normalize = false;
}
static void gSynthGrid(GenOut& o, const GenCtx& c) {
    Field F(o, c); float sp = c.p[0], off = c.p[1], hor = c.p[2];
    F.each([&](float u, float v, int, int) {
        float y = v - hor;
        if (y <= 0) {   // sky with sun
            float dx = (u - 0.5f) * c.aspect, dy = (v - hor + 0.22f); float d = std::sqrt(dx * dx + dy * dy);
            float sun = smooth(0.2f, 0.19f, d) * (fmodp(v * 40, 1.0f) < 0.6f + v ? 1.0f : 0.0f);
            return 0.1f + 0.25f * (1 + y * 3) * 0.5f + sun * 0.85f;
        }
        float z = 0.25f / (y + 0.01f), x = (u - 0.5f) * c.aspect * z;
        float gx = absf(fmodp(x * sp, 1.0f) - 0.5f), gz = absf(fmodp(z * sp + off, 1.0f) - 0.5f);
        float lw = 0.03f * z;
        float line = std::max(smooth(lw, 0, 0.5f - gx) , smooth(lw, 0, 0.5f - gz)); line = 0;
        float lx = 1 - smooth(0.0f, 0.02f * (1 + z), 0.5f - gx), lz = 1 - smooth(0.0f, 0.02f * (1 + z * 0.5f), 0.5f - gz);
        line = std::max(lx, lz) * sat(1.5f - z * 0.08f);
        return 0.05f + 0.95f * line;
    });
    o.normalize = false;
}
static void gLaserBeams(GenOut& o, const GenCtx& c) {
    Field F(o, c); F.fill(0); Rng r(c.seed); int n = (int)c.p[0]; float wd = c.p[1] * F.w / 128;
    for (int i = 0; i < n; i++) {
        float a = r.uni() * TAU; int kx = r.irange(-2, 2), ky = r.irange(-2, 2); if (!kx && !ky) kx = 1; (void)a;
        float x0 = r.uni() * F.w, y0 = r.uni() * F.h; float br = 0.5f + 0.5f * r.uni();
        // a periodic straight line: direction (kx,ky) in tile units closes after lcm steps -> draw across several wraps
        float len = std::sqrt((float)(kx * kx + ky * ky));
        float dx = kx / len, dy = ky / len; float L = F.w * len;
        F.line(x0, y0, x0 + dx * L, y0 + dy * L, wd, br);
        F.line(x0, y0, x0 + dx * L, y0 + dy * L, wd * 3, br * 0.25f);
    }
    F.blur(1); o.normalize = false; o.lo = 0; o.hi = 1;
}
static void gSplatter(GenOut& o, const GenCtx& c) {
    Field F(o, c); F.fill(lvl(0)); Rng r(c.seed); int n = (int)c.p[0]; float mx = c.p[1] * F.w / 100;
    for (int i = 0; i < n; i++) { float x = r.uni() * F.w, y = r.uni() * F.h, rr = mx * std::pow(r.uni(), 2.0f) + c.px; F.disc(x, y, rr, lvl(r.irange(1, 3))); }
    o.normalize = false;
}
static void gJazz(GenOut& o, const GenCtx& c) {
    // dry-brush bands (colour 2) with a thin zig-zag squiggle (colour 3) riding along each one
    Field F(o, c); F.fill(lvl(0)); Rng r(c.seed); int n = (int)c.p[0]; float wd = c.p[1] * F.w / 100;
    for (int i = 0; i < n; i++) {
        float cx = r.uni() * F.w, cy = r.uni() * F.h, ang = -0.55f + r.uni() * 0.5f, L = F.w * (0.45f + 0.3f * r.uni());
        float dx = std::cos(ang), dy = std::sin(ang); uint32_t sd = r.next();
        int R = (int)(L / 2 + wd + 2);
        for (int yy = -R; yy <= R; yy++)
            for (int xx = -R; xx <= R; xx++) {
                float px = xx + 0.5f, py = yy + 0.5f;
                float s = px * dx + py * dy, q = -px * dy + py * dx;      // along / across
                float t = s / L + 0.5f;
                if (t < 0 || t > 1) continue;
                float half = wd * 0.5f * (0.55f + 0.45f * std::sin(t * PI_F)) * (1.0f + 0.12f * std::sin(t * 23 + i));
                if (std::fabs(q) > half) continue;
                // dry brush: bristle streaks open up towards the tail
                float bristle = hf(hash3((int)((q / wd + 1) * 40), 0, sd));
                float dry = smooth(0.55f, 1.0f, t) * 0.9f + 0.08f;
                if (bristle < dry) continue;
                F.set((int)std::floor(cx + px), (int)std::floor(cy + py), lvl(1));
            }
        std::vector<std::pair<float, float>> pts;
        float amp = wd * 0.55f; int zz = 9 + r.irange(0, 4);
        for (int k = 0; k <= zz; k++) {
            float t = k / (float)zz, s = (t - 0.5f) * L * 0.9f, q = (k & 1 ? amp : -amp) * 0.8f + wd * 0.15f;
            pts.push_back({cx + s * dx - q * dy, cy + s * dy + q * dx});
        }
        F.polyline(pts, std::max(1.5f * c.px, wd * 0.11f), lvl(2));
    }
    // sprinkle of small dark ticks
    for (int i = 0; i < n * 3; i++) { float x = r.uni() * F.w, y = r.uni() * F.h, a = r.uni() * TAU, l = wd * 0.35f; F.line(x, y, x + std::cos(a) * l, y + std::sin(a) * l, std::max(1.2f * c.px, wd * 0.07f), lvl(3)); }
    // discrete: plot() keeps the max level, so draw order above maps to slot priority
    o.normalize = false;
}
static void gBlocks(GenOut& o, const GenCtx& c) {
    Field F(o, c); F.fill(lvl(0)); Rng r(c.seed); int n = (int)c.p[0]; float sz = c.p[1] * F.w / 100;
    for (int i = 0; i < n; i++) { float x = r.uni() * F.w, y = r.uni() * F.h, w = sz * (0.3f + r.uni()), h = sz * (0.3f + r.uni()); int lv = r.irange(1, 3); F.rect(x, y, x + w, y + h, lvl(lv)); if (r.uni() < 0.5f) { float o2 = 2 * c.px; F.rect(x + o2, y + o2, x + w + o2, y + h + o2, lvl(lv)); F.rect(x, y, x + w, y + h, lvl(pmod(lv, 3) + 1)); } }
    o.normalize = false;
}
static void gNoodles(GenOut& o, const GenCtx& c) {
    Field F(o, c); int waves = (int)c.p[0]; float th = c.p[1];
    F.each([&](float u, float v, int, int) {
        float y = v * waves + 0.35f * std::sin(u * TAU * 2) + 0.15f * std::sin(u * TAU * 5 + 1);
        float d = absf(fmodp(y, 1.0f) - 0.5f);
        int band = pmod((int)std::floor(y), waves);   // colour repeats with the tile
        if (d < th * 0.5f) return lvl(1 + band % 3);
        return lvl(0);
    });
    o.normalize = false;
}
static void gWarpedChecker(GenOut& o, const GenCtx& c) {
    Field F(o, c); int n = std::max(2, 2 * (int)std::lround(c.p[0] / 2)); float wp = c.p[1];   // even count keeps parity across the seam
    F.each([&](float u, float v, int, int) {
        float du = wp * fbm(u, v, 2, 3, 0.5f, c.seed), dv = wp * fbm(u, v, 2, 3, 0.5f, c.seed + 1);
        int ck = ((int)std::floor((u + du) * n) + (int)std::floor((v + dv) * n)) & 1;
        float fx = fmodp((u + du) * n, 1.0f), fy = fmodp((v + dv) * n, 1.0f);
        float edge = std::min(std::min(fx, 1 - fx), std::min(fy, 1 - fy));
        return ck ? (edge < 0.06f ? lvl(3) : lvl(2)) : (edge < 0.06f ? lvl(1) : lvl(0));
    });
    o.normalize = false;
}
static void gDiamondLattice(GenOut& o, const GenCtx& c) {
    // harlequin diamonds: (ca + cb) parity is invariant under the tile shifts, so the seam is invisible
    Field F(o, c); int n = (int)c.p[0]; float lw = c.p[1];
    F.each([&](float u, float v, int, int) {
        float A = (u + v) * n, B = (u - v) * n;
        float a = A - std::floor(A), b = B - std::floor(B);
        float da = std::min(a, 1 - a), db = std::min(b, 1 - b);
        if (std::min(da, db) < lw * 0.5f) return lvl(3);
        float cxd = a - 0.5f, cyd = b - 0.5f;
        if (cxd * cxd + cyd * cyd < 0.012f) return lvl(0);             // a small dot in each diamond
        int ca = (int)std::floor(A), cb = (int)std::floor(B);
        return lvl(1 + ((ca + cb) & 1));
    });
    o.normalize = false;
}
static void gPixelMosaic(GenOut& o, const GenCtx& c) {
    Field F(o, c); int bs = std::max(2, (int)c.p[0]); float mix = c.p[1];
    int nb = std::max(1, (int)std::lround(c.dw / (float)bs));          // whole blocks per tile
    float bsx = F.w / (float)nb;
    F.each([&](float u, float v, int, int) {
        float X = u * nb, Y = v * nb;
        int bx = (int)std::floor(X), by = (int)std::floor(Y); uint32_t h = hash3(bx, by, c.seed);
        float n = fbm((bx + 0.5f) / nb, (by + 0.5f) / nb, 3, 2, 0.5f, c.seed) * 0.5f + 0.5f;
        float t = lerpf(n, hf(h), mix);
        int k = std::min(3, (int)(t * 4));
        bool border = (X - bx) * bsx < c.px || (Y - by) * bsx < c.px;
        return border ? lvl(0) : lvl(k);
    });
}
static void gShattered(GenOut& o, const GenCtx& c) {
    Field F(o, c); int n = (int)c.p[0]; float edge = c.p[1];
    F.each([&](float u, float v, int, int) {
        Worley w = worley(u, v, n, c.seed, 1.0f);
        if (w.f2 - w.f1 < edge * 0.05f) return lvl(3);
        return lvl(1 + (int)(hf(w.id) * 2.999f) % 2 + (hash32(w.id) % 5 == 0 ? 1 : 0) % 2);
    });
    o.normalize = false;
}
static void gStarburst(GenOut& o, const GenCtx& c) {
    // full-screen sunburst centred on the display
    Field F(o, c); int rays = (int)c.p[0]; float rot = c.p[1] * PI_F / 180;
    F.each([&](float u, float v, int, int) {
        FULLXY
        float th = std::atan2(Y, X) + rot; float r = std::sqrt(X * X + Y * Y);
        int k = (int)std::floor((th / TAU + 1) * rays);
        bool ring = fmodp(r * 6, 1.0f) < 0.05f;
        if (ring) return lvl(3);
        return lvl(1 + pmod(k, 2) + (pmod(k, 4) == 1 ? 1 : 0));
    });
    o.normalize = false;
}
static void gBullseye(GenOut& o, const GenCtx& c) {
    Field F(o, c); float rings = c.p[0], off = c.p[1];
    F.each([&](float u, float v, int, int) {
        float x = wrapd(u - 0.5f * off), y = wrapd(v - 0.5f * off); float r = std::sqrt(x * x + y * y);
        float x2 = wrapd(u), y2 = wrapd(v); float r2 = std::sqrt(x2 * x2 + y2 * y2);
        float rr = std::min(r, r2);
        int band = (int)std::floor(rr * rings);
        return lvl(pmod(band, 4));
    });
    o.normalize = false;
}
static void gTwistedStripes(GenOut& o, const GenCtx& c) {
    Field F(o, c); int n = (int)c.p[0]; float tw = c.p[1];
    F.each([&](float u, float v, int, int) {
        float s = u * n + tw * std::sin(v * TAU) * 0.5f + tw * 0.3f * std::sin(v * TAU * 2 + 1);
        int band = (int)std::floor(s); float f = fmodp(s, 1.0f);
        if (f < 0.1f) return lvl(0);
        return lvl(1 + pmod(band, 3));
    });
    o.normalize = false;
}
static void gBoomerangs(GenOut& o, const GenCtx& c) {
    // each swoosh is a filled crescent (a thick arc tapering at both ends), outlined smoothly
    Field F(o, c); F.fill(lvl(0)); Rng r(c.seed); int n = (int)c.p[0]; float sz = c.p[1] * F.w / 100;
    const int K = 48;
    for (int i = 0; i < n; i++) {
        float x = r.uni() * F.w, y = r.uni() * F.h, a = r.uni() * TAU, s = sz * (0.6f + 0.8f * r.uni()); int lv = r.irange(1, 3);
        float L[K + 1][2], R[K + 1][2];
        for (int k = 0; k <= K; k++) {
            float t = k / (float)K, ang = a - 0.9f + t * 1.8f;
            float rr = s * (0.6f + 0.4f * std::cos((t - 0.5f) * PI_F));
            float hw = s * 0.15f * (0.25f + 0.75f * std::sin(t * PI_F));   // half width, tapering to the tips
            float cx = x + rr * std::cos(ang), cy = y + rr * std::sin(ang);
            // the normal of the centre line (numerically, from the neighbouring point)
            float t2 = std::min(1.0f, t + 1.0f / K), t1 = std::max(0.0f, t - 1.0f / K);
            float a1 = a - 0.9f + t1 * 1.8f, a2 = a - 0.9f + t2 * 1.8f;
            float r1 = s * (0.6f + 0.4f * std::cos((t1 - 0.5f) * PI_F)), r2 = s * (0.6f + 0.4f * std::cos((t2 - 0.5f) * PI_F));
            float dx = r2 * std::cos(a2) - r1 * std::cos(a1), dy = r2 * std::sin(a2) - r1 * std::sin(a1), dl = std::sqrt(dx * dx + dy * dy) + 1e-6f;
            float nx = -dy / dl, ny = dx / dl;
            L[k][0] = cx + nx * hw; L[k][1] = cy + ny * hw; R[k][0] = cx - nx * hw; R[k][1] = cy - ny * hw;
        }
        for (int k = 0; k < K; k++) {
            F.tri(L[k][0], L[k][1], L[k + 1][0], L[k + 1][1], R[k + 1][0], R[k + 1][1], lvl(lv));
            F.tri(L[k][0], L[k][1], R[k + 1][0], R[k + 1][1], R[k][0], R[k][1], lvl(lv));
        }
        for (int e : {0, K}) {   // rounded tips
            float cx = (L[e][0] + R[e][0]) * 0.5f, cy = (L[e][1] + R[e][1]) * 0.5f, hw = std::hypot(L[e][0] - R[e][0], L[e][1] - R[e][1]) * 0.5f;
            F.disc(cx, cy, hw, lvl(lv));
        }
    }
    o.normalize = false;
}
static void gSprinkles(GenOut& o, const GenCtx& c) {
    Field F(o, c); F.fill(lvl(0)); Rng r(c.seed); int n = (int)c.p[0]; float len = c.p[1] * F.w / 100, wd = std::max(1.5f * c.px, len * 0.3f);
    for (int i = 0; i < n; i++) { float x = r.uni() * F.w, y = r.uni() * F.h, a = r.uni() * TAU; F.line(x, y, x + std::cos(a) * len, y + std::sin(a) * len, wd, lvl(r.irange(1, 3))); }
    o.normalize = false;
}

// ---------------------------------------------------------------------------------------------
// TEXT FRIENDLY (20) - low local contrast, soft structure; icons and labels stay readable
// ---------------------------------------------------------------------------------------------
static void gDreamMist(GenOut& o, const GenCtx& c) {
    Field F(o, c); int sc = (int)c.p[0]; float soft = c.p[1];
    F.each([&](float u, float v, int, int) { return fbm(u, v, sc, 3, 0.5f, c.seed) * soft + 0.5f; });
    o.lo = 0.0f; o.hi = 1.0f;
}
static void gPaperFiber(GenOut& o, const GenCtx& c) {
    Field F(o, c); float grain = c.p[0];
    F.each([&](float u, float v, int, int) { return 0.5f + grain * 0.5f * (F.grain(u, v) - 0.5f) * 1.6f + 0.25f * fbm(u, v, 24, 2, 0.5f, c.seed) + 0.15f * fbm(u, v, 3, 2, 0.5f, c.seed + 2); });
    o.lo = 0.002f; o.hi = 0.998f;
}
static void gSoftGrid(GenOut& o, const GenCtx& c) {
    Field F(o, c); int n = (int)c.p[0]; float lw = c.p[1];
    F.each([&](float u, float v, int, int) { float fx = fmodp(u * n, 1.0f), fy = fmodp(v * n, 1.0f); float d = std::min(std::min(fx, 1 - fx), std::min(fy, 1 - fy)); return 0.4f + 0.6f * smooth(0, lw * 0.05f, d); });
    o.normalize = false;
}
static void gFaintDots(GenOut& o, const GenCtx& c) {
    Field F(o, c); int n = (int)c.p[0]; float ds = c.p[1];
    F.each([&](float u, float v, int, int) { float fx = fmodp(u * n, 1.0f) - 0.5f, fy = fmodp(v * n, 1.0f) - 0.5f; float d = std::sqrt(fx * fx + fy * fy); return 0.4f + 0.6f * smooth(ds * 0.1f, ds * 0.1f + 0.05f, d); });
    o.normalize = false;
}
static void gWhisperWaves(GenOut& o, const GenCtx& c) {
    Field F(o, c); int waves = (int)c.p[0]; float amp = c.p[1];
    F.each([&](float u, float v, int, int) { return 0.5f + 0.5f * std::sin(TAU * (v * waves + amp * std::sin(u * TAU) + amp * 0.5f * std::sin(u * TAU * 3 + 1))); });
    o.lo = 0; o.hi = 1;
}
static void gLinenFine(GenOut& o, const GenCtx& c) {
    Field F(o, c); int th = (int)c.p[0];
    F.each([&](float u, float v, int, int) {
        float a = 0.5f + 0.5f * std::cos(TAU * u * th), b = 0.5f + 0.5f * std::cos(TAU * v * th);
        // over / under alternates every thread crossing; blend softly across the crossing
        float pu = std::sin(PI_F * u * th), pv = std::sin(PI_F * v * th);
        float wsel = 0.5f + 0.5f * clampf(pu * pv * 3.0f, -1, 1);
        float wv = lerpf(b, a, wsel);
        return 0.5f + 0.35f * (wv - 0.5f) + 0.25f * (F.grain(u, v) - 0.5f) + 0.1f * fbm(u, v, 4, 2, 0.5f, c.seed);
    });
    o.lo = 0.003f; o.hi = 0.997f;
}
static void gFrostedGlass(GenOut& o, const GenCtx& c) {
    Field F(o, c); int sc = (int)c.p[0];
    F.each([&](float u, float v, int, int) { Worley w = worley(u, v, sc, c.seed, 1.0f); return 0.5f + 0.25f * (w.f1 - 0.5f) + 0.15f * fbm(u, v, sc * 2, 2, 0.5f, c.seed) + 0.12f * (F.grain(u, v) - 0.5f); });
}
static void gPaleTopo(GenOut& o, const GenCtx& c) {
    Field F(o, c); float sp = c.p[0];
    F.each([&](float u, float v, int, int) { float h = fbm(u, v, 2, 4, 0.5f, c.seed) * sp; float f = absf(fmodp(h, 1.0f) - 0.5f); return 0.45f + 0.55f * smooth(0.0f, 0.08f, 0.5f - f) * 0.3f + 0.3f * smooth(0.04f, 0.1f, f); });
}
static void gSubtleHex(GenOut& o, const GenCtx& c) {
    // pointy-top hexagon lattice that tiles exactly: columns n, rows chosen to stay near-regular
    Field F(o, c); int n = (int)c.p[0];
    float s = F.w / (float)n;
    int rows = std::max(1, (int)std::lround(F.h / (s * 1.7320508f)));
    float rH = F.h / (float)rows;                  // vertical period of the rectangular cell
    float line = std::max(0.8f, s * 0.035f);
    F.each([&](float, float, int x, int y) {
        float X = x + 0.5f, Y = y + 0.5f, best = 1e9f;
        for (int k = 0; k < 2; k++) {
            float ox = k ? s * 0.5f : 0, oy = k ? rH * 0.5f : 0;
            float cx = std::floor((X - ox) / s + 0.5f) * s + ox, cy = std::floor((Y - oy) / rH + 0.5f) * rH + oy;
            float dx = std::fabs(X - cx), dy = std::fabs(Y - cy) * (s * 1.7320508f) / rH;   // rescale so the lattice is regular
            float e = std::max(dx, 0.5f * dx + 0.8660254f * dy);
            best = std::min(best, e);
        }
        float edge = s * 0.5f - best;                 // distance to the hexagon border
        return 0.5f + 0.5f * (1.0f - smooth(0.0f, line, edge));
    });
    o.normalize = false;
}
static void gBrushedSteel(GenOut& o, const GenCtx& c) {
    Field F(o, c); int len = std::max(2, (int)c.p[0]);
    // streaks: fine noise stretched along x by the streak length (smooth at any Scale)
    F.each([&](float u, float v, int, int) {
        float s = 0.6f * F.grain(u, v, 1, 1.0f / len, 1.0f) + 0.4f * F.grain(u, v, 2, 2.0f / len, 1.0f);
        return s + 0.3f * fbm(u, v, 2, 2, 0.5f, c.seed) + 0.2f * std::sin(v * TAU * 2);
    });
    o.lo = 0.002f; o.hi = 0.998f;
}
static void gDenim(GenOut& o, const GenCtx& c) {
    Field F(o, c); int n = (int)c.p[0];
    F.each([&](float u, float v, int, int) { int tx = (int)(u * n), ty = (int)(v * n); bool over = pmod(tx - ty, 3) < 2; float fx = fmodp(u * n, 1.0f), fy = fmodp(v * n, 1.0f); float shade = over ? 0.5f + 0.5f * std::sin(fx * PI_F) : 0.5f + 0.5f * std::sin(fy * PI_F); return 0.3f + 0.5f * (over ? 0.85f : 0.55f) * shade + 0.14f * (F.grain(u, v) - 0.5f); });
    o.lo = 0.003f; o.hi = 0.997f;
}
static void gSandstone(GenOut& o, const GenCtx& c) {
    Field F(o, c); float grain = c.p[0]; int bands = (int)c.p[1];
    F.each([&](float u, float v, int, int) { float b = std::sin(TAU * (v * bands + 0.5f * fbm(u, v, 2, 2, 0.5f, c.seed))); return 0.5f + 0.2f * b + grain * 0.36f * (F.grain(u, v) - 0.5f) + 0.15f * fbm(u, v, 16, 2, 0.5f, c.seed + 3); });
    o.lo = 0.002f; o.hi = 0.998f;
}
static void gLeather(GenOut& o, const GenCtx& c) {
    Field F(o, c); int n = (int)c.p[0];
    F.each([&](float u, float v, int, int) { Worley w = worley(u, v, n, c.seed, 1.0f); float crease = smooth(0.0f, 0.12f, w.f2 - w.f1); float dome = 1 - sat(w.f1 * 1.2f); return 0.4f + 0.35f * crease * (0.5f + 0.5f * dome) + 0.1f * fbm(u, v, n * 3, 2, 0.5f, c.seed); });
}
static void gVignette(GenOut& o, const GenCtx& c) {
    Field F(o, c); float str = c.p[0];
    F.each([&](float u, float v, int, int) { FULLXY float d = std::sqrt(X * X + Y * Y) / std::sqrt(a * a / 4 + 0.25f); return 1.0f - str * d * d; });
    o.normalize = false;
}
static void gSoftDiagonal(GenOut& o, const GenCtx& c) {
    Field F(o, c); float ang = c.p[0] * PI_F / 180; int bands = (int)c.p[1];
    F.each([&](float u, float v, int, int) { float t = (u - 0.5f) * c.aspect * std::cos(ang) + (v - 0.5f) * std::sin(ang); return 0.5f + 0.3f * t + 0.1f * std::sin(t * bands * TAU); });
    o.lo = 0; o.hi = 1;
}
static void gCloudVeil(GenOut& o, const GenCtx& c) {
    Field F(o, c); int sc = (int)c.p[0];
    F.each([&](float u, float v, int, int) { return fbm(u * c.aspect, v, sc, 5, 0.5f, c.seed) * 0.5f + 0.5f + 0.2f * (1 - v); });
    o.lo = 0.005f; o.hi = 0.995f;
}
static void gPinstripe(GenOut& o, const GenCtx& c) {
    Field F(o, c); int pitch = std::max(2, (int)c.p[0]); float wd = c.p[1];
    int count = std::max(1, (int)std::lround(c.dw / (float)pitch));     // whole stripes per tile
    float px1 = 1.0f / std::max(1, F.w) * count;                        // one output pixel, in stripe units
    F.each([&](float u, float, int, int) { float f = u * count - std::floor(u * count); float d = std::max(f - wd, -f); return lerpf(0.45f, 0.78f, aa(d, px1)); });
    o.normalize = false;
}
static void gDotMatrix(GenOut& o, const GenCtx& c) {
    Field F(o, c); int pitch = std::max(3, (int)c.p[0]);
    int count = std::max(1, (int)std::lround(c.dw / (float)pitch));
    float pe = F.w / (float)count;
    F.each([&](float u, float v, int, int) {
        float fx = (u * count - std::floor(u * count) - 0.5f) * pe, fy = (v * count - std::floor(v * count) - 0.5f) * pe;
        float d = std::sqrt(fx * fx + fy * fy);
        return 0.5f + 0.5f * smooth(pe * 0.18f, pe * 0.18f + c.px, d);
    });
    o.normalize = false;
}
static void gCarbonWeave(GenOut& o, const GenCtx& c) {
    Field F(o, c); int n = std::max(2, 2 * (int)std::lround(c.p[0] / 2));   // even: weave parity matches at the seam
    F.each([&](float u, float v, int, int) { int cx = (int)std::floor(u * n), cy = (int)std::floor(v * n); bool h = (cx + cy) & 1; float f = h ? fmodp(v * n, 1.0f) : fmodp(u * n, 1.0f); float g = h ? fmodp(u * n, 1.0f) : fmodp(v * n, 1.0f); return 0.35f + 0.45f * std::sin(f * PI_F) * (0.8f + 0.2f * std::sin(g * 6 * PI_F)); });
    o.normalize = false;
}
static void gSilkSheen(GenOut& o, const GenCtx& c) {
    Field F(o, c); int waves = (int)c.p[0];
    F.each([&](float u, float v, int, int) { float w = fbm(u, v, 2, 3, 0.5f, c.seed); float s = std::sin(TAU * (u * waves + 0.6f * w)); return 0.5f + 0.35f * std::pow(absf(s), 0.5f) * (s > 0 ? 1 : -1) * 0.7f + 0.1f * w; });
    o.lo = 0; o.hi = 1;
}

// ---------------------------------------------------------------------------------------------
// The v1 tiles (formerly fixed 4-colour pixmaps), now drawn at any resolution. Colour slots keep
// the IRIX order (slot 3 darkest, 1, 0, 2 lightest) so saved colours mean the same thing.
// ---------------------------------------------------------------------------------------------
static uint32_t v1seed(uint32_t seed, int id) { return hash32(seed * 7919U + id * 104729U); }
static void gBasalt(GenOut& o, const GenCtx& c) {
    Field F(o, c); uint32_t s = v1seed(c.seed, 0);
    F.each([&](float u, float v, int, int) { return fbm(u, v, 4, 4, 0.55f, s) + 0.55f * fbm(u, v, 32, 2, 0.5f, s + 9) + 0.32f * (F.grain(u, v) - 0.5f); });
}
static void gMalachite(GenOut& o, const GenCtx& c) {
    Field F(o, c); uint32_t s = v1seed(c.seed, 1);
    F.each([&](float u, float v, int, int) {
        float w = fbm(u, v, 2, 4, 0.5f, s);
        float r = std::sqrt(0.5f + 0.5f * fbm(u, v, 3, 2, 0.5f, s + 7));   // swelling "eyes"
        float ph = TAU * (3 * u + 2 * v) + 5.0f * w + 9.0f * r;
        float b = 0.65f * std::sin(ph) + 0.35f * std::sin(3.0f * ph + 1.0f);
        return b + 0.12f * fbm(u, v, 32, 2, 0.5f, s + 3);
    });
}
static void gStratus(GenOut& o, const GenCtx& c) {
    Field F(o, c); uint32_t s = v1seed(c.seed, 2);
    F.each([&](float u, float v, int, int) {
        float q1 = fbm(u, v, 2, 4, 0.5f, s + 1), q2 = fbm(u, v, 2, 4, 0.5f, s + 2);
        return fbm(u + 0.25f * q1, v + 0.12f * q2, 2, 6, 6, 0.55f, s);
    });
    o.lo = 0.02f; o.hi = 0.995f; o.gamma = 1.2f;
}
static void gCobblestone(GenOut& o, const GenCtx& c) {
    Field F(o, c); uint32_t s = v1seed(c.seed, 3);
    F.each([&](float u, float v, int, int) {
        Worley w = worley(u, v, 5, s, 0.8f);
        float edge = w.f2 - w.f1;                      // 0 at cell borders
        float h = smooth(0.0f, 0.55f, edge);           // dome height
        float len = std::sqrt(w.dx * w.dx + w.dy * w.dy) + 1e-4f;
        float lit = -(w.dx * 0.7071f + w.dy * 0.7071f) / len;  // light from upper-left
        float slope = 1.0f - h;
        return 0.55f * h + 0.45f * (0.5f + 0.5f * lit * (0.25f + slope)) + 0.15f * fbm(u, v, 16, 2, 0.5f, s + 5);
    });
}
static void gCrazing(GenOut& o, const GenCtx& c) {
    Field F(o, c); uint32_t s = v1seed(c.seed, 4);
    F.each([&](float u, float v, int, int) {
        float wu = u + 0.02f * fbm(u, v, 4, 3, 0.5f, s + 1), wv = v + 0.02f * fbm(u, v, 4, 3, 0.5f, s + 2);
        Worley a = worley(wu, wv, 6, s, 0.95f), b = worley(wu, wv, 17, s + 77, 0.95f);
        float base = 0.6f + 0.4f * fbm(u, v, 4, 4, 0.5f, s + 3);
        float c1 = smooth(0.015f, 0.09f, a.f2 - a.f1);
        float c2 = 0.55f + 0.45f * smooth(0.01f, 0.08f, b.f2 - b.f1);
        float shade = 0.85f + 0.15f * smooth(0.0f, 0.6f, a.f2 - a.f1);
        return base * c1 * c2 * shade;
    });
    o.lo = 0.01f; o.hi = 0.995f;
}
static void gCoral(GenOut& o, const GenCtx& c) {
    Field F(o, c); uint32_t s = v1seed(c.seed, 5);
    F.each([&](float u, float v, int, int) { float w = fbm(u, v, 3, 3, 0.45f, s); return std::cos(TAU * w * 13.0f) + 0.15f * fbm(u, v, 32, 2, 0.5f, s + 4); });
}
static void gTidewater(GenOut& o, const GenCtx& c) {
    Field F(o, c); uint32_t s = v1seed(c.seed, 6);
    static const int K[5][2] = {{5, 2}, {-2, 5}, {4, -4}, {1, -6}, {6, 3}};
    F.each([&](float u, float v, int, int) {
        float w = fbm(u, v, 2, 4, 0.5f, s) * 7.0f, acc = 0;
        for (int i = 0; i < 5; i++) acc += std::cos(TAU * (K[i][0] * u + K[i][1] * v) + w + i * 1.7f + hf(hash32(s + i)) * TAU);
        return -std::fabs(acc) * 0.6f + 0.4f * acc;
    });
    o.gamma = 0.8f;
}
static void gDunes(GenOut& o, const GenCtx& c) {
    Field F(o, c); uint32_t s = v1seed(c.seed, 7);
    F.each([&](float u, float v, int, int) {
        float w = fbm(u, v, 2, 4, 0.5f, s);
        float ph = (1 * u + 11 * v) + 1.6f * w;
        float saw = ph - std::floor(ph);
        float prof = saw < 0.72f ? saw / 0.72f : (1 - saw) / 0.28f;
        return prof + 0.35f * fbm(u, v, 8, 3, 0.5f, s + 2);
    });
}
static void gZellige(GenOut& o, const GenCtx& c) {
    // eight-point stars on a centred lattice, measured in design pixels of a 64 px repeat
    Field F(o, c); const float N = 64.0f, R = N * 0.19f;
    int reps = std::max(1, (int)std::lround(c.dw / N));   // stars per tile edge (bigger tiles: more stars)
    F.each([&](float u, float v, int, int) {
        float x = fmodp(u * reps, 1.0f) * N, y = fmodp(v * reps, 1.0f) * N, best = 1e9f;
        for (int k = 0; k < 2; k++) {
            float cx = k ? N / 2 : 0, cy = k ? N / 2 : 0;
            float dx = std::fabs(x - cx), dy = std::fabs(y - cy);
            dx = std::min(dx, N - dx); dy = std::min(dy, N - dy);
            best = std::min(best, std::min(std::max(dx, dy), (dx + dy) * 0.70710678f));
        }
        if (best < R * 0.42f) return lvl(2);   // star centre
        if (best < R * 0.52f) return lvl(3);   // inner outline
        if (best < R) return lvl(1);           // star body
        if (best < R + 2.0f) return lvl(3);    // outline
        return lvl(0);                         // ground
    });
    o.normalize = false;
}
static void gLichen(GenOut& o, const GenCtx& c) {
    Field F(o, c); uint32_t s = v1seed(c.seed, 9);
    F.each([&](float u, float v, int, int) {
        float n = fbm(u, v, 4, 4, 0.5f, s + 1);
        Worley w = worley(u + 0.03f * n, v + 0.03f * n, 9, s, 1.0f);
        float r = 0.30f + 0.10f * hf(w.id);
        float base = 0.62f + 0.3f * fbm(u, v, 8, 3, 0.5f, s + 2);
        if (w.f1 < r * 0.55f) return 0.45f + 0.1f * n;
        if (w.f1 < r) return 0.08f;
        return base;
    });
    o.lo = 0; o.hi = 1;
}
static void gTopographic(GenOut& o, const GenCtx& c) {
    // contour map: alternating bands, contour lines and an index contour every fifth level
    Field F(o, c); uint32_t s = v1seed(c.seed, 10);
    float mn = 1e9f, mx = -1e9f;
    for (int j = 0; j < 96; j++) for (int i = 0; i < 96; i++) { float t = fbm((i + 0.5f) / 96, (j + 0.5f) / 96, 2, 5, 0.5f, s); mn = std::min(mn, t); mx = std::max(mx, t); }
    const float bands = 14;
    float k = bands / (mx - mn + 1e-6f);
    F.each([&](float u, float v, int, int) { return (fbm(u, v, 2, 5, 0.5f, s) - mn) * k; });
    std::vector<float> t = o.f;
    int W = F.w, H = F.h;
    float* out = o.f.data();
    ParallelFor(H, [&](int y) {
        for (int x = 0; x < W; x++) {
            float v0 = t[(size_t)y * W + x];
            float gx = 0.5f * (t[(size_t)y * W + pmod(x + 1, W)] - t[(size_t)y * W + pmod(x - 1, W)]);
            float gy = 0.5f * (t[(size_t)pmod(y + 1, H) * W + x] - t[(size_t)pmod(y - 1, H) * W + x]);
            float g = std::sqrt(gx * gx + gy * gy) + 1e-6f;   // levels per output pixel
            float fr = v0 - std::floor(v0 + 0.5f);
            float d = std::fabs(fr) / g;                      // output pixels to the nearest contour
            int l = (int)std::floor(v0 + 0.5f);
            float r;
            if (pmod(l, 5) == 0 && d < 0.9f * c.px) r = lvl(3);
            else if (d < 0.5f * c.px) r = lvl(1);
            else r = pmod((int)std::floor(v0), 2) ? lvl(0) : lvl(2);
            out[(size_t)y * W + x] = r;
        }
    });
    o.normalize = false;
}
static void gPlasma(GenOut& o, const GenCtx& c) {
    Field F(o, c); uint32_t s = v1seed(c.seed, 11);
    float p1 = hf(s) * TAU, p2 = hf(s + 1) * TAU, p3 = hf(s + 2) * TAU;
    int f = std::max(1, (int)c.p[0]);
    F.each([&](float u, float v, int, int) {
        return std::sin(TAU * u * 2 * f + p1) + std::sin(TAU * v * 3 * f + p2) + std::sin(TAU * (u + v) * 2 * f + p3) + std::cos(TAU * (3 * u - 2 * v) * f) +
               std::sin(TAU * 2 * std::sin(TAU * u * f) * 0.25f + TAU * v * 2 * f);
    });
    o.lo = 0; o.hi = 1;
}
static void gHoundstooth(GenOut& o, const GenCtx& c) {
    // 2/2 twill, 8-thread repeat, threads two design pixels wide
    Field F(o, c);
    F.each([&](float u, float v, int, int) {
        int tx = (int)std::floor(u * 8), ty = (int)std::floor(v * 8);
        bool warpDark = (tx % 8) < 4, weftDark = (ty % 8) < 4;
        bool warpUp = pmod(tx - ty, 4) < 2;
        return (warpUp ? warpDark : weftDark) ? lvl(1) : lvl(0);
    });
    o.normalize = false;
}
static void gWickerwork(GenOut& o, const GenCtx& c) {
    Field F(o, c); const int N = 48, B = 12;
    F.each([&](float u, float v, int, int) {
        int x = std::min(N - 1, (int)std::floor(u * N)), y = std::min(N - 1, (int)std::floor(v * N));
        int bx = x / B, by = y / B, lx = x % B, ly = y % B;
        bool horiz = ((bx + by) & 1) == 0;
        int a = horiz ? ly : lx, b = horiz ? lx : ly;
        return ((a % 4) == 3 || b == B - 1) ? lvl(1) : lvl(0);
    });
    o.normalize = false;
}
static void gHerringbone(GenOut& o, const GenCtx& c) {
    Field F(o, c); const int N = 32;
    F.each([&](float u, float v, int, int) {
        int x = std::min(N - 1, (int)std::floor(u * N)), y = std::min(N - 1, (int)std::floor(v * N));
        int band = x / 8;
        int d = (band & 1) ? (x - y + 64) : (x + y);
        bool st = ((d / 4) & 1) != 0;
        if (x % 8 == 7 && ((y / 2) & 1)) st = !st;   // stitched seam between columns
        return st ? lvl(1) : lvl(0);
    });
    o.normalize = false;
}

// ---------------------------------------------------------------------------------------------
// Catalogue
// ---------------------------------------------------------------------------------------------
#include "gens2.inc"   // the generators of the 2.4 families (declarations)
#define P(n, a, b, d, dec) {L##n, a, b, d, dec, nullptr}
#define CH(n, list, d) {L##n, 0, 0, d, 0, L##list}
#define NOP {nullptr, 0, 0, 0, 0, nullptr}
const wchar_t* const kFamilyNames[FAM_COUNT] = {L"Geometric", L"Wireframe", L"Fractals", L"Retro Raster", L"Decorative", L"Topographic",
                                                L"Abstract", L"Procedural Textures", L"Demoscene", L"Neon 90s", L"Text Friendly"};
const GenDef2 kGens2[] = {
    {L"Mandelbrot", FAM_FRACTALS, true, 0, {0x05030f, 0x2a1d6e, 0xd8486a, 0xffd56a}, L"The Mandelbrot set with smooth escape-time bands", 4, {P("Zoom", 0, 12, 0.3f, 1), P("Iterations", 30, 400, 120, 0), P("Center X", -2, 1, -0.6f, 3), P("Center Y", -1.2f, 1.2f, 0, 3), NOP, NOP}, false, false, gMandelbrot, GF_EDGES},
    {L"Julia Set", FAM_FRACTALS, true, 0, {0x120a2a, 0x3b2f8f, 0x55c8e8, 0xffffff}, L"A Julia set - pick its constant with Re and Im", 4, {P("Re", -1, 1, -0.76f, 3), P("Im", -1, 1, 0.1f, 3), P("Zoom", 0, 6, 0, 1), P("Iterations", 30, 400, 100, 0), NOP, NOP}, false, false, gJulia, GF_EDGES},
    {L"Newton Basins", FAM_FRACTALS, true, 0, {0x1b2a4a, 0x2f8f6f, 0xd2b24c, 0xc0392b}, L"Newton's method on z^n - 1: basins of attraction, shaded by convergence speed", 2, {P("Degree", 3, 7, 3, 0), P("Zoom", -2, 4, 0, 1), NOP, NOP, NOP, NOP}, false, false, gNewton, GF_EDGES},
    {L"Lyapunov Space", FAM_FRACTALS, true, 0, {0x0a0a1a, 0x0b4f6c, 0x01baef, 0xfff07c}, L"Markus-Lyapunov fractal (AABAB) - order versus chaos in a logistic map", 0, {NOP, NOP, NOP, NOP, NOP, NOP}, false, false, gLyapunov, 0},
    {L"Sierpinski Carpet", FAM_FRACTALS, false, 243, {0xf2e8d0, 0xc9a66b, 0x8c5e3c, 0x2b1b12}, L"Self-similar square carpet; depth sets the recursion", 1, {P("Depth", 1, 5, 4, 0), NOP, NOP, NOP, NOP, NOP}, false, false, gSierpinski, GF_DISCRETE|GF_PIXEL},
    {L"Hilbert Curve", FAM_FRACTALS, false, 256, {0x0b1a2a, 0x1f6f8b, 0x5fd3bc, 0xeafff5}, L"One continuous space-filling curve - brightness follows its path", 2, {P("Order", 3, 7, 5, 0), P("Thickness", 0.1f, 0.8f, 0.35f, 2), NOP, NOP, NOP, NOP}, false, false, gHilbert, GF_EDGES},
    {L"Lissajous Web", FAM_TOPO, true, 0, {0x05050a, 0x303060, 0x8080ff, 0xffffff}, L"Nested Lissajous figures drawn in phosphor lines", 3, {P("A", 1, 9, 3, 0), P("B", 1, 9, 4, 0), P("Curves", 1, 8, 4, 0), NOP, NOP, NOP}, false, true, gLissajous, GF_EDGES},
    {L"Harmonograph", FAM_TOPO, true, 0, {0x0a0a0a, 0x2d4a3e, 0x6fbf9a, 0xeafbe9}, L"Decaying pendulum drawing - damped sines in two axes", 3, {P("Damping", 0.005f, 0.05f, 0.012f, 3), P("Freq 1", 1, 8, 3, 1), P("Freq 2", 1, 8, 2, 1), NOP, NOP, NOP}, false, true, gHarmonograph, GF_EDGES},
    {L"Phyllotaxis", FAM_FRACTALS, true, 0, {0x0f1d0e, 0x3c7a2a, 0xa8d85b, 0xfff6c8}, L"Sunflower seed spiral at the golden angle", 3, {P("Seeds", 100, 3000, 900, 0), P("Spacing", 4, 20, 9, 1), P("Dot Size", 0.2f, 1.5f, 0.6f, 2), NOP, NOP, NOP}, false, false, gPhyllotaxis, GF_EDGES},
    {L"Moire Rings", FAM_WIREFRAME, true, 0, {0x1a1a1a, 0x4a4a6a, 0x9a9ad0, 0xffffff}, L"Two interfering ring families produce a moire bloom", 2, {P("Spacing", 8, 60, 28, 0), P("Offset", 0.02f, 0.5f, 0.17f, 2), NOP, NOP, NOP, NOP}, false, false, gMoire, GF_EDGES},
    {L"Chladni Plate", FAM_TOPO, true, 0, {0x1a1410, 0x5c4630, 0xd2b48c, 0xfff8e8}, L"Standing-wave nodal lines of a vibrating plate", 2, {P("Mode N", 1, 9, 5, 0), P("Mode M", 1, 9, 2, 0), NOP, NOP, NOP, NOP}, false, false, gChladni, GF_EDGES},
    {L"Rose Curves", FAM_DECORATIVE, true, 0, {0x1a0510, 0x7a1540, 0xf04a8a, 0xffd0e0}, L"Rhodonea curves r = cos(k theta), nested", 2, {P("k", 1.5f, 9, 3.5f, 1), P("Layers", 1, 5, 3, 0), NOP, NOP, NOP, NOP}, false, false, gRose, GF_EDGES},
    {L"Domain Warp", FAM_TOPO, false, 256, {0x0c1020, 0x1f4a6a, 0x4fb0a0, 0xf0e8c0}, L"Noise warped by noise warped by noise - liquid topography", 3, {P("Scale", 1, 6, 2, 0), P("Warp", 0, 1.5f, 0.6f, 2), P("Octaves", 1, 6, 4, 0), NOP, NOP, NOP}, false, true, gDomainWarp, 0},
    {L"Reaction-Diffusion", FAM_ABSTRACT, false, 128, {0x101820, 0x2a6a8a, 0x7fd0e0, 0xf0ffff}, L"Gray-Scott chemistry grown live - spots, worms and mazes", 3, {P("Feed", 0.01f, 0.1f, 0.055f, 3), P("Kill", 0.045f, 0.07f, 0.062f, 3), P("Steps", 200, 4000, 2000, 0), NOP, NOP, NOP}, false, true, gReactionDiffusion, 0},
    {L"Cellular Automaton", FAM_RETRO, false, 256, {0x1a1a2a, 0x3a5a9a, 0x9ab8ff, 0xffffff}, L"Elementary 1-D automaton (rule 30, 90, 110...) run down the tile", 2, {P("Rule", 0, 255, 30, 0), P("Cell", 1, 8, 2, 0), NOP, NOP, NOP, NOP}, false, true, gAutomaton, GF_DISCRETE|GF_PIXEL},
    {L"Life Colony", FAM_RETRO, false, 256, {0x0a0f0a, 0x2a7a3a, 0x7adf5a, 0xeaffd0}, L"Conway's Game of Life after N generations, aged cells darker", 3, {P("Density", 0.1f, 0.7f, 0.35f, 2), P("Generations", 0, 200, 40, 0), P("Cell", 2, 8, 4, 0), NOP, NOP, NOP}, false, true, gLife, GF_DISCRETE|GF_PIXEL},
    {L"DLA Crystal", FAM_ABSTRACT, false, 128, {0x08080c, 0x3a3a6a, 0x9a9ae0, 0xffffff}, L"Diffusion-limited aggregation - frost growing on a window", 2, {P("Particles", 500, 6000, 2500, 0), P("Stickiness", 0.1f, 1, 0.8f, 2), NOP, NOP, NOP, NOP}, false, true, gDLA, 0},
    {L"Mandala", FAM_DECORATIVE, false, 256, {0x1a0a2a, 0x6a2a8a, 0xe8a04a, 0xfff2c8}, L"Radially symmetric rings and petals", 2, {P("Symmetry", 3, 16, 8, 0), P("Rings", 2, 12, 5, 1), NOP, NOP, NOP, NOP}, false, true, gMandala, 0},
    {L"Truchet Tiles", FAM_GEOMETRIC, false, 256, {0xf4efe2, 0xd8c8a8, 0x6a4a3a, 0x202020}, L"Random quarter-arc or diagonal tiles forming endless paths", 3, {P("Cells", 4, 24, 10, 0), P("Line Width", 0.1f, 0.6f, 0.3f, 2), P("Style", 0, 1, 0, 0), NOP, NOP, NOP}, false, true, gTruchet, GF_EDGES},
    {L"De Jong Attractor", FAM_FRACTALS, true, 0, {0x050505, 0x3a1a5a, 0xb050d0, 0xffffff}, L"Peter de Jong strange attractor - millions of points, four numbers", 4, {P("a", -3, 3, 1.4f, 2), P("b", -3, 3, -2.3f, 2), P("c", -3, 3, 2.4f, 2), P("d", -3, 3, -2.1f, 2), NOP, NOP}, false, false, gDeJong, GF_EDGES},
    {L"Clifford Attractor", FAM_FRACTALS, true, 0, {0x050a0a, 0x0a4a4a, 0x40c0b0, 0xf0fff0}, L"Clifford Pickover's attractor - smoky orbits", 4, {P("a", -3, 3, -1.4f, 2), P("b", -3, 3, 1.6f, 2), P("c", -3, 3, 1.0f, 2), P("d", -3, 3, 0.7f, 2), NOP, NOP}, false, false, gClifford, GF_EDGES},
    {L"Hopalong", FAM_FRACTALS, true, 0, {0x0a0a0a, 0x4a3a0a, 0xe0a020, 0xffffe0}, L"Barry Martin's Hopalong orbit fractal", 3, {P("a", -100, 100, -55, 1), P("b", -10, 10, -1, 2), P("c", -100, 100, -42, 1), NOP, NOP, NOP}, false, false, gHopalong, GF_EDGES},
    {L"Gumowski-Mira", FAM_FRACTALS, true, 0, {0x0a0510, 0x3a1a4a, 0xb080e0, 0xfff0ff}, L"Gumowski-Mira map - particle-accelerator mathematics", 2, {P("Mu", -1, 1, -0.496f, 3), P("Alpha", 0, 0.05f, 0.008f, 4), NOP, NOP, NOP, NOP}, false, false, gMira, GF_EDGES},
    {L"Barnsley Fern", FAM_FRACTALS, true, 0, {0x05100a, 0x1a5a2a, 0x5ab84a, 0xe0ffd0}, L"Iterated-function-system fern", 1, {P("Points (k)", 50, 2000, 600, 0), NOP, NOP, NOP, NOP, NOP}, false, true, gFern, GF_EDGES},
    {L"Koch Snowflake", FAM_FRACTALS, true, 0, {0x0a1420, 0x2a5a8a, 0x90d8ff, 0xffffff}, L"Nested Koch snowflakes", 1, {P("Depth", 1, 6, 4, 0), NOP, NOP, NOP, NOP, NOP}, false, false, gKoch, GF_EDGES},
    {L"Dragon Curve", FAM_FRACTALS, true, 0, {0x100808, 0x7a2a1a, 0xf0803a, 0xfff0d0}, L"Heighway dragon, brightness follows the fold order", 1, {P("Iterations", 6, 15, 12, 0), NOP, NOP, NOP, NOP, NOP}, false, false, gDragon, GF_EDGES},
    {L"Voronoi Cells", FAM_FRACTALS, false, 256, {0x101018, 0x3a4a6a, 0x9ab0c8, 0xffffff}, L"Cell diagram with shaded or flat cells and crisp borders", 3, {P("Cells", 3, 24, 8, 0), P("Edge", 0.1f, 1.5f, 0.5f, 2), P("Shading", 0, 2, 1, 0), NOP, NOP, NOP}, false, true, gVoronoi, GF_EDGES},
    {L"Neighbor Web", FAM_ABSTRACT, false, 256, {0x0a0a14, 0x2a3a6a, 0x6a9ae0, 0xffffff}, L"Points linked to their nearest neighbours - constellation graph", 2, {P("Points", 8, 80, 30, 0), P("Links", 1, 6, 3, 0), NOP, NOP, NOP, NOP}, false, true, gNeighborWeb, GF_EDGES},
    {L"Flow Field", FAM_TOPO, false, 256, {0x0a0e14, 0x2a4a5a, 0x7ab8c0, 0xf0ffff}, L"Particles streaking along a noise field", 2, {P("Streaks", 50, 1500, 500, 0), P("Length", 10, 200, 60, 0), NOP, NOP, NOP, NOP}, false, true, gFlowField, GF_EDGES},
    {L"Hex Pulse", FAM_ABSTRACT, false, 256, {0x0a1010, 0x1a5a5a, 0x40c0b0, 0xe0fff8}, L"Hexagonal cells each pulsing at its own phase", 2, {P("Cells", 3, 16, 7, 0), P("Pulse", 0, 6, 2, 0), NOP, NOP, NOP, NOP}, false, true, gHexPulse, GF_EDGES},
    {L"Quasicrystal", FAM_PROCEDURAL, false, 256, {0x100a1a, 0x4a2a7a, 0xc080e0, 0xffffff}, L"Sum of N rotated plane waves - almost-periodic order", 2, {P("Fold", 3, 9, 5, 0), P("Frequency", 2, 12, 6, 0), NOP, NOP, NOP, NOP}, false, false, gQuasicrystal, GF_EDGES},
    {L"Data Stream", FAM_RETRO, false, 256, {0x020804, 0x0a5a1a, 0x40e060, 0xd0ffd0}, L"Falling columns of code", 2, {P("Column", 4, 16, 8, 0), P("Density", 0.2f, 1, 0.7f, 2), NOP, NOP, NOP, NOP}, false, true, gDataStream, GF_DISCRETE|GF_PIXEL},
    {L"Wireframe Terrain", FAM_WIREFRAME, true, 0, {0x05050a, 0x2a2a6a, 0x40c0ff, 0xffffff}, L"Perspective wire mesh landscape receding to a horizon", 3, {P("Grid", 10, 60, 30, 0), P("Height", 0, 0.6f, 0.25f, 2), P("Horizon", 0.2f, 0.7f, 0.45f, 2), NOP, NOP, NOP}, false, true, gWireTerrain, GF_EDGES},
    {L"Checkered Horizon", FAM_DEMOSCENE, true, 0, {0x0a0a20, 0x6a2a8a, 0xf0a0c0, 0xffffff}, L"The infinite checkerboard plane every renderer demo started with", 2, {P("Size", 1, 8, 3, 1), P("Horizon", 0.3f, 0.7f, 0.5f, 2), NOP, NOP, NOP, NOP}, false, false, gCheckerHorizon, GF_EDGES},
    {L"Plasma Tunnel", FAM_DEMOSCENE, true, 0, {0x100020, 0x6a10a0, 0xff40a0, 0xffe0a0}, L"Demoscene tunnel effect - infinite rings twisting inwards", 2, {P("Twist", 0, 8, 3, 1), P("Rings", 2, 20, 8, 1), NOP, NOP, NOP, NOP}, false, false, gPlasmaTunnel, GF_EDGES},
    {L"Rotozoom", FAM_DEMOSCENE, false, 256, {0x1a1a2a, 0x3a3a6a, 0xc0c0ff, 0xffffff}, L"Rotated, zoomed checkerboard - the Amiga intro staple", 3, {P("Angle", 0, 90, 30, 0), P("Zoom", 1, 6, 2, 0), P("Cells", 1, 6, 3, 0), NOP, NOP, NOP}, false, false, gRotozoom, GF_EDGES},
    {L"Starfield", FAM_DEMOSCENE, true, 0, {0x02030a, 0x1a2040, 0x8090c0, 0xffffff}, L"Warp-speed starfield streaking from the centre", 2, {P("Density", 1, 20, 10, 0), P("Streak", 0, 1, 0.35f, 2), NOP, NOP, NOP, NOP}, false, true, gStarfield, GF_EDGES},
    {L"Vector Tunnel", FAM_WIREFRAME, true, 0, {0x050a10, 0x0a3a6a, 0x30b0ff, 0xffffff}, L"Receding polygon rings joined by vector lines", 2, {P("Sides", 3, 12, 6, 0), P("Depth", 3, 24, 12, 0), NOP, NOP, NOP, NOP}, false, false, gVectorTunnel, GF_EDGES},
    {L"Lens Flare", FAM_DEMOSCENE, true, 0, {0x0a0a1a, 0x2a2a5a, 0xe0a060, 0xffffff}, L"Anamorphic lens flare with ghost rings", 3, {P("X", 0, 1, 0.3f, 2), P("Y", 0, 1, 0.35f, 2), P("Size", 0.1f, 1, 0.45f, 2), NOP, NOP, NOP}, false, false, gLensFlare, 0},
    {L"Gouraud Mesh", FAM_DEMOSCENE, false, 256, {0x0a1020, 0x2a4a8a, 0x80b0e0, 0xffffff}, L"Flat-shaded triangle mesh lit from the upper left", 2, {P("Cells", 3, 16, 7, 0), P("Jitter", 0, 1, 0.6f, 2), NOP, NOP, NOP, NOP}, false, true, gGouraud, GF_EDGES},
    {L"Voxel Hills", FAM_DEMOSCENE, true, 0, {0x1a0a2a, 0x5a3a7a, 0xd0a080, 0xfff0d0}, L"Voxel-landscape column renderer, Comanche style", 2, {P("Height", 0.05f, 0.5f, 0.2f, 2), P("Detail", 1, 6, 4, 0), NOP, NOP, NOP, NOP}, false, true, gVoxelHills, GF_EDGES},
    {L"Wire Sphere", FAM_WIREFRAME, true, 0, {0x05050f, 0x1a2a5a, 0x50a0ff, 0xffffff}, L"Latitude-longitude wireframe globe with depth cue", 3, {P("Latitudes", 3, 24, 10, 0), P("Longitudes", 3, 36, 16, 0), P("Size", 0.2f, 0.48f, 0.4f, 2), NOP, NOP, NOP}, false, false, gWireSphere, GF_EDGES},
    {L"Bezier Ribbons", FAM_DEMOSCENE, true, 0, {0x0a0a14, 0x3a1a6a, 0xd060c0, 0xffe0ff}, L"Smooth shaded ribbons sweeping across the screen", 2, {P("Ribbons", 1, 12, 5, 0), P("Width", 0.01f, 0.12f, 0.045f, 3), NOP, NOP, NOP, NOP}, false, true, gRibbons, GF_EDGES},
    {L"Pipe Maze", FAM_DEMOSCENE, false, 256, {0x101010, 0x3a5a3a, 0x80d080, 0xf0fff0}, L"Shaded cylindrical pipes snaking through a grid", 2, {P("Cells", 3, 14, 6, 0), P("Width", 0.05f, 0.3f, 0.16f, 2), NOP, NOP, NOP, NOP}, false, true, gPipeMaze, GF_EDGES},
    {L"Fractal Tree", FAM_FRACTALS, true, 0, {0x0a0f14, 0x2a4a3a, 0x80c080, 0xf0ffe0}, L"Recursive branching tree", 2, {P("Depth", 4, 13, 10, 0), P("Spread", 10, 60, 28, 0), NOP, NOP, NOP, NOP}, false, true, gFractalTree, GF_EDGES},
    {L"Torus Knot", FAM_WIREFRAME, true, 0, {0x0a0510, 0x4a1a6a, 0xd080ff, 0xffffff}, L"A (p,q) torus knot, depth-sorted tube", 3, {P("p", 1, 7, 2, 0), P("q", 1, 9, 3, 0), P("Thickness", 0.01f, 0.08f, 0.035f, 3), NOP, NOP, NOP}, false, false, gTorusKnot, GF_EDGES},
    {L"Raytraced Floor", FAM_DEMOSCENE, true, 0, {0x0a0a10, 0x4a2a2a, 0xe0a0a0, 0xffffff}, L"Shiny spheres on a checkerboard with soft shadows - the 1989 demo", 1, {P("Spheres", 1, 8, 4, 0), NOP, NOP, NOP, NOP, NOP}, false, true, gRaytraceBalls, GF_EDGES},
    {L"Memphis", FAM_NEON, false, 256, {0x1a0b2e, 0xff2d95, 0x00e5c8, 0xfff44f}, L"Squiggles, triangles, dots and hatch - 1991 pattern paper", 2, {P("Shapes", 10, 120, 45, 0), P("Stroke", 1, 6, 2.5f, 1), NOP, NOP, NOP, NOP}, false, true, gMemphis, GF_DISCRETE|GF_EDGES},
    {L"Zigzag Bolts", FAM_NEON, false, 256, {0x0d0d2b, 0xff5ec4, 0x3ae0d9, 0xfff95a}, L"Jagged lightning stripes", 3, {P("Amplitude", 0.1f, 1, 0.5f, 2), P("Pitch", 2, 12, 5, 0), P("Thickness", 0.1f, 0.6f, 0.3f, 2), NOP, NOP, NOP}, false, false, gZigzag, GF_DISCRETE|GF_EDGES},
    {L"Confetti", FAM_NEON, false, 256, {0x14052a, 0xff3d7f, 0x2de2e6, 0xf9f871}, L"Scattered neon triangles", 2, {P("Count", 20, 400, 120, 0), P("Size", 1, 8, 3, 1), NOP, NOP, NOP, NOP}, false, true, gConfetti, GF_DISCRETE|GF_EDGES},
    {L"Synth Grid", FAM_NEON, true, 0, {0x120028, 0x7a1fa8, 0xff2bd6, 0xffd36e}, L"Outrun perspective grid under a striped sun", 3, {P("Spacing", 1, 8, 3, 1), P("Scroll", 0, 1, 0.3f, 2), P("Horizon", 0.35f, 0.7f, 0.55f, 2), NOP, NOP, NOP}, false, false, gSynthGrid, GF_EDGES},
    {L"Laser Beams", FAM_NEON, false, 256, {0x08001a, 0x6a00b0, 0xff40ff, 0xffffff}, L"Criss-crossing laser lines with glow", 2, {P("Beams", 2, 20, 7, 0), P("Width", 1, 6, 2, 1), NOP, NOP, NOP, NOP}, false, true, gLaserBeams, GF_EDGES},
    {L"Splatter Dots", FAM_NEON, false, 256, {0x1e0b3a, 0xff4fa3, 0x2ee6d6, 0xffe14a}, L"Paint-splat dots of every size", 2, {P("Count", 20, 500, 160, 0), P("Max Size", 2, 12, 6, 1), NOP, NOP, NOP, NOP}, false, true, gSplatter, GF_DISCRETE|GF_EDGES},
    {L"Jazz Cups", FAM_NEON, false, 256, {0xf4f1ea, 0x1fb3b0, 0x6e3a9e, 0x202020}, L"The brushstroke squiggle of a certain 1992 paper cup", 2, {P("Strokes", 1, 16, 8, 0), P("Width", 4, 30, 16, 1), NOP, NOP, NOP, NOP}, false, true, gJazz, GF_DISCRETE|GF_EDGES},
    {L"Block Party", FAM_NEON, false, 256, {0x101a3a, 0xff6f3c, 0x1ad4b2, 0xffd93d}, L"Overlapping rectangles with drop shadows", 2, {P("Blocks", 5, 80, 40, 0), P("Size", 4, 30, 16, 1), NOP, NOP, NOP, NOP}, false, true, gBlocks, GF_DISCRETE|GF_EDGES},
    {L"Noodle Waves", FAM_NEON, false, 256, {0x1a0a2a, 0xff2d95, 0x00e5c8, 0xfff44f}, L"Wavy horizontal noodles in rotating colours", 2, {P("Waves", 2, 12, 5, 0), P("Thickness", 0.1f, 0.9f, 0.5f, 2), NOP, NOP, NOP, NOP}, false, false, gNoodles, GF_DISCRETE|GF_EDGES},
    {L"Warped Checker", FAM_NEON, false, 256, {0x0a0a1a, 0xff3ea5, 0x29d9c2, 0xfff44f}, L"Checkerboard melting under a noise warp", 2, {P("Cells", 2, 12, 5, 0), P("Warp", 0, 0.4f, 0.12f, 2), NOP, NOP, NOP, NOP}, false, true, gWarpedChecker, GF_DISCRETE|GF_EDGES},
    {L"Diamond Lattice", FAM_GEOMETRIC, false, 256, {0x12082a, 0xff4d8d, 0x2fd6c4, 0xffffff}, L"Harlequin diamonds with bright seams", 2, {P("Cells", 2, 12, 4, 0), P("Line", 0.02f, 0.3f, 0.08f, 2), NOP, NOP, NOP, NOP}, false, false, gDiamondLattice, GF_DISCRETE|GF_EDGES},
    {L"Pixel Mosaic", FAM_RETRO, false, 256, {0x101020, 0xff3b8f, 0x3de0d0, 0xffe95a}, L"Chunky pixel blocks shaded by noise and chance", 2, {P("Block", 4, 32, 12, 0), P("Mix", 0, 1, 0.5f, 2), NOP, NOP, NOP, NOP}, false, true, gPixelMosaic, GF_DISCRETE|GF_PIXEL},
    {L"Shattered Glass", FAM_ABSTRACT, false, 256, {0x0a0a1a, 0x7a2aa0, 0x30c0d0, 0xffffff}, L"Shards with bright cracks", 2, {P("Shards", 3, 20, 8, 0), P("Edge", 0.2f, 2, 0.8f, 2), NOP, NOP, NOP, NOP}, false, true, gShattered, GF_DISCRETE|GF_EDGES},
    {L"Starburst", FAM_NEON, true, 0, {0x1a0a2a, 0xff2d95, 0x00e5c8, 0xfff44f}, L"Radiating sunburst rays", 2, {P("Rays", 4, 48, 16, 0), P("Rotation", 0, 90, 0, 0), NOP, NOP, NOP, NOP}, false, false, gStarburst, GF_DISCRETE|GF_EDGES},
    {L"Bullseye", FAM_NEON, false, 256, {0x12102a, 0xff4a7d, 0x2ad4c4, 0xffdc5e}, L"Concentric rings cycling the palette", 2, {P("Rings", 2, 24, 8, 1), P("Offset", 0, 1, 0, 2), NOP, NOP, NOP, NOP}, false, false, gBullseye, GF_DISCRETE|GF_EDGES},
    {L"Twisted Stripes", FAM_NEON, false, 256, {0x0a0a1a, 0xff3d9a, 0x2fe3d2, 0xfff44f}, L"Vertical stripes bent by a sine twist", 2, {P("Stripes", 2, 16, 6, 0), P("Twist", 0, 2, 0.6f, 2), NOP, NOP, NOP, NOP}, false, false, gTwistedStripes, GF_DISCRETE|GF_EDGES},
    {L"Boomerangs", FAM_NEON, false, 256, {0xf7f1dc, 0xff4f9a, 0x1fb3b0, 0x6e3a9e}, L"Memphis boomerang swooshes", 2, {P("Count", 5, 80, 40, 0), P("Size", 4, 30, 16, 1), NOP, NOP, NOP, NOP}, false, true, gBoomerangs, GF_DISCRETE|GF_EDGES},
    {L"Sprinkles", FAM_NEON, false, 256, {0x1a0b2e, 0xff2d95, 0x00e5c8, 0xfff44f}, L"Sprinkle dashes at random angles", 2, {P("Count", 20, 600, 200, 0), P("Length", 2, 12, 5, 1), NOP, NOP, NOP, NOP}, false, true, gSprinkles, GF_DISCRETE|GF_EDGES},
    {L"Dream Mist", FAM_TEXT, false, 256, {0x6a7a8e, 0x75879b, 0x8597ab, 0x5f6d80}, L"Soft, slow noise clouds - almost flat, very readable", 2, {P("Scale", 1, 6, 2, 0), P("Softness", 0.2f, 1, 0.6f, 2), NOP, NOP, NOP, NOP}, true, true, gDreamMist, 0},
    {L"Paper Fiber", FAM_TEXT, false, 256, {0xd9d2c0, 0xe2dccb, 0xece6d6, 0xcfc7b3}, L"Fine paper grain", 1, {P("Grain", 0.2f, 1, 0.5f, 2), NOP, NOP, NOP, NOP, NOP}, true, true, gPaperFiber, 0},
    {L"Soft Grid", FAM_TEXT, false, 256, {0x4e5d6e, 0x56657a, 0x5f6f86, 0x465466}, L"Faint ruled grid", 2, {P("Cells", 2, 16, 6, 0), P("Line", 0.2f, 2, 0.6f, 2), NOP, NOP, NOP, NOP}, true, false, gSoftGrid, 0},
    {L"Faint Dots", FAM_TEXT, false, 256, {0x55607a, 0x5c6882, 0x66728d, 0x4a5570}, L"Quiet dot grid", 2, {P("Spacing", 4, 32, 12, 0), P("Dot", 0.5f, 3, 1.2f, 1), NOP, NOP, NOP, NOP}, true, false, gFaintDots, 0},
    {L"Whisper Waves", FAM_TEXT, false, 256, {0x3f5c6b, 0x466576, 0x4f7083, 0x385260}, L"Gentle horizontal wave bands", 2, {P("Waves", 1, 12, 4, 0), P("Amplitude", 0, 0.4f, 0.15f, 2), NOP, NOP, NOP, NOP}, true, false, gWhisperWaves, 0},
    {L"Linen Fine", FAM_TEXT, false, 256, {0x8d8679, 0x968f82, 0xa0998c, 0x807a6d}, L"Tight woven linen", 1, {P("Thread", 16, 96, 48, 0), NOP, NOP, NOP, NOP, NOP}, true, true, gLinenFine, 0},
    {L"Frosted Glass", FAM_TEXT, false, 256, {0x8fa3b5, 0x98acbe, 0xa3b6c8, 0x8396a8}, L"Frosted, milky glass texture", 1, {P("Scale", 4, 40, 16, 0), NOP, NOP, NOP, NOP, NOP}, true, true, gFrostedGlass, 0},
    {L"Pale Topo", FAM_TEXT, false, 256, {0x9aa58f, 0xa3ae98, 0xaeb9a3, 0x8e9983}, L"Faint contour lines", 1, {P("Spacing", 2, 16, 7, 0), NOP, NOP, NOP, NOP, NOP}, true, true, gPaleTopo, 0},
    {L"Subtle Hex", FAM_TEXT, false, 256, {0x5a6270, 0x626a78, 0x6c7482, 0x525a68}, L"Barely-there hexagon lattice", 1, {P("Cells", 3, 20, 8, 0), NOP, NOP, NOP, NOP, NOP}, true, false, gSubtleHex, 0},
    {L"Brushed Steel", FAM_TEXT, false, 256, {0x7d8288, 0x868b91, 0x90959b, 0x72777d}, L"Brushed metal with a soft sheen", 1, {P("Streak", 4, 60, 24, 0), NOP, NOP, NOP, NOP, NOP}, true, true, gBrushedSteel, 0},
    {L"Denim", FAM_TEXT, false, 256, {0x3c4e6e, 0x435677, 0x4c6084, 0x344564}, L"Twill denim weave", 1, {P("Weave", 16, 96, 48, 0), NOP, NOP, NOP, NOP, NOP}, true, true, gDenim, 0},
    {L"Sandstone", FAM_TEXT, false, 256, {0xb8a88a, 0xc1b194, 0xcbbb9f, 0xae9e80}, L"Banded sandstone grain", 2, {P("Grain", 0, 1, 0.5f, 2), P("Bands", 1, 8, 3, 0), NOP, NOP, NOP, NOP}, true, true, gSandstone, 0},
    {L"Leather", FAM_TEXT, false, 256, {0x5e4634, 0x66503c, 0x705a45, 0x543d2c}, L"Pebbled leather grain", 1, {P("Grain", 6, 40, 18, 0), NOP, NOP, NOP, NOP, NOP}, true, true, gLeather, 0},
    {L"Vignette", FAM_TEXT, true, 0, {0x16202c, 0x1f2a3a, 0x2b3a4e, 0x3a4c64}, L"Plain centre fading to darker corners", 1, {P("Strength", 0, 1, 0.6f, 2), NOP, NOP, NOP, NOP, NOP}, true, false, gVignette, 0},
    {L"Soft Diagonal", FAM_TEXT, true, 0, {0x2d3e50, 0x34485c, 0x3d5269, 0x253546}, L"Gentle diagonal gradient with faint bands", 2, {P("Angle", 0, 180, 35, 0), P("Bands", 0, 12, 3, 0), NOP, NOP, NOP, NOP}, true, false, gSoftDiagonal, 0},
    {L"Cloud Veil", FAM_TEXT, true, 0, {0x6f8aa5, 0x7995af, 0x85a0ba, 0x647e98}, L"Thin high cloud across the whole display", 1, {P("Scale", 1, 8, 3, 0), NOP, NOP, NOP, NOP, NOP}, true, true, gCloudVeil, 0},
    {L"Pinstripe", FAM_TEXT, false, 256, {0x3b3f4a, 0x404552, 0x474c5a, 0x343843}, L"Thin vertical pinstripes", 2, {P("Pitch", 3, 24, 8, 0), P("Width", 0.1f, 0.5f, 0.2f, 2), NOP, NOP, NOP, NOP}, true, false, gPinstripe, GF_EDGES},
    {L"Dot Matrix", FAM_TEXT, false, 256, {0x555a66, 0x5c6170, 0x646a7a, 0x4c515c}, L"Regular soft dot matrix", 1, {P("Pitch", 3, 16, 6, 0), NOP, NOP, NOP, NOP, NOP}, true, false, gDotMatrix, 0},
    {L"Carbon Weave", FAM_TEXT, false, 256, {0x2a2d33, 0x303338, 0x383b41, 0x24272c}, L"Carbon-fibre twill", 1, {P("Cells", 4, 32, 12, 0), NOP, NOP, NOP, NOP, NOP}, true, false, gCarbonWeave, GF_EDGES},
    {L"Silk Sheen", FAM_TEXT, false, 256, {0x6b5a78, 0x746283, 0x7e6c8e, 0x61516d}, L"Soft silk folds with a satin sheen", 1, {P("Waves", 1, 8, 3, 0), NOP, NOP, NOP, NOP, NOP}, true, true, gSilkSheen, 0},
    // ---- the v1 tiles, drawn at any resolution
    {L"Basalt", FAM_PROCEDURAL, false, 128, {0x8a8984, 0x5c5b58, 0xbdbab0, 0x2a2a2a}, L"Fine volcanic speckle - granite-style grain", 0, {NOP, NOP, NOP, NOP, NOP, NOP}, false, true, gBasalt, GF_IRIXSLOTS},
    {L"Cobblestone", FAM_PROCEDURAL, false, 128, {0xc4b597, 0x968669, 0xe8dec8, 0x655841}, L"Lit cobbles on a cellular grid - in the spirit of Bulge", 0, {NOP, NOP, NOP, NOP, NOP, NOP}, false, true, gCobblestone, GF_IRIXSLOTS},
    {L"Coral", FAM_ABSTRACT, false, 256, {0xd38a6c, 0xa25a45, 0xf1c3a5, 0x57291f}, L"Meandering coral ridges - in the spirit of Brain", 0, {NOP, NOP, NOP, NOP, NOP, NOP}, false, true, gCoral, GF_IRIXSLOTS | GF_EDGES},
    {L"Crazing", FAM_PROCEDURAL, false, 256, {0xc9c3b2, 0xa29b88, 0xe4dfd0, 0x4b463b}, L"Crackle-glaze ceramic - in the spirit of Cracked", 0, {NOP, NOP, NOP, NOP, NOP, NOP}, false, true, gCrazing, GF_IRIXSLOTS | GF_EDGES},
    {L"Dunes", FAM_PROCEDURAL, false, 256, {0xd8b67e, 0xb5915a, 0xf0dcae, 0x84653a}, L"Wind-blown sand ripples", 0, {NOP, NOP, NOP, NOP, NOP, NOP}, false, true, gDunes, GF_IRIXSLOTS | GF_EDGES},
    {L"Herringbone", FAM_GEOMETRIC, false, 32, {0x5b5f66, 0xa3a8af, 0, 0}, L"Two-colour herringbone weave", 0, {NOP, NOP, NOP, NOP, NOP, NOP}, false, false, gHerringbone, GF_TWO | GF_DISCRETE | GF_PIXEL},
    {L"Houndstooth", FAM_GEOMETRIC, false, 16, {0xe6dfcc, 0x2a2622, 0, 0}, L"Classic 2/2 twill houndstooth", 0, {NOP, NOP, NOP, NOP, NOP, NOP}, false, false, gHoundstooth, GF_TWO | GF_DISCRETE | GF_PIXEL},
    {L"Lichen", FAM_ABSTRACT, false, 256, {0x9aa56a, 0x6b7843, 0xc9d29b, 0x384224}, L"Spotted lichen on stone", 0, {NOP, NOP, NOP, NOP, NOP, NOP}, false, true, gLichen, GF_IRIXSLOTS | GF_EDGES},
    {L"Malachite", FAM_PROCEDURAL, false, 256, {0x2e8c60, 0x1c5c41, 0x6ccb9d, 0x0c2c20}, L"Banded green stone - a cousin of Verde Marble", 0, {NOP, NOP, NOP, NOP, NOP, NOP}, false, true, gMalachite, GF_IRIXSLOTS},
    {L"Plasma", FAM_PROCEDURAL, false, 128, {0xcf4f8a, 0x6a2a8a, 0xf5b24a, 0x1e1450}, L"Demoscene sine plasma", 1, {P("Frequency", 1, 4, 1, 0), NOP, NOP, NOP, NOP, NOP}, false, true, gPlasma, GF_IRIXSLOTS},
    {L"Stratus", FAM_PROCEDURAL, false, 256, {0x9fb4cc, 0x7d95b3, 0xdfe7ef, 0x5d7696}, L"Layered cloud deck - in the spirit of Cirrostratus", 0, {NOP, NOP, NOP, NOP, NOP, NOP}, false, true, gStratus, GF_IRIXSLOTS},
    {L"Tidewater", FAM_PROCEDURAL, false, 256, {0x2e7d95, 0x1f5c73, 0x92d4e0, 0x0e3346}, L"Shallow-water caustics - in the spirit of Tide Pool", 0, {NOP, NOP, NOP, NOP, NOP, NOP}, false, true, gTidewater, GF_IRIXSLOTS},
    {L"Topographic", FAM_TOPO, false, 256, {0xcfd8b8, 0x8a6a4a, 0xe4e9d0, 0x4a3420}, L"Survey map of an imaginary landscape - bands, contours and index contours", 0, {NOP, NOP, NOP, NOP, NOP, NOP}, false, true, gTopographic, GF_DISCRETE | GF_EDGES},
    {L"Wickerwork", FAM_GEOMETRIC, false, 48, {0xb88d5c, 0x5e3f22, 0, 0}, L"Two-colour basket weave", 0, {NOP, NOP, NOP, NOP, NOP, NOP}, false, false, gWickerwork, GF_TWO | GF_DISCRETE | GF_PIXEL},
    {L"Zellige", FAM_GEOMETRIC, false, 64, {0xe9dfc6, 0x1d6b73, 0xd39b3a, 0x1b2430}, L"Eight-point star tilework - in the spirit of Moorish", 0, {NOP, NOP, NOP, NOP, NOP, NOP}, false, false, gZellige, GF_DISCRETE | GF_EDGES},
#include "gens2_table.inc"   // the 2.4 additions
};
const int kGens2Count = (int)(sizeof(kGens2) / sizeof(kGens2[0]));
