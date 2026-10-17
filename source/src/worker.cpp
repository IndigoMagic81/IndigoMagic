// Background rendering: the preview, list thumbnails and long-running tasks never block the UI
#include "app.h"
#include "crash.h"
#include <new>
#include <map>

static CRITICAL_SECTION s_cs;
static HANDLE s_event = nullptr, s_thread = nullptr;
static volatile LONG s_quit = 0;
static HWND s_notify = nullptr;

struct PrevReq { bool pending = false; Config cfg; int w = 0, h = 0; std::wstring key; };
static PrevReq s_req;
static PreviewResult s_res;
static bool s_resValid = false, s_working = false;

struct ThumbReq { std::wstring key; Config cfg; int w, h; };
static std::deque<ThumbReq> s_tq;
static std::map<std::wstring, std::shared_ptr<const Image>> s_thumbs;
static std::deque<std::wstring> s_thumbOrder;

struct CS { CS() { EnterCriticalSection(&s_cs); } ~CS() { LeaveCriticalSection(&s_cs); } };

std::wstring PreviewKey(const Config& c, int w, int h) {
    std::wstring k = ConfigKey(c);
    if (IsDisplayKind(Patterns()[c.pattern].kind)) k += L"|" + std::to_wstring(w) + L"x" + std::to_wstring(h);
    else k += L"|z" + std::to_wstring(c.zoom);
    return k;
}
std::wstring ThumbKey(const Config& c, int w, int h) { return ConfigKey(c) + L"|t" + std::to_wstring(w) + L"x" + std::to_wstring(h) + L"|z" + std::to_wstring(c.zoom); }

static void downsample2(const Image& src, Image& dst) {
    dst.w = src.w / 2; dst.h = src.h / 2;
    dst.px.resize((size_t)dst.w * dst.h);
    for (int y = 0; y < dst.h; y++)
        for (int x = 0; x < dst.w; x++) {
            uint32_t a = src.px[(size_t)(2 * y) * src.w + 2 * x], b = src.px[(size_t)(2 * y) * src.w + 2 * x + 1];
            uint32_t c = src.px[(size_t)(2 * y + 1) * src.w + 2 * x], d = src.px[(size_t)(2 * y + 1) * src.w + 2 * x + 1];
            uint32_t r = (((a >> 16) & 255) + ((b >> 16) & 255) + ((c >> 16) & 255) + ((d >> 16) & 255) + 2) / 4;
            uint32_t gg = (((a >> 8) & 255) + ((b >> 8) & 255) + ((c >> 8) & 255) + ((d >> 8) & 255) + 2) / 4;
            uint32_t bb = ((a & 255) + (b & 255) + (c & 255) + (d & 255) + 2) / 4;
            dst.px[(size_t)y * dst.w + x] = r << 16 | gg << 8 | bb;
        }
}

static DWORD WINAPI workerLoop(LPVOID) {
    CrashThread(L"render worker");
    while (!s_quit) {
        WaitForSingleObject(s_event, 500);
        for (;;) {
            if (s_quit) break;
            PrevReq r;
            ThumbReq t;
            bool havePrev = false, haveThumb = false;
            {
                CS l;
                if (s_req.pending) { r = s_req; s_req.pending = false; havePrev = true; s_working = true; }
                else if (!s_tq.empty()) { t = s_tq.back(); s_tq.pop_back(); haveThumb = true; }
            }
            if (havePrev) {
                LARGE_INTEGER t0, t1, f;
                QueryPerformanceCounter(&t0);
                PreviewResult res;
                res.key = r.key;
                res.cfg = r.cfg;
                int tone = r.cfg.adj.textTone == TT_DARK ? TT_DARK : TT_LIGHT;
                CrashContext(L"preview of \"%ls\" (%ls) %dx%d", Patterns()[r.cfg.pattern].name.c_str(), ConfigKey(r.cfg).c_str(), r.w, r.h);
                try {
                    if (IsTiled(Patterns()[r.cfg.pattern].kind)) {
                        res.tiled = true;
                        res.tile = RenderTile(r.cfg);
                        Image sample;
                        sample.w = 480; sample.h = 270;
                        sample.px.resize((size_t)sample.w * sample.h);
                        RenderRegionFrom(r.cfg, res.tile.get(), sample.px.data(), sample.w, 0, 0, sample.w, sample.h);
                        res.read = AnalyzeReadability(sample, tone);
                    } else {
                        auto img = std::make_shared<Image>();
                        RenderDisplay(r.cfg, *img, std::clamp(r.w, 8, 4096), std::clamp(r.h, 8, 4096));
                        res.read = AnalyzeReadability(*img, tone);
                        res.image = img;
                    }
                } catch (...) {
                    // keep the program alive: the preview shows the error and the report is in crash.log
                    res.tile.reset();
                    res.image.reset();
                    res.error = CrashRecovered(L"render worker (preview)");
                }
                CrashContext(L"idle");
                QueryPerformanceCounter(&t1);
                QueryPerformanceFrequency(&f);
                res.ms = (t1.QuadPart - t0.QuadPart) * 1000.0 / f.QuadPart;
                {
                    CS l;
                    s_res = res;
                    s_resValid = true;
                    s_working = false;
                }
                PostMessageW(s_notify, WM_APP_PREVIEW, 0, 0);
                continue;
            }
            if (haveThumb) {
                {
                    CS l;
                    if (s_thumbs.count(t.key)) continue;
                }
                auto out = std::make_shared<Image>();
                int tw = std::clamp(t.w, 1, 512), th = std::clamp(t.h, 1, 512);
                CrashContext(L"thumbnail of \"%ls\" (%ls) %dx%d", Patterns()[t.cfg.pattern].name.c_str(), ConfigKey(t.cfg).c_str(), tw, th);
                try {
                    Image big;
                    if (IsTiled(Patterns()[t.cfg.pattern].kind)) {
                        auto tile = RenderTile(t.cfg);
                        big.w = tw * 2; big.h = th * 2;
                        big.px.resize((size_t)big.w * big.h);
                        RenderRegionFrom(t.cfg, tile.get(), big.px.data(), big.w, 0, 0, big.w, big.h);
                    } else {
                        RenderDisplay(t.cfg, big, tw * 2, th * 2);
                    }
                    downsample2(big, *out);
                } catch (...) {
                    CrashRecovered(L"render worker (thumbnail)");
                    out->w = tw; out->h = th;   // a plain grey square, so the list does not ask again
                    out->px.assign((size_t)tw * th, 0x9A9A9A);
                }
                CrashContext(L"idle");
                {
                    CS l;
                    s_thumbs[t.key] = out;
                    s_thumbOrder.push_back(t.key);
                    while (s_thumbOrder.size() > 900) { s_thumbs.erase(s_thumbOrder.front()); s_thumbOrder.pop_front(); }
                }
                PostMessageW(s_notify, WM_APP_THUMB, 0, 0);
                continue;
            }
            break;
        }
    }
    return 0;
}

void WorkerStart(HWND notify) {
    InitializeCriticalSection(&s_cs);
    s_notify = notify;
    s_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    s_thread = CreateThread(nullptr, 8 << 20, workerLoop, nullptr, 0, nullptr);
}
void WorkerStop() {
    if (!s_thread) return;
    InterlockedExchange(&s_quit, 1);
    SetEvent(s_event);
    WaitForSingleObject(s_thread, 15000);
    CloseHandle(s_thread);
    s_thread = nullptr;
}
void RequestPreview(const Config& c, int w, int h) {
    CS l;
    std::wstring k = PreviewKey(c, w, h);
    if (s_req.pending && s_req.key == k) return;
    s_req.pending = true;
    s_req.cfg = c;
    s_req.w = w; s_req.h = h;
    s_req.key = k;
    SetEvent(s_event);
}
bool LatestPreview(PreviewResult& out) {
    CS l;
    if (!s_resValid) return false;
    out = s_res;
    return true;
}
bool PreviewPending() {
    CS l;
    return s_req.pending || s_working;
}
std::shared_ptr<const Image> GetThumb(const std::wstring& key) {
    CS l;
    auto it = s_thumbs.find(key);
    return it == s_thumbs.end() ? nullptr : it->second;
}
void RequestThumb(const std::wstring& key, const Config& c, int w, int h) {
    CS l;
    if (s_thumbs.count(key)) return;
    for (auto it = s_tq.begin(); it != s_tq.end(); ++it)
        if (it->key == key) { ThumbReq t = *it; s_tq.erase(it); s_tq.push_back(t); SetEvent(s_event); return; }   // move to front
    s_tq.push_back({key, c, w, h});
    while (s_tq.size() > 120) s_tq.pop_front();
    SetEvent(s_event);
}

struct TaskCtx { std::function<bool(std::wstring&)> fn; std::wstring what; };
static DWORD WINAPI taskThread(LPVOID p) {
    TaskCtx* t = (TaskCtx*)p;
    CrashThread(L"task");
    CrashContext(L"%ls", t->what.c_str());
    std::wstring msg;
    bool ok = false;
    try {
        ok = t->fn(msg);
    } catch (const std::bad_alloc&) {
        ok = false;
        msg = L"Not enough memory for an image that large - try a smaller size.";
    } catch (...) {
        ok = false;
        msg = L"Failed: " + CrashRecovered(L"background task") + L" (details in crash.log)";
    }
    delete t;
    PostMessageW(s_notify, WM_APP_TASK, ok ? 1 : 0, (LPARAM) new std::wstring(msg));
    return 0;
}
HANDLE StartTask(std::function<bool(std::wstring&)> fn, const std::wstring& what) {
    TaskCtx* t = new TaskCtx{fn, what};
    HANDLE h = CreateThread(nullptr, 8 << 20, taskThread, t, 0, nullptr);
    if (!h) delete t;
    return h;
}
