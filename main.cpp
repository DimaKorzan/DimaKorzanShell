// ============================================================================
//  DimaKorzanShell — полноэкранная оболочка-«режим фокуса» (C++17, чистый WinAPI)
//  cl /std:c++20 /permissive- /utf-8 /O2 /EHsc /DUNICODE main.cpp /link /SUBSYSTEM:WINDOWS
//  (нужен Windows SDK 10.0.17763+ : C++/WinRT для медиа-панели)
// ============================================================================
#define _SILENCE_EXPERIMENTAL_COROUTINE_DEPRECATION_WARNINGS
#define NOMINMAX
#include <shlobj.h>
#include <shobjidl.h>
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shlwapi.h>
#include <d2d1.h>
#include <d2d1helper.h>
#include <dwrite.h>
#include <dwmapi.h>
#include <wincodec.h>
#include <dxgiformat.h>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <deque>
#include <mutex>
#include <functional>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cctype>
#include <cstdio>
#include <cwchar>
#include <ctime>
#include <chrono>
#include <atomic>
#include <tlhelp32.h>
#include <winhttp.h>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>

#pragma comment(lib,"d2d1.lib")
#pragma comment(lib,"dwrite.lib")
#pragma comment(lib,"dwmapi.lib")
#pragma comment(lib,"shell32.lib")
#pragma comment(lib,"ole32.lib")
#pragma comment(lib,"uuid.lib")
#pragma comment(lib,"windowscodecs.lib")
#pragma comment(lib,"shlwapi.lib")
#pragma comment(lib,"user32.lib")
#pragma comment(lib,"gdi32.lib")
#pragma comment(lib,"winhttp.lib")
#pragma comment(lib,"windowsapp.lib")

// ============================ 1. УТИЛИТЫ / EASING ============================
struct Col { float r, g, b, a; };
static Col C8(int r, int g, int b, float a = 1.f) { return Col{ r / 255.f, g / 255.f, b / 255.f, a }; }
static Col Mix(Col a, Col b, float t) { return Col{ a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t }; }
static Col Alpha(Col c, float a) { c.a = a; return c; }
struct Rc { float l, t, r, b; float w() const { return r - l; } float h() const { return b - t; } };
static Rc R(float x, float y, float w, float h) { return Rc{ x, y, x + w, y + h }; }
struct Pt { float x, y; };
static bool In(const Rc& r, Pt p) { return p.x >= r.l && p.x < r.r && p.y >= r.t && p.y < r.b; }
static Rc Infl(Rc r, float d) { return Rc{ r.l - d, r.t - d, r.r + d, r.b + d }; }
static float Cl(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
static float Lerpf(float a, float b, float t) { return a + (b - a) * t; }

enum { E_LIN, E_CUBIC, E_BACK, E_INOUT };
static float Ease(int e, float t) {
    t = Cl(t, 0.f, 1.f);
    switch (e) {
    case E_CUBIC: { float u = 1 - t; return 1 - u * u * u; }
    case E_BACK: { const float c1 = 1.70158f, c3 = c1 + 1; float u = t - 1; return 1 + c3 * u * u * u + c1 * u * u; }
    case E_INOUT: return t < 0.5f ? 4 * t * t * t : 1 - powf(-2 * t + 2, 3.f) / 2;
    default: return t;
    }
}

static std::wstring Lower(std::wstring s) { if (!s.empty()) CharLowerBuffW(&s[0], (DWORD)s.size()); return s; }
static std::wstring BaseName(const std::wstring& p) { size_t i = p.find_last_of(L"\\/"); return i == std::wstring::npos ? p : p.substr(i + 1); }
static std::wstring DirName(const std::wstring& p) { size_t i = p.find_last_of(L"\\/"); return i == std::wstring::npos ? L"" : p.substr(0, i); }
static bool EndsWithI(const std::wstring& s, const wchar_t* suf) {
    size_t n = wcslen(suf); if (s.size() < n) return false;
    return _wcsicmp(s.c_str() + s.size() - n, suf) == 0;
}
static std::wstring StripExt(std::wstring s) { size_t i = s.find_last_of(L'.'); if (i != std::wstring::npos && i > 0) s.resize(i); return s; }
static std::string W2U(const std::wstring& w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0'); WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr); return s;
}
static std::wstring U2W(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0'); MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n); return w;
}

// ============================ 2. JSON (свой, простой) ============================
struct JV {
    enum T { Null, Bool, Num, Str, Arr, Obj } t = Null;
    bool b = false; double n = 0; std::wstring s;
    std::vector<JV> a;                       // элементы массива
    std::vector<std::wstring> keys;          // ключи объекта
    std::vector<JV> vals;                    // значения объекта
    const JV* get(const wchar_t* k) const { for (size_t i = 0; i < keys.size(); ++i) if (keys[i] == k) return &vals[i]; return nullptr; }
};
struct JParser {
    const std::wstring& s; size_t i = 0;
    explicit JParser(const std::wstring& str) : s(str) {}
    void ws() { while (i < s.size() && iswspace(s[i])) i++; }
    bool pstr(std::wstring& out) {
        if (i >= s.size() || s[i] != L'"') return false; i++;
        while (i < s.size()) {
            wchar_t c = s[i++];
            if (c == L'"') return true;
            if (c == L'\\') {
                if (i >= s.size()) return false;
                wchar_t e = s[i++];
                switch (e) {
                case L'n': out += L'\n'; break; case L't': out += L'\t'; break; case L'r': out += L'\r'; break;
                case L'b': out += L'\b'; break; case L'f': out += L'\f'; break;
                case L'u': { if (i + 4 > s.size()) return false; out += (wchar_t)wcstoul(s.substr(i, 4).c_str(), nullptr, 16); i += 4; break; }
                default: out += e;
                }
            } else out += c;
        }
        return false;
    }
    bool parse(JV& v, int depth) {
        ws(); if (i >= s.size() || depth > 48) return false;
        wchar_t c = s[i];
        if (c == L'{') {
            i++; v.t = JV::Obj; ws();
            if (i < s.size() && s[i] == L'}') { i++; return true; }
            for (;;) {
                ws(); std::wstring k; if (!pstr(k)) return false; ws();
                if (i >= s.size() || s[i] != L':') return false; i++;
                JV c2; if (!parse(c2, depth + 1)) return false;
                v.keys.push_back(k); v.vals.push_back(std::move(c2)); ws();
                if (i >= s.size()) return false;
                if (s[i] == L',') { i++; continue; }
                if (s[i] == L'}') { i++; return true; }
                return false;
            }
        }
        if (c == L'[') {
            i++; v.t = JV::Arr; ws();
            if (i < s.size() && s[i] == L']') { i++; return true; }
            for (;;) {
                JV c2; if (!parse(c2, depth + 1)) return false; v.a.push_back(std::move(c2)); ws();
                if (i >= s.size()) return false;
                if (s[i] == L',') { i++; continue; }
                if (s[i] == L']') { i++; return true; }
                return false;
            }
        }
        if (c == L'"') { v.t = JV::Str; return pstr(v.s); }
        if (s.compare(i, 4, L"true") == 0) { v.t = JV::Bool; v.b = true; i += 4; return true; }
        if (s.compare(i, 5, L"false") == 0) { v.t = JV::Bool; v.b = false; i += 5; return true; }
        if (s.compare(i, 4, L"null") == 0) { v.t = JV::Null; i += 4; return true; }
        wchar_t* e = nullptr; double d = wcstod(s.c_str() + i, &e);
        if (e == s.c_str() + i) return false;
        v.t = JV::Num; v.n = d; i = (size_t)(e - s.c_str()); return true;
    }
};
static void JWriteStr(std::wstring& o, const std::wstring& s) {
    o += L'"';
    for (wchar_t c : s) {
        switch (c) {
        case L'"': o += L"\\\""; break; case L'\\': o += L"\\\\"; break;
        case L'\n': o += L"\\n"; break; case L'\r': o += L"\\r"; break; case L'\t': o += L"\\t"; break;
        default:
            if (c < 0x20) { wchar_t b[8]; swprintf(b, 8, L"\\u%04x", (unsigned)c); o += b; }
            else o += c;
        }
    }
    o += L'"';
}

// ============================ 3. КОНФИГ ============================
struct Shortcut { std::wstring name, path, aumid, exe; int col = 0, row = 0; int uid = 0; bool dying = false; };
struct Config {
    int theme = 0, ver = 3; float opacity = 0.72f; bool animBg = true, hideTaskbar = true, killExplorer = false, imported = false; float iconScale = 1.f;
    std::wstring wallpaper, geniusToken; std::vector<Shortcut> icons;
};
static Config g_cfg; static int g_uid = 1; static bool g_dirty = false;

static std::wstring ExeDir() { wchar_t b[MAX_PATH * 2]; GetModuleFileNameW(nullptr, b, MAX_PATH * 2); PathRemoveFileSpecW(b); return b; }
static std::wstring CfgPath() { return ExeDir() + L"\\config.json"; }
static std::string ReadFileA(const std::wstring& p) {
    HANDLE h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return std::string();
    LARGE_INTEGER sz; std::string d;
    if (GetFileSizeEx(h, &sz) && sz.QuadPart > 0 && sz.QuadPart < (8 << 20)) {
        d.resize((size_t)sz.QuadPart); DWORD rd = 0;
        if (!ReadFile(h, &d[0], (DWORD)d.size(), &rd, nullptr)) rd = 0;
        d.resize(rd);
    }
    CloseHandle(h); return d;
}
static void LoadConfig() {
    g_cfg = Config();
    std::string raw = ReadFileA(CfgPath());
    if (raw.size() >= 3 && (uint8_t)raw[0] == 0xEF && (uint8_t)raw[1] == 0xBB && (uint8_t)raw[2] == 0xBF) raw.erase(0, 3);
    if (raw.empty()) return;                                   // пустой/отсутствующий файл — дефолты
    std::wstring w = U2W(raw); JParser p(w); JV root;
    if (!p.parse(root, 0) || root.t != JV::Obj) return;        // битый JSON — дефолты
    auto num = [&](const wchar_t* k, double d) { const JV* v = root.get(k); return (v && v->t == JV::Num) ? v->n : d; };
    auto bol = [&](const wchar_t* k, bool d) { const JV* v = root.get(k); return (v && v->t == JV::Bool) ? v->b : d; };
    auto str = [&](const wchar_t* k) { const JV* v = root.get(k); return (v && v->t == JV::Str) ? v->s : std::wstring(); };
    g_cfg.theme = std::max(0, std::min(5, (int)num(L"theme", 0)));
    g_cfg.opacity = Cl((float)num(L"opacity", 0.72), 0.35f, 1.f);
    g_cfg.animBg = bol(L"animBg", true); g_cfg.hideTaskbar = bol(L"hideTaskbar", true); g_cfg.killExplorer = bol(L"killExplorer", false); g_cfg.imported = bol(L"imported", false); g_cfg.iconScale = Cl((float)num(L"iconScale", 1.0), 0.6f, 1.6f);
    g_cfg.wallpaper = str(L"wallpaper"); g_cfg.geniusToken = str(L"geniusToken");
    g_cfg.ver = (int)num(L"ver", 0);
    if (g_cfg.ver < 3) { g_cfg.hideTaskbar = true; g_cfg.killExplorer = false; g_cfg.ver = 3; g_dirty = true; }   // миграция: теперь по умолчанию остаются панель Windows и Проводник
    if (const JV* a = root.get(L"icons")) if (a->t == JV::Arr) for (auto& e : a->a) {
        if (e.t != JV::Obj) continue;
        Shortcut s; auto gs = [&](const wchar_t* k) { const JV* v = e.get(k); return (v && v->t == JV::Str) ? v->s : std::wstring(); };
        auto gn = [&](const wchar_t* k) { const JV* v = e.get(k); return (v && v->t == JV::Num) ? (int)v->n : 0; };
        s.name = gs(L"name"); s.path = gs(L"path"); s.aumid = gs(L"aumid"); s.exe = gs(L"exe"); s.col = gn(L"col"); s.row = gn(L"row");
        if (s.name.empty() || (s.path.empty() && s.aumid.empty())) continue;
        s.uid = g_uid++; g_cfg.icons.push_back(s);
    }
}
static void SaveConfig() {
    g_dirty = false; std::wstring o; wchar_t b[96];
    o += L"{\n";
    swprintf(b, 96, L"  \"theme\": %d,\n", g_cfg.theme); o += b;
    swprintf(b, 96, L"  \"opacity\": %.3f,\n", g_cfg.opacity); o += b;
    o += std::wstring(L"  \"animBg\": ") + (g_cfg.animBg ? L"true" : L"false") + L",\n";
    o += std::wstring(L"  \"hideTaskbar\": ") + (g_cfg.hideTaskbar ? L"true" : L"false") + L",\n";
    o += std::wstring(L"  \"killExplorer\": ") + (g_cfg.killExplorer ? L"true" : L"false") + L",\n";
    o += std::wstring(L"  \"imported\": ") + (g_cfg.imported ? L"true" : L"false") + L",\n";
    swprintf(b, 96, L"  \"iconScale\": %.3f,\n", g_cfg.iconScale); o += b;
    swprintf(b, 96, L"  \"ver\": %d,\n", g_cfg.ver); o += b;
    o += L"  \"geniusToken\": "; JWriteStr(o, g_cfg.geniusToken); o += L",\n";
    o += L"  \"wallpaper\": "; JWriteStr(o, g_cfg.wallpaper); o += L",\n";
    o += L"  \"icons\": [";
    bool first = true;
    for (auto& s : g_cfg.icons) {
        if (s.dying) continue;
        o += first ? L"\n    {" : L",\n    {"; first = false;
        o += L"\"name\": "; JWriteStr(o, s.name); o += L", \"path\": "; JWriteStr(o, s.path);
        o += L", \"aumid\": "; JWriteStr(o, s.aumid); o += L", \"exe\": "; JWriteStr(o, s.exe);
        swprintf(b, 96, L", \"col\": %d, \"row\": %d}", s.col, s.row); o += b;
    }
    o += L"\n  ]\n}\n";
    std::string u = W2U(o); std::wstring path = CfgPath(), tmp = path + L".tmp";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD wr = 0; BOOL ok = WriteFile(h, u.data(), (DWORD)u.size(), &wr, nullptr); CloseHandle(h);
    if (ok) MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
}
static void MarkDirty() { g_dirty = true; }

// ============================ 4. ВРЕМЯ / ТВИНЫ / ТЕМЫ ============================
static HWND g_hwnd = nullptr, g_mini = nullptr; static HINSTANCE g_hi = nullptr;
static UINT g_dpi = 96; static float g_W = 1920, g_H = 1080;
static double g_now = 0, g_animUntil = 0; static bool g_timerOn = false;
static HANDLE g_quitEv = nullptr, g_icoEv = nullptr, g_lyrEv = nullptr, g_medEv = nullptr;
static float TB_H = 60.f;   // высота своей панели задач; 0, когда используется панель Windows
static const UINT WM_APPS = WM_APP + 1, WM_ICON = WM_APP + 2, WM_WINKEY = WM_APP + 3, WM_WINCOMBO = WM_APP + 4;

static double NowSec() {
    static LARGE_INTEGER f = { 0 }; if (!f.QuadPart) QueryPerformanceFrequency(&f);
    LARGE_INTEGER c; QueryPerformanceCounter(&c); return (double)c.QuadPart / (double)f.QuadPart;
}
static void Invalidate() { if (g_hwnd) InvalidateRect(g_hwnd, nullptr, FALSE); }
// запускает 60-FPS таймер на время анимации
static void Kick(double d) {
    double u = g_now + d + 0.05; if (u > g_animUntil) g_animUntil = u;
    if (g_hwnd) { if (!g_timerOn) { SetTimer(g_hwnd, 1, 16, nullptr); g_timerOn = true; } InvalidateRect(g_hwnd, nullptr, FALSE); }
}
struct Tw {
    float from = 0, to = 0, cur = 0; double t0 = 0, dur = 0; int e = E_CUBIC; bool inited = false;
    void jump(float v) { from = to = cur = v; dur = 0; }
    void go(float v, double d = 0.25, int ease = E_CUBIC, double delay = 0) {
        if (v == to && (dur > 0 || cur == v)) return;
        from = cur; to = v; t0 = g_now + delay; dur = d; e = ease; Kick(d + delay);
    }
    float val() {
        if (dur <= 0) { cur = to; return cur; }
        double t = (g_now - t0) / dur;
        if (t <= 0) { cur = from; return cur; }
        if (t >= 1) { dur = 0; cur = to; return cur; }
        cur = from + (to - from) * Ease(e, (float)t); return cur;
    }
};
static std::map<long long, Tw> g_tw;
static Tw& TW(int id, int sub = 0) { return g_tw[(long long)id * 8 + sub]; }

enum { T_BG1, T_BG2, T_BG3, T_PANEL, T_ACC, T_TXT, T_DIM, T_CARD, T_N };
struct Theme { const wchar_t* name; Col c[T_N]; };
static const Theme g_themes[6] = {
    { L"Полночь", { C8(15,18,40), C8(40,22,80), C8(10,40,90), C8(22,26,48), C8(124,140,255), C8(240,242,255), C8(160,168,200), C8(36,40,70) } },
    { L"Океан",   { C8(4,30,50), C8(8,70,100), C8(10,110,130), C8(8,38,56), C8(56,214,230), C8(235,248,252), C8(150,190,205), C8(14,58,80) } },
    { L"Закат",   { C8(50,14,40), C8(120,35,60), C8(200,90,60), C8(50,20,40), C8(255,150,90), C8(255,244,238), C8(210,170,170), C8(80,34,56) } },
    { L"Лес",     { C8(10,34,26), C8(20,70,50), C8(40,100,60), C8(14,40,30), C8(120,230,140), C8(238,252,242), C8(160,200,175), C8(24,62,46) } },
    { L"Графит",  { C8(20,20,22), C8(38,38,44), C8(56,56,64), C8(28,28,32), C8(230,230,240), C8(245,245,248), C8(160,160,170), C8(44,44,50) } },
    { L"Сакура",  { C8(44,20,46), C8(110,50,100), C8(190,90,150), C8(48,22,50), C8(255,140,200), C8(255,244,250), C8(210,170,200), C8(78,36,70) } },
};
static Col g_thFrom[T_N]; static Tw g_thT;
static Col ThemeCol(int i) { return Mix(g_thFrom[i], g_themes[g_cfg.theme].c[i], g_thT.val()); }
static void SetTheme(int i) {
    if (i == g_cfg.theme) return;
    for (int k = 0; k < T_N; ++k) g_thFrom[k] = ThemeCol(k);
    g_cfg.theme = i; g_thT.jump(0); g_thT.go(1, 0.7, E_INOUT); MarkDirty();
}

// ============================ 5. DISCORD IPC (без библиотек) ============================
static const wchar_t* kDiscordAppId = L"1563662563529719858";   // Application ID для Rich Presence
struct DiscState { std::mutex m; std::wstring clientId, details, state; long long start = 0; unsigned ver = 1; } g_disc;
static HANDLE g_pipe = INVALID_HANDLE_VALUE;
static void DiscSetClient(const std::wstring& id) {
    std::wstring d; for (wchar_t c : id) if (c >= L'0' && c <= L'9') d += c;
    std::lock_guard<std::mutex> l(g_disc.m);
    if (d != g_disc.clientId) { g_disc.clientId = d; g_disc.ver++; }
}
static void DiscSet(const std::wstring& details, const std::wstring& state) {
    std::lock_guard<std::mutex> l(g_disc.m);
    if (details != g_disc.details || state != g_disc.state) {
        g_disc.details = details; g_disc.state = state; g_disc.start = (long long)time(nullptr); g_disc.ver++;
    }
}
static std::string JEsc(const std::string& s) {
    std::string o;
    for (unsigned char c : s) {
        if (c == '"') o += "\\\""; else if (c == '\\') o += "\\\\";
        else if (c < 0x20) { char b[8]; snprintf(b, 8, "\\u%04x", c); o += b; }
        else o += (char)c;
    }
    return o;
}
static bool PipeWrite(uint32_t op, const std::string& js) {
    std::vector<char> b(8 + js.size()); uint32_t l = (uint32_t)js.size();
    memcpy(b.data(), &op, 4); memcpy(b.data() + 4, &l, 4); if (!js.empty()) memcpy(b.data() + 8, js.data(), js.size());
    DWORD w = 0; return WriteFile(g_pipe, b.data(), (DWORD)b.size(), &w, nullptr) && w == (DWORD)b.size();
}
// возвращает opcode, -1 — таймаут, -2 — канал сломан
static int PipeRead(std::string* body, DWORD waitMs) {
    DWORD t0 = GetTickCount();
    for (;;) {
        DWORD av = 0; if (!PeekNamedPipe(g_pipe, nullptr, 0, nullptr, &av, nullptr)) return -2;
        if (av >= 8) break; if (GetTickCount() - t0 >= waitMs) return -1; Sleep(20);
    }
    uint32_t hdr[2]; DWORD rd = 0;
    if (!ReadFile(g_pipe, hdr, 8, &rd, nullptr) || rd != 8) return -2;
    if (hdr[1] > 65536) return -2;
    std::string b(hdr[1], '\0'); DWORD got = 0;
    while (got < hdr[1]) { DWORD r2 = 0; if (!ReadFile(g_pipe, &b[got], hdr[1] - got, &r2, nullptr) || r2 == 0) return -2; got += r2; }
    if (body) *body = b; return (int)hdr[0];
}
static void DiscClose() { if (g_pipe != INVALID_HANDLE_VALUE) { CloseHandle(g_pipe); g_pipe = INVALID_HANDLE_VALUE; } }
static DWORD WINAPI DiscordThread(LPVOID) {
    bool connected = false, first = true; std::wstring connId; unsigned sent = 0; int nonce = 0;
    for (;;) {
        if (WaitForSingleObject(g_quitEv, first ? 300 : (connected ? 500 : 3000)) == WAIT_OBJECT_0) break;
        first = false;
        std::wstring cid, det, st; long long start; unsigned ver;
        { std::lock_guard<std::mutex> l(g_disc.m); cid = g_disc.clientId; det = g_disc.details; st = g_disc.state; start = g_disc.start; ver = g_disc.ver; }
        if (cid.empty()) { if (connected) { DiscClose(); connected = false; } continue; }
        if (connected && cid != connId) { DiscClose(); connected = false; }
        if (!connected) {
            for (int n = 0; n < 10 && !connected; ++n) {
                wchar_t name[64]; swprintf(name, 64, L"\\\\?\\pipe\\discord-ipc-%d", n);
                g_pipe = CreateFileW(name, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
                if (g_pipe == INVALID_HANDLE_VALUE) continue;
                std::string hs = "{\"v\":1,\"client_id\":\"" + W2U(cid) + "\"}";
                if (PipeWrite(0, hs)) {
                    std::string body; int op = PipeRead(&body, 2000);
                    if (op == 1) { connected = true; connId = cid; sent = 0; }
                }
                if (!connected) DiscClose();
            }
        }
        if (connected) {
            for (;;) {   // вычитываем входящие кадры
                DWORD av = 0; if (!PeekNamedPipe(g_pipe, nullptr, 0, nullptr, &av, nullptr)) { DiscClose(); connected = false; break; }
                if (av < 8) break;
                int op = PipeRead(nullptr, 200); if (op == 2 || op == -2) { DiscClose(); connected = false; break; }
            }
        }
        if (connected && ver != sent && !det.empty()) {
            std::string js = "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":" + std::to_string(GetCurrentProcessId()) +
                ",\"activity\":{\"details\":\"" + JEsc(W2U(det)) + "\"";
            if (!st.empty()) js += ",\"state\":\"" + JEsc(W2U(st)) + "\"";
            js += ",\"timestamps\":{\"start\":" + std::to_string(start) + "}}},\"nonce\":\"" + std::to_string(++nonce) + "\"}";
            if (PipeWrite(1, js)) sent = ver; else { DiscClose(); connected = false; }
        }
    }
    DiscClose(); return 0;
}

// ============================ 6. ПРИЛОЖЕНИЯ / ИКОНКИ / ЗАПУСК ============================
struct AppEntry { std::wstring name, parse; };
static std::mutex g_appsMx; static std::vector<AppEntry> g_apps, g_appsUI;
static std::wstring AppKey(const std::wstring& parse) {
    if (parse.size() > 2 && parse[1] == L':' && parse[2] == L'\\') return parse;
    return L"shell:AppsFolder\\" + parse;
}
static bool ParseIsPath(const std::wstring& p) { return p.size() > 2 && p[1] == L':' && p[2] == L'\\'; }

static void EnumApps(std::vector<AppEntry>& out) {
    IShellItem* folder = nullptr;
    if (FAILED(SHGetKnownFolderItem(FOLDERID_AppsFolder, KF_FLAG_DEFAULT, nullptr, IID_PPV_ARGS(&folder))) || !folder) return;
    IEnumShellItems* en = nullptr; std::set<std::wstring> seen;
    if (SUCCEEDED(folder->BindToHandler(nullptr, BHID_EnumItems, IID_PPV_ARGS(&en))) && en) {
        IShellItem* it = nullptr; ULONG got = 0;
        while (en->Next(1, &it, &got) == S_OK && it) {
            PWSTR n = nullptr, p = nullptr;
            it->GetDisplayName(SIGDN_NORMALDISPLAY, &n); it->GetDisplayName(SIGDN_PARENTRELATIVEPARSING, &p);
            if (n && p && *n && *p && seen.insert(Lower(p)).second) out.push_back(AppEntry{ n, p });
            if (n) CoTaskMemFree(n); if (p) CoTaskMemFree(p);
            it->Release(); it = nullptr;
        }
        en->Release();
    }
    folder->Release();
}
static DWORD WINAPI AppsThread(LPVOID) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    do {
        std::vector<AppEntry> v; EnumApps(v);
        std::sort(v.begin(), v.end(), [](const AppEntry& a, const AppEntry& b) {
            return CompareStringEx(LOCALE_NAME_USER_DEFAULT, LINGUISTIC_IGNORECASE, a.name.c_str(), -1, b.name.c_str(), -1, nullptr, nullptr, 0) == CSTR_LESS_THAN; });
        { std::lock_guard<std::mutex> l(g_appsMx); g_apps.swap(v); }
        if (g_hwnd) PostMessageW(g_hwnd, WM_APPS, 0, 0);
    } while (WaitForSingleObject(g_quitEv, 60000) == WAIT_TIMEOUT);
    CoUninitialize(); return 0;
}

// --- иконки: фоновый загрузчик, потом D2D-битмапы создаются в UI-потоке ---
struct IconPx { int w = 0, h = 0; std::vector<uint8_t> px; };
static std::mutex g_icoMx; static std::deque<std::wstring> g_icoQ; static std::map<std::wstring, IconPx> g_icoDone; static std::set<std::wstring> g_icoReq;
static void LoadIconPx(const std::wstring& key, IconPx& out) {
    IShellItemImageFactory* f = nullptr;
    if (FAILED(SHCreateItemFromParsingName(key.c_str(), nullptr, IID_PPV_ARGS(&f))) || !f) return;
    HBITMAP hb = nullptr; SIZE sz = { 128, 128 };
    HRESULT hr = f->GetImage(sz, SIIGBF_ICONONLY | SIIGBF_BIGGERSIZEOK, &hb); f->Release();
    if (FAILED(hr) || !hb) return;
    BITMAP bm = {}; GetObjectW(hb, sizeof(bm), &bm); int w = bm.bmWidth, h = bm.bmHeight;
    if (w > 0 && h > 0 && w <= 1024 && h <= 1024) {
        BITMAPINFO bi = {}; bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h;
        bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
        std::vector<uint8_t> buf((size_t)w * h * 4); HDC dc = GetDC(nullptr);
        int got = GetDIBits(dc, hb, 0, h, buf.data(), &bi, DIB_RGB_COLORS); ReleaseDC(nullptr, dc);
        if (got) {
            bool anyA = false, straight = false;
            for (size_t i = 0; i < buf.size(); i += 4) { uint8_t a = buf[i + 3]; if (a) anyA = true; if (buf[i] > a || buf[i + 1] > a || buf[i + 2] > a) straight = true; }
            if (!anyA) { for (size_t i = 0; i < buf.size(); i += 4) buf[i + 3] = 255; }
            else if (straight) for (size_t i = 0; i < buf.size(); i += 4) { uint8_t a = buf[i + 3]; buf[i] = (uint8_t)(buf[i] * a / 255); buf[i + 1] = (uint8_t)(buf[i + 1] * a / 255); buf[i + 2] = (uint8_t)(buf[i + 2] * a / 255); }
            out.w = w; out.h = h; out.px.swap(buf);
        }
    }
    DeleteObject(hb);
}
static DWORD WINAPI IconThread(LPVOID) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    HANDLE ev[2] = { g_icoEv, g_quitEv };
    for (;;) {
        std::wstring key;
        { std::lock_guard<std::mutex> l(g_icoMx); if (!g_icoQ.empty()) { key = g_icoQ.front(); g_icoQ.pop_front(); } }
        if (key.empty()) { if (WaitForMultipleObjects(2, ev, FALSE, INFINITE) == WAIT_OBJECT_0 + 1) break; continue; }
        IconPx px; LoadIconPx(key, px);
        { std::lock_guard<std::mutex> l(g_icoMx); g_icoDone[key] = std::move(px); }
        if (g_hwnd) PostMessageW(g_hwnd, WM_ICON, 0, 0);
        if (WaitForSingleObject(g_quitEv, 0) == WAIT_OBJECT_0) break;
    }
    CoUninitialize(); return 0;
}

// --- запуск ---
static bool ActivateAumid(const std::wstring& aumid) {
    IApplicationActivationManager* m = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ApplicationActivationManager, nullptr, CLSCTX_LOCAL_SERVER, IID_PPV_ARGS(&m))) || !m) return false;
    DWORD pid = 0; HRESULT hr = m->ActivateApplication(aumid.c_str(), nullptr, AO_NONE, &pid); m->Release();
    return SUCCEEDED(hr);
}
static bool ShellRun(const std::wstring& file, const std::wstring& dir, std::wstring* learned) {
    SHELLEXECUTEINFOW e = {}; e.cbSize = sizeof(e);
    e.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI; e.hwnd = g_hwnd; e.lpVerb = L"open";
    e.lpFile = file.c_str(); e.lpDirectory = dir.empty() ? nullptr : dir.c_str(); e.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&e)) return false;
    if (e.hProcess) {
        if (learned) { wchar_t b[MAX_PATH * 2]; DWORD n = MAX_PATH * 2; if (QueryFullProcessImageNameW(e.hProcess, 0, b, &n)) *learned = BaseName(b); }
        CloseHandle(e.hProcess);
    }
    return true;
}
static bool LaunchTarget(const std::wstring& path, const std::wstring& aumid, std::wstring* learned) {
    AllowSetForegroundWindow(ASFW_ANY);
    if (!aumid.empty()) {
        if (aumid.find(L'!') != std::wstring::npos && ActivateAumid(aumid)) return true;   // UWP/Store
        return ShellRun(L"shell:AppsFolder\\" + aumid, L"", nullptr);
    }
    return ShellRun(path, DirName(path), learned);
}
static std::wstring ExeNameForPath(const std::wstring& p) {
    if (EndsWithI(p, L".exe")) return BaseName(p);
    if (EndsWithI(p, L".lnk")) {
        std::wstring r; IShellLinkW* sl = nullptr;
        if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&sl))) && sl) {
            IPersistFile* pf = nullptr;
            if (SUCCEEDED(sl->QueryInterface(IID_PPV_ARGS(&pf))) && pf) {
                if (SUCCEEDED(pf->Load(p.c_str(), STGM_READ))) {
                    wchar_t t[MAX_PATH * 2];
                    if (SUCCEEDED(sl->GetPath(t, MAX_PATH * 2, nullptr, 0)) && EndsWithI(t, L".exe")) r = BaseName(t);
                }
                pf->Release();
            }
            sl->Release();
        }
        return r;
    }
    return L"";
}
static std::wstring ExeFromParse(const std::wstring& parse) { return EndsWithI(parse, L".exe") ? BaseName(parse) : std::wstring(); }

// ============================ 7. СИСТЕМНЫЙ UI / ФОКУС-ВОЧЕР ============================
static HWND g_hidden[16]; static volatile LONG g_hiddenN = 0;
static void HideWin(HWND h) {
    if (!h || !IsWindowVisible(h)) return;
    bool have = false; for (LONG i = 0; i < g_hiddenN; ++i) if (g_hidden[i] == h) have = true;
    if (!have && g_hiddenN < 16) { g_hidden[g_hiddenN] = h; g_hiddenN = g_hiddenN + 1; }
    ShowWindow(h, SW_HIDE);
}
static bool IsShellWorker(HWND h) {
    wchar_t c[64]; GetClassNameW(h, c, 64);
    if (!wcscmp(c, L"Shell_SecondaryTrayWnd")) return true;
    return !wcscmp(c, L"WorkerW") && FindWindowExW(h, nullptr, L"SHELLDLL_DefView", nullptr) != nullptr;
}
static BOOL CALLBACK HideEnum(HWND h, LPARAM) { if (IsShellWorker(h)) HideWin(h); return TRUE; }
static BOOL CALLBACK RestoreEnum(HWND h, LPARAM) { if (IsShellWorker(h)) ShowWindow(h, SW_SHOW); return TRUE; }
static void HideSystemUi() {
    HideWin(FindWindowW(L"Shell_TrayWnd", nullptr)); HideWin(FindWindowW(L"Progman", nullptr));
    EnumWindows(HideEnum, 0);
}
static int g_killsLeft = 0; static double g_killDeadline = 0;
static bool ExplorerRunning() {
    bool r = false; DWORD sess = 0; ProcessIdToSessionId(GetCurrentProcessId(), &sess);
    HANDLE sn = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0); if (sn == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W e = {}; e.dwSize = sizeof(e);
    if (Process32FirstW(sn, &e)) do {
        if (!_wcsicmp(e.szExeFile, L"explorer.exe")) { DWORD ps = 0; if (ProcessIdToSessionId(e.th32ProcessID, &ps) && ps == sess) { r = true; break; } }
    } while (Process32NextW(sn, &e));
    CloseHandle(sn); return r;
}
static void KillExplorer() {
    DWORD sess = 0; ProcessIdToSessionId(GetCurrentProcessId(), &sess);
    HANDLE sn = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0); if (sn == INVALID_HANDLE_VALUE) return;
    PROCESSENTRY32W e = {}; e.dwSize = sizeof(e);
    if (Process32FirstW(sn, &e)) do {
        if (!_wcsicmp(e.szExeFile, L"explorer.exe")) {
            DWORD ps = 0; if (!ProcessIdToSessionId(e.th32ProcessID, &ps) || ps != sess) continue;
            HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, e.th32ProcessID); if (h) { TerminateProcess(h, 0); CloseHandle(h); }
        }
    } while (Process32NextW(sn, &e));
    CloseHandle(sn);
}
static void EnsureExplorer() {   // вернуть стандартную оболочку, если её нет
    static DWORD last = 0; DWORD t = GetTickCount(); if (last && t - last < 4000) return;
    if (ExplorerRunning()) return; last = t;
    ShellExecuteW(nullptr, L"open", L"explorer.exe", nullptr, nullptr, SW_SHOWNORMAL);
}
static void AppBarRemove();
static void RestoreSystem() {   // вызывается при выходе, краше, atexit, ENDSESSION
    AppBarRemove();
    LONG n = g_hiddenN; for (LONG i = 0; i < n && i < 16; ++i) if (IsWindow(g_hidden[i])) ShowWindow(g_hidden[i], SW_SHOW);
    g_hiddenN = 0;
    HWND t = FindWindowW(L"Shell_TrayWnd", nullptr); if (t) ShowWindow(t, SW_SHOW);
    HWND p = FindWindowW(L"Progman", nullptr); if (p) ShowWindow(p, SW_SHOW);
    EnumWindows(RestoreEnum, 0);
    EnsureExplorer();
}
static bool g_respawned = false;
static LONG WINAPI CrashFilter(EXCEPTION_POINTERS*) {   // при падении: вернуть Windows-оболочку и один раз перезапустить себя
    RestoreSystem();
    wchar_t p[MAX_PATH]; if (!g_respawned && GetModuleFileNameW(nullptr, p, MAX_PATH)) ShellExecuteW(nullptr, L"open", p, L"/respawn", nullptr, SW_SHOWNORMAL);
    return EXCEPTION_EXECUTE_HANDLER;
}

static void RebuildAllow() {}
static const wchar_t* kSysAllow[] = { L"taskmgr.exe", L"applicationframehost.exe", L"searchhost.exe", L"searchapp.exe", L"startmenuexperiencehost.exe",
    L"shellexperiencehost.exe", L"lockapp.exe", L"logonui.exe", L"consent.exe", L"textinputhost.exe", L"dwm.exe", L"sihost.exe",
    L"ctfmon.exe", L"shellhost.exe", L"widgets.exe", L"systemsettings.exe" };
static std::wstring ProcExe(DWORD pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid); if (!h) return L"";
    wchar_t b[MAX_PATH * 2]; DWORD n = MAX_PATH * 2; std::wstring r;
    if (QueryFullProcessImageNameW(h, 0, b, &n)) r = BaseName(b);
    CloseHandle(h); return r;
}
static std::wstring ProcAumid(DWORD pid) {
    typedef LONG(WINAPI* PFN)(HANDLE, UINT32*, PWSTR);
    static PFN fn = (PFN)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetApplicationUserModelId");
    if (!fn) return L"";
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid); if (!h) return L"";
    wchar_t b[512]; UINT32 n = 512; std::wstring r; if (fn(h, &n, b) == ERROR_SUCCESS) r = b;
    CloseHandle(h); return r;
}
struct CoreFind { DWORD hostPid; DWORD found; };
static BOOL CALLBACK EnumChildCB(HWND h, LPARAM lp) {
    CoreFind* f = (CoreFind*)lp; wchar_t cls[64]; GetClassNameW(h, cls, 64);
    if (!wcscmp(cls, L"Windows.UI.Core.CoreWindow")) {
        DWORD pid = 0; GetWindowThreadProcessId(h, &pid);
        if (pid && pid != f->hostPid) { f->found = pid; return FALSE; }
    }
    return TRUE;
}

// ============================ 8. СОСТОЯНИЕ UI ============================
struct Hit { Rc r; int id = 0; int cur = 0; std::function<void()> click, right, dbl; std::function<void(Pt, int)> drag; };
static std::vector<Hit> g_hits; static Rc g_clip = { -1e9f, -1e9f, 1e9f, 1e9f }; static bool g_hitsOn = true;
static int g_hover = 0, g_downId = 0, g_cursorKind = 0; static bool g_downValid = false, g_didDrag = false;
static Hit g_downHit; static Pt g_downPos = { 0, 0 }, g_mouse = { 0, 0 };

enum { FLD_SEARCH = 1, FLD_RENAME = 2,
    ID_STARTBTN = 10, ID_FOCUS, ID_PREV, ID_PLAY, ID_NEXT, ID_TAB0, ID_TAB1, ID_ADD, ID_EXIT, ID_SW_ANIM, ID_SW_HIDE, ID_SL_OP,
    ID_WALLSEL, ID_WALLRST, ID_ADDALLOW, ID_MOK, ID_MCANCEL,
    ID_THEME = 100, ID_ALLOWDEL = 200, ID_CTX = 300, ID_ICON = 100000, ID_TILE = 300000 };
enum { ID_SL_SCALE = 450, ID_POWER, ID_TASK0 = 500 };
enum { ID_LAUNCHSEL = 470, ID_LAUNCHAPPS = 471 };
enum { ID_MP_PREV = 400, ID_MP_PLAY, ID_MP_NEXT, ID_MP_T0, ID_MP_T1, ID_MP_GENIUS, ID_MP_PANEL, ID_SW_KILL };

static Tw g_start, g_tab, g_tb, g_ctxT, g_modalT, g_toast; static Tw g_scrTw[2];
static float g_scr[2] = { 0, 0 }, g_scrMax[2] = { 0, 0 }; static Rc g_view[2];
static bool g_startOpen = false, g_ctxOpen = false, g_modalOpen = false; static int g_tabIdx = 0;
static std::wstring g_search, g_renameBuf, g_lastApp, g_toastTxt; static bool g_toastWarn = false; static double g_toastUntil = 0;
static int g_focusId = 0; static double g_focusT0 = 0; static int g_firstApp = -1;
static bool g_launchActive = false, g_launchSaw = false;
struct CtxItem { std::wstring label; std::function<void()> fn; };
static std::vector<CtxItem> g_ctxItems; static Pt g_ctxPos = { 0, 0 };
static std::wstring g_modalTitle; static std::function<void()> g_modalOk;
static int g_dragUid = 0; static bool g_dragging = false; static Pt g_dragOff = { 0, 0 };
static std::set<int> g_selIcons; static std::set<std::wstring> g_selApps;   // мультивыбор: ярлыки на столе и приложения в Пуске (Ctrl+клик)
static bool CtrlDown() { return (GetKeyState(VK_CONTROL) & 0x8000) != 0; }

static float CELL_W = 112, CELL_H = 124; static const float GX0 = 24, GY0 = 24;
static void ApplyScale() { CELL_W = 112.f * g_cfg.iconScale; CELL_H = 124.f * g_cfg.iconScale; }
static void GridDims(int& cols, int& rows) {
    cols = std::max(1, (int)((g_W - 2 * GX0) / CELL_W)); rows = std::max(1, (int)((g_H - TB_H - 2 * GY0) / CELL_H));
}

static void ShowToast(const std::wstring& t, bool warn = false) {
    g_toastTxt = t; g_toastWarn = warn; g_toastUntil = g_now + 3.2; g_toast.go(1, 0.45, E_BACK); Kick(3.8);
}
static std::vector<std::wstring> g_recent;   // приложения, запущенные из оболочки (для Discord)
static void UpdatePresence() {
    if (g_launchActive && !g_recent.empty()) {
        std::wstring s; for (size_t i = 0; i < g_recent.size(); ++i) { if (i) s += L", "; s += g_recent[i]; }
        if (s.size() > 100) { s.resize(99); s += L"…"; }
        DiscSet(L"Запущено: " + s, L"");
    }
    else DiscSet(L"На рабочем столе", L"");
}
static void NoteLaunch(const std::wstring& name) {
    g_lastApp = name; g_recent.erase(std::remove(g_recent.begin(), g_recent.end(), name), g_recent.end());
    g_recent.push_back(name); if (g_recent.size() > 4) g_recent.erase(g_recent.begin());
    g_launchActive = true; g_launchSaw = false; UpdatePresence(); Invalidate();
}
static void SetFocusField(int id) {
    g_focusId = id; g_focusT0 = g_now;
    if (g_hwnd) { if (id) SetTimer(g_hwnd, 6, 530, nullptr); else KillTimer(g_hwnd, 6); }
    Invalidate();
}
static std::wstring* FieldBuf(int id) { return id == FLD_SEARCH ? &g_search : id == FLD_RENAME ? &g_renameBuf : nullptr; }

static void ForceFg(HWND h) {
    HWND fg = GetForegroundWindow(); DWORD ft = fg ? GetWindowThreadProcessId(fg, nullptr) : 0, me = GetCurrentThreadId();
    AllowSetForegroundWindow(ASFW_ANY);
    if (ft && ft != me) AttachThreadInput(me, ft, TRUE);
    SetWindowPos(h, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetForegroundWindow(h); BringWindowToTop(h);
    if (ft && ft != me) AttachThreadInput(me, ft, FALSE);
}
static void ShowShell() { if (IsIconic(g_hwnd)) ShowWindow(g_hwnd, SW_RESTORE); ForceFg(g_hwnd); Kick(0.2); }

static void CloseCtx() { if (g_ctxOpen) { g_ctxOpen = false; g_ctxT.go(0, 0.14); } }
static void OpenCtx(Pt p, std::vector<CtxItem> items) {
    g_ctxItems = std::move(items); g_ctxPos = p; g_ctxOpen = true; g_ctxT.jump(0); g_ctxT.go(1, 0.2, E_BACK);
}
static void OpenStart(bool open) {
    if (open == g_startOpen) return;
    g_startOpen = open; CloseCtx(); g_start.go(open ? 1.f : 0.f, 0.4, E_CUBIC);
    if (open) { g_search.clear(); g_scr[0] = 0; g_scrTw[0].jump(0); SetFocusField(g_tabIdx == 0 ? FLD_SEARCH : 0); }
    else { SetFocusField(0); g_selApps.clear(); }
    Kick(0.5);
}
static void SetTab(int i) { g_tabIdx = i; g_tab.go((float)i, 0.35, E_CUBIC); SetFocusField(i == 0 ? FLD_SEARCH : 0); }

// --- файловые диалоги ---
static std::wstring PickFile(const COMDLG_FILTERSPEC* filters, UINT nf) {
    IFileOpenDialog* d = nullptr; std::wstring res;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&d))) || !d) return res;
    DWORD o = 0; d->GetOptions(&o);
    d->SetOptions(o | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST | FOS_DONTADDTORECENT | FOS_NODEREFERENCELINKS);
    if (filters) d->SetFileTypes(nf, filters);
    if (SUCCEEDED(d->Show(g_hwnd))) {
        IShellItem* it = nullptr;
        if (SUCCEEDED(d->GetResult(&it)) && it) {
            PWSTR p = nullptr; if (SUCCEEDED(it->GetDisplayName(SIGDN_FILESYSPATH, &p)) && p) { res = p; CoTaskMemFree(p); }
            it->Release();
        }
    }
    d->Release(); ShowShell(); return res;
}
static const COMDLG_FILTERSPEC fAll[] = { { L"Программы и ярлыки", L"*.exe;*.lnk;*.bat;*.url" }, { L"Все файлы", L"*.*" } };
static const COMDLG_FILTERSPEC fExe[] = { { L"Программы", L"*.exe" }, { L"Все файлы", L"*.*" } };
static const COMDLG_FILTERSPEC fImg[] = { { L"Изображения", L"*.jpg;*.jpeg;*.png;*.bmp;*.gif;*.tif;*.tiff" }, { L"Все файлы", L"*.*" } };

// --- рабочий стол: ярлыки ---
static Shortcut* FindSc(int uid) { for (auto& s : g_cfg.icons) if (s.uid == uid) return &s; return nullptr; }
static bool Occupied(int c, int r, int ign) { for (auto& s : g_cfg.icons) if (s.uid != ign && !s.dying && s.col == c && s.row == r) return true; return false; }
static void FindFreeCell(int& c, int& r, int ign) {
    int cols, rows; GridDims(cols, rows);
    for (int cc = 0; cc < cols; ++cc) for (int rr = 0; rr < rows; ++rr) if (!Occupied(cc, rr, ign)) { c = cc; r = rr; return; }
    c = 0; r = 0;
}
static void NormalizeGrid() {
    int cols, rows; GridDims(cols, rows);
    for (auto& s : g_cfg.icons) {
        s.col = std::max(0, std::min(cols - 1, s.col)); s.row = std::max(0, std::min(rows - 1, s.row));
        for (auto& o : g_cfg.icons) if (&o != &s && o.uid < s.uid && !o.dying && o.col == s.col && o.row == s.row) { FindFreeCell(s.col, s.row, s.uid); break; }
    }
}
static std::wstring IconKeyOf(const Shortcut& s) { return !s.aumid.empty() ? L"shell:AppsFolder\\" + s.aumid : s.path; }
static void AddShortcut(Shortcut s) {
    s.uid = g_uid++; FindFreeCell(s.col, s.row, s.uid); g_cfg.icons.push_back(s); RebuildAllow(); MarkDirty(); Kick(0.8);
}
static void AddShortcutDialog() {
    std::wstring p = PickFile(fAll, 2); if (p.empty()) return;
    for (auto& s : g_cfg.icons) if (!s.dying && _wcsicmp(s.path.c_str(), p.c_str()) == 0) { ShowToast(L"Этот ярлык уже на рабочем столе"); return; }
    Shortcut s; s.name = StripExt(BaseName(p)); s.path = p; s.exe = ExeNameForPath(p); AddShortcut(s);
}
static void ArrangeIcons() {   // по имени, столбцами сверху вниз
    int cols, rows; GridDims(cols, rows);
    std::stable_sort(g_cfg.icons.begin(), g_cfg.icons.end(), [](const Shortcut& a, const Shortcut& b) {
        return CompareStringEx(LOCALE_NAME_USER_DEFAULT, LINGUISTIC_IGNORECASE, a.name.c_str(), -1, b.name.c_str(), -1, nullptr, nullptr, 0) == CSTR_LESS_THAN; });
    int i = 0; for (auto& s : g_cfg.icons) { if (s.dying) continue; s.col = (i / rows) % cols; s.row = i % rows; ++i; }
    NormalizeGrid(); MarkDirty(); Kick(0.5);
}
static int ImportDesktop() {   // только ярлыки (.lnk/.url) и папки с рабочего стола
    int added = 0; std::vector<std::wstring> dirs; PWSTR p = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &p)) && p) { dirs.push_back(p); CoTaskMemFree(p); }
    p = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_PublicDesktop, 0, nullptr, &p)) && p) { dirs.push_back(p); CoTaskMemFree(p); }
    for (auto& dir : dirs) {
        WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd); if (h == INVALID_HANDLE_VALUE) continue;
        do {
            std::wstring n = fd.cFileName; if (n == L"." || n == L"..") continue;
            if (fd.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) continue;
            bool isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            if (!isDir && !EndsWithI(n, L".lnk") && !EndsWithI(n, L".url")) continue;
            std::wstring full = dir + L"\\" + n, nm = isDir ? n : StripExt(n); bool dup = false;
            for (auto& s : g_cfg.icons) if (!s.dying && (_wcsicmp(s.path.c_str(), full.c_str()) == 0 || _wcsicmp(s.name.c_str(), nm.c_str()) == 0)) { dup = true; break; }
            if (dup) continue;
            Shortcut sc; sc.name = nm; sc.path = full; sc.exe = isDir ? std::wstring() : ExeNameForPath(full);
            AddShortcut(sc); ++added;
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    return added;
}
static void PinApp(const AppEntry& a) {
    for (auto& s : g_cfg.icons) if (!s.dying && ((!s.aumid.empty() && _wcsicmp(s.aumid.c_str(), a.parse.c_str()) == 0) || (!s.path.empty() && _wcsicmp(s.path.c_str(), a.parse.c_str()) == 0))) { ShowToast(L"Уже закреплено на рабочем столе"); return; }
    Shortcut s; s.name = a.name;
    if (ParseIsPath(a.parse)) { s.path = a.parse; s.exe = ExeNameForPath(a.parse); } else { s.aumid = a.parse; s.exe = ExeFromParse(a.parse); }
    AddShortcut(s); ShowToast(L"📌 Закреплено: " + a.name);
}
static bool BadLearn(const std::wstring& e) {
    static const wchar_t* b[] = { L"cmd.exe", L"powershell.exe", L"explorer.exe", L"rundll32.exe", L"wscript.exe", L"cscript.exe", L"conhost.exe", L"msiexec.exe" };
    for (auto x : b) if (_wcsicmp(e.c_str(), x) == 0) return true; return false;
}
static bool LaunchShortcutQuiet(Shortcut& s) {
    std::wstring learned;
    if (!LaunchTarget(s.path, s.aumid, &learned)) { ShowToast(L"Не удалось запустить: " + s.name, true); return false; }
    if (s.exe.empty() && !learned.empty() && !BadLearn(learned)) { s.exe = learned; RebuildAllow(); MarkDirty(); }
    return true;
}
static void LaunchShortcut(Shortcut& s) { if (LaunchShortcutQuiet(s)) { NoteLaunch(s.name); OpenStart(false); } }
static void LaunchSelectedIcons() {   // запуск сразу нескольких ярлыков
    std::vector<int> ids(g_selIcons.begin(), g_selIcons.end()); int n = 0;
    for (int uid : ids) {
        Shortcut* s = FindSc(uid); if (!s || s->dying) continue;
        if (LaunchShortcutQuiet(*s)) { NoteLaunch(s->name); ++n; Sleep(150); }
    }
    g_selIcons.clear(); OpenStart(false);
    if (n) ShowToast(L"Запущено приложений: " + std::to_wstring(n));
}
static void LaunchApp(const AppEntry& a) {
    bool p = ParseIsPath(a.parse);
    if (!LaunchTarget(p ? a.parse : L"", p ? L"" : a.parse, nullptr)) { ShowToast(L"Не удалось запустить: " + a.name, true); return; }
    NoteLaunch(a.name); OpenStart(false);
}
static void LaunchSelectedApps() {
    std::vector<AppEntry> list; for (auto& a : g_appsUI) if (g_selApps.count(a.parse)) list.push_back(a);
    int n = 0;
    for (auto& a : list) {
        bool p = ParseIsPath(a.parse);
        if (LaunchTarget(p ? a.parse : L"", p ? L"" : a.parse, nullptr)) { NoteLaunch(a.name); ++n; Sleep(150); }
        else ShowToast(L"Не удалось запустить: " + a.name, true);
    }
    g_selApps.clear(); OpenStart(false);
    if (n) ShowToast(L"Запущено приложений: " + std::to_wstring(n));
}
static void MediaKey(WORD vk) {
    INPUT in[2] = {}; in[0].type = INPUT_KEYBOARD; in[0].ki.wVk = vk; in[0].ki.dwFlags = KEYEVENTF_EXTENDEDKEY;
    in[1] = in[0]; in[1].ki.dwFlags = KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP; SendInput(2, in, sizeof(INPUT));
}
static void ApplyRect();
static void QuitShell() { SaveConfig(); RestoreSystem(); DestroyWindow(g_hwnd); }

// ============================ 9. РЕНДЕР (D2D) + ВИДЖЕТЫ ============================
static ID2D1Factory* g_d2d = nullptr; static IDWriteFactory* g_dw = nullptr; static IWICImagingFactory* g_wic = nullptr;
static ID2D1HwndRenderTarget* g_rt = nullptr; static ID2D1SolidColorBrush* g_br = nullptr; static float g_a = 1.f;
static std::map<int, IDWriteTextFormat*> g_fmt;
static ID2D1Bitmap* g_wall = nullptr; static std::wstring g_wallTried;
static ID2D1Bitmap* g_mpArt = nullptr; static unsigned g_mpArtVer = 0xffffffffu;
struct IcoEnt { ID2D1Bitmap* b = nullptr; double t0 = 0; };
static std::map<std::wstring, IcoEnt> g_ico;

static D2D1_RECT_F DR(const Rc& r) { return D2D1::RectF(r.l, r.t, r.r, r.b); }
static D2D1_COLOR_F CF(Col c) { return D2D1::ColorF(c.r, c.g, c.b, c.a); }
static void SetC(Col c) { g_br->SetColor(D2D1::ColorF(c.r, c.g, c.b, c.a * g_a)); }
static void FillRR(Rc r, float rad, Col c) { SetC(c); g_rt->FillRoundedRectangle(D2D1::RoundedRect(DR(r), rad, rad), g_br); }
static void StrokeRR(Rc r, float rad, Col c, float w = 1.f) { SetC(c); g_rt->DrawRoundedRectangle(D2D1::RoundedRect(DR(r), rad, rad), g_br, w); }
static void FillRect_(Rc r, Col c) { SetC(c); g_rt->FillRectangle(DR(r), g_br); }
static void Circle(float x, float y, float rad, Col c) { SetC(c); g_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x, y), rad, rad), g_br); }
static void Shadow(Rc r, float rad, float spread, float alpha) {
    const int N = 6;
    for (int i = N; i >= 1; --i) {
        float k = (float)i / N; Rc q = Infl(r, spread * k); q.t += spread * 0.35f; q.b += spread * 0.35f;
        FillRR(q, rad + spread * k, Col{ 0, 0, 0, alpha * (1.f - k) * 0.35f + alpha * 0.02f });
    }
}
static void GradRR(Rc r, float rad, Col a, Col b) {
    D2D1_GRADIENT_STOP st[2]; st[0].position = 0; st[0].color = CF(a); st[1].position = 1; st[1].color = CF(b);
    ID2D1GradientStopCollection* gs = nullptr; if (FAILED(g_rt->CreateGradientStopCollection(st, 2, &gs)) || !gs) return;
    ID2D1LinearGradientBrush* lb = nullptr;
    if (SUCCEEDED(g_rt->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(r.l, r.t), D2D1::Point2F(r.r, r.b)), gs, &lb)) && lb) {
        lb->SetOpacity(g_a); g_rt->FillRoundedRectangle(D2D1::RoundedRect(DR(r), rad, rad), lb); lb->Release();
    }
    gs->Release();
}
static void Poly(const Pt* p, int n, Col c) {
    ID2D1PathGeometry* g = nullptr; if (FAILED(g_d2d->CreatePathGeometry(&g)) || !g) return;
    ID2D1GeometrySink* s = nullptr;
    if (SUCCEEDED(g->Open(&s)) && s) {
        s->BeginFigure(D2D1::Point2F(p[0].x, p[0].y), D2D1_FIGURE_BEGIN_FILLED);
        for (int i = 1; i < n; ++i) s->AddLine(D2D1::Point2F(p[i].x, p[i].y));
        s->EndFigure(D2D1_FIGURE_END_CLOSED); s->Close(); s->Release();
        SetC(c); g_rt->FillGeometry(g, g_br);
    }
    g->Release();
}
static IDWriteTextFormat* Fmt(float size, bool bold, int align) {
    int key = (int)(size * 10) * 100 + (bold ? 10 : 0) + align;
    auto it = g_fmt.find(key); if (it != g_fmt.end()) return it->second;
    IDWriteTextFormat* f = nullptr;
    if (FAILED(g_dw->CreateTextFormat(L"Segoe UI", nullptr, bold ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"ru-ru", &f)) || !f) return nullptr;
    f->SetTextAlignment(align == 1 ? DWRITE_TEXT_ALIGNMENT_CENTER : align == 2 ? DWRITE_TEXT_ALIGNMENT_TRAILING : DWRITE_TEXT_ALIGNMENT_LEADING);
    f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER); f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    IDWriteInlineObject* sign = nullptr;
    if (SUCCEEDED(g_dw->CreateEllipsisTrimmingSign(f, &sign)) && sign) {
        DWRITE_TRIMMING t = { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 }; f->SetTrimming(&t, sign); sign->Release();
    }
    g_fmt[key] = f; return f;
}
static void Txt(const std::wstring& s, Rc r, float size, Col c, int align = 0, bool bold = false) {
    if (s.empty()) return; IDWriteTextFormat* f = Fmt(size, bold, align); if (!f) return;
    SetC(c); g_rt->DrawText(s.c_str(), (UINT32)s.size(), f, DR(r), g_br, (D2D1_DRAW_TEXT_OPTIONS)(D2D1_DRAW_TEXT_OPTIONS_CLIP | D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT), DWRITE_MEASURING_MODE_NATURAL);
}
static float TxtW(const std::wstring& s, float size, bool bold) {
    if (s.empty()) return 0; IDWriteTextFormat* f = Fmt(size, bold, 0); if (!f) return 0;
    IDWriteTextLayout* l = nullptr; if (FAILED(g_dw->CreateTextLayout(s.c_str(), (UINT32)s.size(), f, 10000.f, 100.f, &l)) || !l) return 0;
    DWRITE_TEXT_METRICS m = {}; l->GetMetrics(&m); l->Release(); return m.widthIncludingTrailingWhitespace;
}

static void DiscardRT() {
    for (auto& kv : g_ico) if (kv.second.b) kv.second.b->Release();
    g_ico.clear(); { std::lock_guard<std::mutex> l(g_icoMx); g_icoReq.clear(); }
    if (g_wall) { g_wall->Release(); g_wall = nullptr; } g_wallTried.clear();
    if (g_mpArt) { g_mpArt->Release(); g_mpArt = nullptr; } g_mpArtVer = 0xffffffffu;
    if (g_br) { g_br->Release(); g_br = nullptr; } if (g_rt) { g_rt->Release(); g_rt = nullptr; }
}
static bool EnsureRT() {
    if (g_rt) return true;
    RECT rc; GetClientRect(g_hwnd, &rc);
    if (FAILED(g_d2d->CreateHwndRenderTarget(D2D1::RenderTargetProperties(),
        D2D1::HwndRenderTargetProperties(g_hwnd, D2D1::SizeU((UINT32)std::max(1L, rc.right), (UINT32)std::max(1L, rc.bottom))), &g_rt)) || !g_rt) return false;
    g_rt->SetDpi((float)g_dpi, (float)g_dpi); g_rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    if (FAILED(g_rt->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), &g_br)) || !g_br) { g_rt->Release(); g_rt = nullptr; return false; }
    return true;
}
static void UpdateWallpaper() {
    if (g_cfg.wallpaper == g_wallTried) return;
    g_wallTried = g_cfg.wallpaper; if (g_wall) { g_wall->Release(); g_wall = nullptr; }
    if (g_cfg.wallpaper.empty() || !g_wic) return;
    IWICBitmapDecoder* dec = nullptr; IWICBitmapFrameDecode* fr = nullptr; IWICFormatConverter* cv = nullptr;
    if (SUCCEEDED(g_wic->CreateDecoderFromFilename(g_cfg.wallpaper.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec)) && dec &&
        SUCCEEDED(dec->GetFrame(0, &fr)) && fr && SUCCEEDED(g_wic->CreateFormatConverter(&cv)) && cv &&
        SUCCEEDED(cv->Initialize(fr, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.f, WICBitmapPaletteTypeMedianCut)))
        g_rt->CreateBitmapFromWicBitmap(cv, nullptr, &g_wall);
    if (cv) cv->Release(); if (fr) fr->Release(); if (dec) dec->Release();
}
static ID2D1Bitmap* GetIcon(const std::wstring& key, float* fade) {
    *fade = 1.f; auto it = g_ico.find(key);
    if (it != g_ico.end()) {
        if (it->second.b) { *fade = Cl((float)((g_now - it->second.t0) / 0.3), 0.f, 1.f); if (*fade < 1.f) Kick(0.1); }
        return it->second.b;
    }
    IconPx px; bool have = false;
    {
        std::lock_guard<std::mutex> l(g_icoMx);
        auto d = g_icoDone.find(key);
        if (d != g_icoDone.end()) { px = std::move(d->second); g_icoDone.erase(d); have = true; }
        else if (g_icoReq.insert(key).second) { g_icoQ.push_back(key); SetEvent(g_icoEv); }
    }
    if (!have) return nullptr;
    IcoEnt e; e.t0 = g_now;
    if (px.w > 0 && g_rt)
        g_rt->CreateBitmap(D2D1::SizeU(px.w, px.h), px.px.data(), px.w * 4,
            D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)), &e.b);
    g_ico[key] = e; *fade = 0.f; Kick(0.3); return e.b;
}
static void DrawIcon(const std::wstring& key, Rc r, float alpha = 1.f) {
    float fade = 1; ID2D1Bitmap* b = GetIcon(key, &fade);
    if (b) g_rt->DrawBitmap(b, DR(r), alpha * fade * g_a, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    else FillRR(r, r.w() * 0.22f, Alpha(ThemeCol(T_DIM), 0.18f * alpha));
}

// --- хит-зоны и виджеты ---
static Hit* AddHit(Rc r, int id, int cur) {
    if (!g_hitsOn) return nullptr;
    Rc c{ std::max(r.l, g_clip.l), std::max(r.t, g_clip.t), std::min(r.r, g_clip.r), std::min(r.b, g_clip.b) };
    if (c.r <= c.l || c.b <= c.t) return nullptr;
    g_hits.push_back(Hit()); Hit& h = g_hits.back(); h.r = c; h.id = id; h.cur = cur; return &h;
}
static const Hit* HitAt(Pt p) { for (int i = (int)g_hits.size() - 1; i >= 0; --i) if (In(g_hits[i].r, p)) return &g_hits[i]; return nullptr; }
static float Hov(int id, bool on, double d = 0.18) { Tw& t = TW(id, 0); t.go(on ? 1.f : 0.f, d); return t.val(); }
static const Col kDark = { 0.05f, 0.06f, 0.10f, 1.f };

static void Button(int id, Rc r, const std::wstring& label, bool primary, std::function<void()> cb) {
    float h = Hov(id, g_hover == id); Tw& pr = TW(id, 2); pr.go(g_downId == id ? 1.f : 0.f, 0.1); float p = pr.val();
    Rc q = Infl(r, -p * 2.f);
    Col base = primary ? ThemeCol(T_ACC) : ThemeCol(T_CARD);
    Col fill = Mix(base, primary ? Col{ 1, 1, 1, 1 } : ThemeCol(T_ACC), h * (primary ? 0.2f : 0.3f));
    Shadow(q, 10, 6, 0.2f); FillRR(q, 10, fill);
    Txt(label, q, 14, primary ? kDark : ThemeCol(T_TXT), 1, true);
    if (Hit* ht = AddHit(r, id, 1)) ht->click = cb;
}
static void Toggle(int id, Rc r, bool v, std::function<void()> cb) {
    Tw& t = TW(id, 1); if (!t.inited) { t.inited = true; t.jump(v ? 1.f : 0.f); } t.go(v ? 1.f : 0.f, 0.22);
    float k = t.val(), h = Hov(id, g_hover == id);
    FillRR(r, r.h() / 2, Mix(C8(110, 116, 140, 0.55f), ThemeCol(T_ACC), k));
    float kr = r.h() / 2 - 4 + 1.5f * h, cx = Lerpf(r.l + r.h() / 2, r.r - r.h() / 2, k);
    Circle(cx, (r.t + r.b) / 2, kr, Col{ 1, 1, 1, 1 });
    if (Hit* ht = AddHit(Infl(r, 4), id, 1)) ht->click = cb;
}
static void Slider(int id, Rc r, float v01, std::function<void(float)> cb) {
    float cy = (r.t + r.b) / 2, h = Hov(id, g_hover == id || g_downId == id);
    FillRR(R(r.l, cy - 3, r.w(), 6), 3, Alpha(ThemeCol(T_DIM), 0.35f));
    FillRR(R(r.l, cy - 3, r.w() * v01, 6), 3, ThemeCol(T_ACC));
    float kx = r.l + r.w() * v01; Circle(kx, cy, 9 + 2 * h, Col{ 1, 1, 1, 1 }); Circle(kx, cy, 4, ThemeCol(T_ACC));
    if (Hit* ht = AddHit(Infl(r, 8), id, 1)) { Rc rr = r; ht->drag = [rr, cb](Pt p, int ph) { if (ph != 2) cb(Cl((p.x - rr.l) / rr.w(), 0.f, 1.f)); }; }
}
static void Field(int id, Rc r, const std::wstring& buf, const wchar_t* hint) {
    bool f = (g_focusId == id); Hov(id, g_hover == id); Tw& ft = TW(id, 1); ft.go(f ? 1.f : 0.f, 0.15); float k = ft.val();
    FillRR(r, 12, Alpha(Mix(ThemeCol(T_CARD), kDark, 0.25f), 0.92f));
    StrokeRR(r, 12, Mix(Alpha(ThemeCol(T_DIM), 0.3f), ThemeCol(T_ACC), k), 1.5f);
    Rc tr{ r.l + 14, r.t, r.r - 14, r.b };
    if (buf.empty()) Txt(hint, tr, 15, Alpha(ThemeCol(T_DIM), 0.75f), 0); else Txt(buf, tr, 15, ThemeCol(T_TXT), 0);
    if (f && fmodf((float)(g_now - g_focusT0), 1.06f) < 0.53f) {
        float w = std::min(TxtW(buf, 15, false), tr.w() - 4); SetC(ThemeCol(T_ACC));
        g_rt->FillRectangle(D2D1::RectF(tr.l + w + 1, r.t + 13, tr.l + w + 2.5f, r.b - 13), g_br);
    }
    if (Hit* ht = AddHit(r, id, 2)) ht->click = [id] { SetFocusField(id); };
}
static void Tile(int id, Rc r, const std::wstring& name, const std::wstring& key, std::function<void()> click, std::function<void()> right, bool sel = false) {
    float h = Hov(id, g_hover == id); Col acc = ThemeCol(T_ACC);
    Shadow(r, 16, 8 + 6 * h, 0.18f + 0.12f * h);
    FillRR(r, 16, Mix(Alpha(ThemeCol(T_CARD), 0.8f), Alpha(acc, 0.35f), std::max(h * 0.5f, sel ? 0.45f : 0.f))); StrokeRR(r, 16, Alpha(acc, 0.5f * h), 1.5f);
    if (sel) { StrokeRR(r, 16, Alpha(acc, 0.95f), 2.5f); Circle(r.r - 16, r.t + 16, 7, acc); }
    float is = 60 * (1 + 0.12f * h); DrawIcon(key, R(r.l + r.w() / 2 - is / 2, r.t + 16 + (60 - is) / 2, is, is));
    Txt(name, R(r.l + 10, r.b - 50, r.w() - 20, 34), 14, ThemeCol(T_TXT), 1);
    if (Hit* ht = AddHit(r, id, 1)) { ht->click = click; ht->right = right; }
}
static void MediaIcon(int kind, Rc r, Col c) {
    float cx = (r.l + r.r) / 2, cy = (r.t + r.b) / 2;
    if (kind == 0) { FillRR(R(cx - 9, cy - 7, 3, 14), 1, c); Pt t[3] = { { cx + 8, cy - 7 }, { cx + 8, cy + 7 }, { cx - 5, cy } }; Poly(t, 3, c); }
    else if (kind == 2) { Pt t[3] = { { cx - 8, cy - 7 }, { cx - 8, cy + 7 }, { cx + 5, cy } }; Poly(t, 3, c); FillRR(R(cx + 6, cy - 7, 3, 14), 1, c); }
    else { Pt t[3] = { { cx - 11, cy - 8 }, { cx - 11, cy + 8 }, { cx - 1, cy } }; Poly(t, 3, c); FillRR(R(cx + 3, cy - 8, 3, 16), 1, c); FillRR(R(cx + 8, cy - 8, 3, 16), 1, c); }
}

// ============================ 9b. МЕДИА (GSMTC) + ТЕКСТЫ (LRCLIB) ============================
namespace wmc = winrt::Windows::Media::Control;
namespace wsr = winrt::Windows::Storage::Streams;
struct LyrLine { double t; std::wstring s; int src = 0; };
struct MediaState {
    std::mutex m; bool has = false, playing = false;
    std::wstring title, artist, key; double pos = 0, dur = 0, posAt = 0;
    std::vector<uint8_t> art; unsigned artVer = 0;
    int lyrState = 0;   // 0 нет, 1 ищем, 2 найден, 3 не найден
    std::vector<LyrLine> lyr; bool synced = false; unsigned lyrVer = 0;
    std::wstring reqKey, reqArtist, reqTitle, genUrl; double reqDur = 0; unsigned reqVer = 0;
} g_med;
static std::atomic<int> g_medCmd(0);
static void MediaCmd(int c) { g_medCmd = c; if (g_medEv) SetEvent(g_medEv); }

static std::wstring Trim(std::wstring s) {
    while (!s.empty() && iswspace(s.back())) s.pop_back();
    size_t i = 0; while (i < s.size() && iswspace(s[i])) i++; return s.substr(i);
}
static std::wstring StripBrackets(const std::wstring& s) {
    std::wstring o; int d = 0;
    for (wchar_t c : s) { if (c == L'(' || c == L'[') d++; else if ((c == L')' || c == L']') && d > 0) d--; else if (!d) o += c; }
    std::wstring t = Trim(o); return t.empty() ? s : t;
}
static std::wstring FirstArtist(const std::wstring& a) { size_t p = a.find_first_of(L",&;"); return Trim(p == std::wstring::npos ? a : a.substr(0, p)); }
static std::string UrlEnc(const std::wstring& w) {
    std::string u = W2U(w), o; char b[4];
    for (unsigned char c : u) { if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += (char)c; else { snprintf(b, 4, "%%%02X", c); o += b; } }
    return o;
}
static const char* kGeniusDefaultToken = "";   // токен вписывается в config.json (geniusToken)
static std::string g_genToken;   // можно переопределить в config.json ("geniusToken")
static const wchar_t* kBrowserUA = L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36";
#ifndef WINHTTP_OPTION_DECOMPRESSION
#define WINHTTP_OPTION_DECOMPRESSION 118
#endif
#ifndef WINHTTP_DECOMPRESSION_FLAG_ALL
#define WINHTTP_DECOMPRESSION_FLAG_ALL 0x00000003
#endif
// Универсальный GET (http/https), с авто-распаковкой gzip/deflate и ограничением размера.
static bool HttpFetch(const std::wstring& url, const std::wstring& hdr, const wchar_t* ua, std::string& out, size_t maxBytes) {
    out.clear();
    wchar_t host[256] = L"", path[2048] = L"", extra[2048] = L"";
    URL_COMPONENTSW uc = {}; uc.dwStructSize = sizeof(uc);
    uc.lpszHostName = host; uc.dwHostNameLength = 256; uc.lpszUrlPath = path; uc.dwUrlPathLength = 2048; uc.lpszExtraInfo = extra; uc.dwExtraInfoLength = 2048;
    if (!WinHttpCrackUrl(url.c_str(), (DWORD)url.size(), 0, &uc)) return false;
    std::wstring wp = std::wstring(path) + extra; if (wp.empty()) wp = L"/";
    bool ok = false;
    HINTERNET s = WinHttpOpen(ua, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0); if (!s) return false;
    WinHttpSetTimeouts(s, 5000, 5000, 8000, 12000);
    HINTERNET c = WinHttpConnect(s, host, uc.nPort, 0);
    HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", wp.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0) : nullptr;
    if (r) {
        DWORD dc = WINHTTP_DECOMPRESSION_FLAG_ALL; WinHttpSetOption(r, WINHTTP_OPTION_DECOMPRESSION, &dc, sizeof(dc));
        if (WinHttpSendRequest(r, hdr.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : hdr.c_str(), hdr.empty() ? 0 : (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(r, nullptr)) {
            DWORD code = 0, sz = sizeof(code);
            WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &sz, WINHTTP_NO_HEADER_INDEX);
            if (code == 200) {
                ok = true;
                for (;;) {
                    DWORD av = 0; if (!WinHttpQueryDataAvailable(r, &av) || !av) break;
                    if (out.size() + av > maxBytes) { ok = false; break; }
                    std::string bf(av, '\0'); DWORD rd = 0; if (!WinHttpReadData(r, &bf[0], av, &rd) || !rd) break; out.append(bf, 0, rd);
                }
            }
        }
    }
    if (r) WinHttpCloseHandle(r); if (c) WinHttpCloseHandle(c); WinHttpCloseHandle(s);
    return ok && !out.empty();
}
static std::string HttpGet(const std::string& path) {   // LRCLIB
    std::string o; HttpFetch(L"https://lrclib.net" + U2W(path), L"", L"DimaKorzanShell/1.1", o, 2u << 20); return o;
}
static bool LrcParse(const std::string& body, std::wstring& plain, std::wstring& synced) {
    if (body.empty()) return false; std::wstring w = U2W(body); JParser ps(w); JV root; if (!ps.parse(root, 0)) return false;
    auto pick = [&](const JV& o) {
        const JV* a = o.get(L"syncedLyrics"); const JV* b = o.get(L"plainLyrics"); std::wstring sy, pl;
        if (a && a->t == JV::Str) sy = a->s;
        if (b && b->t == JV::Str) pl = b->s;
        if (sy.empty() && pl.empty()) return false; synced = sy; plain = pl; return true;
    };
    if (root.t == JV::Obj) return pick(root);
    if (root.t == JV::Arr) {
        for (auto& e : root.a) if (e.t == JV::Obj) { const JV* a = e.get(L"syncedLyrics"); if (a && a->t == JV::Str && !a->s.empty() && pick(e)) return true; }
        for (auto& e : root.a) if (e.t == JV::Obj && pick(e)) return true;
    }
    return false;
}
static bool FetchLyrics(const std::wstring& artist, const std::wstring& title, double dur, std::wstring& plain, std::wstring& synced) {   // запасной путь (LRCLIB)
    std::wstring ct = StripBrackets(title), fa = FirstArtist(artist);
    if (dur > 0 && LrcParse(HttpGet("/api/get?artist_name=" + UrlEnc(artist) + "&track_name=" + UrlEnc(title) + "&duration=" + std::to_string((int)(dur + 0.5))), plain, synced)) return true;
    if (LrcParse(HttpGet("/api/search?track_name=" + UrlEnc(ct) + "&artist_name=" + UrlEnc(fa)), plain, synced)) return true;
    if (LrcParse(HttpGet("/api/search?q=" + UrlEnc(fa + L" " + ct)), plain, synced)) return true;
    return false;
}
// ---------- Genius: чистая логика (разбор HTML/JSON, подбор лучшего совпадения) ----------
static void AppendUtf8(std::string& o, unsigned cp) {
    if (cp < 0x80) o += (char)cp;
    else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x110000) { o += (char)(0xF0 | (cp >> 18)); o += (char)(0x80 | ((cp >> 12) & 0x3F)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
}
static std::string HtmlDecode(const std::string& s) {
    std::string o; o.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '&') { o += s[i]; continue; }
        size_t e = s.find(';', i); if (e == std::string::npos || e - i > 10) { o += s[i]; continue; }
        std::string ent = s.substr(i + 1, e - i - 1); bool ok = true;
        if (ent == "amp") o += '&'; else if (ent == "lt") o += '<'; else if (ent == "gt") o += '>';
        else if (ent == "quot") o += '"'; else if (ent == "apos") o += '\''; else if (ent == "nbsp") o += ' ';
        else if (ent.size() > 1 && ent[0] == '#') {
            unsigned cp = 0; bool hex = (ent[1] == 'x' || ent[1] == 'X');
            const char* p = ent.c_str() + (hex ? 2 : 1); char* endp = nullptr; cp = (unsigned)strtoul(p, &endp, hex ? 16 : 10);
            if (endp && *endp == 0 && endp != p && cp) AppendUtf8(o, cp); else ok = false;
        } else ok = false;
        if (ok) i = e; else o += s[i];
    }
    return o;
}
// Достаёт текст песни из HTML страницы genius.com: блоки data-lyrics-container, <br> -> перевод строки.
static std::string ExtractGeniusLyrics(const std::string& h) {
    std::string out; size_t pos = 0;
    for (;;) {
        size_t k = h.find("data-lyrics-container=\"true\"", pos); if (k == std::string::npos) break;
        size_t gt = h.find('>', k); if (gt == std::string::npos) break;
        size_t i = gt + 1; int depth = 1, skip = 0;
        while (i < h.size() && depth > 0) {
            if (h[i] == '<') {
                size_t e = h.find('>', i); if (e == std::string::npos) { i = h.size(); break; }
                bool closing = (i + 1 < h.size() && h[i + 1] == '/');
                size_t ns = i + 1 + (closing ? 1 : 0), ne = ns; std::string nm;
                while (ne < e && isalnum((unsigned char)h[ne])) { nm += (char)tolower((unsigned char)h[ne]); ++ne; }
                if (nm == "div") {
                    if (closing) { --depth; if (skip > 0) --skip; }
                    else { ++depth; if (skip > 0 || h.substr(i, e - i).find("data-exclude-from-selection") != std::string::npos) ++skip; }
                } else if (nm == "br") { if (!skip) out += '\n'; }
                else if ((nm == "script" || nm == "style") && !closing) {
                    size_t ce = h.find(nm == "script" ? "</script>" : "</style>", e); if (ce == std::string::npos) { i = h.size(); break; } e = h.find('>', ce);
                    if (e == std::string::npos) { i = h.size(); break; }
                }
                i = e + 1;
            } else {
                size_t nx = h.find('<', i); if (nx == std::string::npos) nx = h.size();
                if (!skip) out += HtmlDecode(h.substr(i, nx - i));
                i = nx;
            }
        }
        out += '\n'; pos = i;
    }
    // чистка: убираем строки-заголовки вида [Verse 1], лишние пустые строки
    std::string res; std::vector<std::string> lines; size_t st = 0;
    for (size_t i = 0; i <= out.size(); ++i) if (i == out.size() || out[i] == '\n') { lines.push_back(out.substr(st, i - st)); st = i + 1; }
    bool lastBlank = true;
    for (auto& l0 : lines) {
        std::string l = l0; while (!l.empty() && (l.back() == '\r' || l.back() == ' ' || l.back() == '\t')) l.pop_back();
        size_t a = 0; while (a < l.size() && (l[a] == ' ' || l[a] == '\t')) ++a; l = l.substr(a);
        if (l.size() >= 2 && l.front() == '[' && l.back() == ']') continue;
        if (l.empty()) { if (lastBlank) continue; lastBlank = true; res += '\n'; continue; }
        lastBlank = false; res += l; res += '\n';
    }
    while (!res.empty() && res.back() == '\n') res.pop_back();
    return res;
}

static bool IsAlnumW(wchar_t c) { return IsCharAlphaNumericW(c) != 0; }
static std::wstring NormKey(const std::wstring& s) { std::wstring o; for (wchar_t c : Lower(s)) if (IsAlnumW(c)) o += c; return o; }
static void CutWord(std::wstring& t, const wchar_t* w) {   // убрать слово/фразу целиком (без учёта регистра)
    std::wstring lw = Lower(t), lk = Lower(w); size_t n = lk.size(), p = 0;
    while ((p = lw.find(lk, p)) != std::wstring::npos) {
        bool l = (p == 0 || !IsAlnumW(lw[p - 1])), r = (p + n >= lw.size() || !IsAlnumW(lw[p + n]));
        if (l && r) { t.erase(p, n); lw.erase(p, n); } else p += n;
    }
}
static std::wstring CleanTitle(std::wstring t) {
    t = StripBrackets(t);
    static const wchar_t* junk[] = { L"official music video", L"official lyric video", L"official video", L"official audio", L"lyric video", L"lyrics video",
        L"music video", L"lyrics", L"audio", L"remastered", L"visualizer", L"клип", L"премьера клипа", L"hd", L"4k", L"mv" };
    for (auto j : junk) CutWord(t, j);
    static const wchar_t* feat[] = { L" feat.", L" feat ", L" ft.", L" ft ", L" featuring ", L" prod.", L" prod " };
    std::wstring lw = Lower(t); size_t cut = std::wstring::npos;
    for (auto f : feat) { size_t p = lw.find(f); if (p != std::wstring::npos && p > 0 && p < cut) cut = p; }
    if (cut != std::wstring::npos) t.resize(cut);
    t = Trim(t);
    while (!t.empty() && (t.back() == L'-' || t.back() == L'|' || t.back() == L'–' || t.back() == L'—' || t.back() == L' ')) t.pop_back();
    return t;
}
struct GeniusSong { std::wstring title, artist, url, artUrl; bool ok = false; };
static bool GeniusPick(const JV& root, const std::wstring& nt, const std::wstring& na, GeniusSong& out) {
    const JV* resp = root.get(L"response"); if (!resp || resp->t != JV::Obj) return false;
    const JV* hits = resp->get(L"hits"); if (!hits || hits->t != JV::Arr) return false;
    auto gs = [](const JV& o, const wchar_t* k) { const JV* v = o.get(k); return (v && v->t == JV::Str) ? v->s : std::wstring(); };
    int best = -1; const JV* bestR = nullptr;
    for (auto& h : hits->a) {
        if (h.t != JV::Obj) continue;
        const JV* ty = h.get(L"type"); if (ty && ty->t == JV::Str && ty->s != L"song") continue;
        const JV* r = h.get(L"result"); if (!r || r->t != JV::Obj) continue;
        std::wstring ht = NormKey(gs(*r, L"title")), hf = NormKey(gs(*r, L"full_title")), ha;
        if (const JV* pa = r->get(L"primary_artist")) if (pa->t == JV::Obj) ha = NormKey(gs(*pa, L"name"));
        if (ha.compare(0, 6, L"genius") == 0) continue;      // «Genius Translations», «Genius Romanizations» — не оригинал
        int ts = 0, as = 0;
        if (!nt.empty() && !ht.empty()) { if (ht == nt) ts = 6; else if (ht.find(nt) != std::wstring::npos || nt.find(ht) != std::wstring::npos) ts = 3; }
        if (!na.empty()) {
            if (ha == na) as = 4;
            else if (!ha.empty() && (ha.find(na) != std::wstring::npos || na.find(ha) != std::wstring::npos)) as = 3;
            else if (hf.find(na) != std::wstring::npos) as = 2;
        }
        if (ts < 3 || (!na.empty() && as < 2)) continue;
        if (ts + as > best) { best = ts + as; bestR = r; }
    }
    if (!bestR) return false;
    out.title = gs(*bestR, L"title"); out.url = gs(*bestR, L"url"); out.artUrl = gs(*bestR, L"song_art_image_url");
    if (out.artUrl.empty() || out.artUrl.find(L"default_cover") != std::wstring::npos) out.artUrl = gs(*bestR, L"header_image_url");
    if (out.artUrl.find(L"default_cover") != std::wstring::npos) out.artUrl.clear();
    if (const JV* pa = bestR->get(L"primary_artist")) if (pa->t == JV::Obj) out.artist = gs(*pa, L"name");
    out.ok = !out.url.empty(); return out.ok;
}
// Переносит длинные строки текста по ширине (для узкой панели), сохраняя связь с исходной строкой (src)
static std::vector<LyrLine> WrapLyr(const std::vector<LyrLine>& in, float maxW, const std::function<float(const std::wstring&)>& width) {
    std::vector<LyrLine> out;
    for (size_t k = 0; k < in.size(); ++k) {
        const LyrLine& l = in[k]; int src = (int)k;
        auto push = [&](const std::wstring& s) { LyrLine x; x.t = l.t; x.s = s; x.src = src; out.push_back(x); };
        if (l.s.empty() || width(l.s) <= maxW) { push(l.s); continue; }
        std::wstring cur; std::vector<std::wstring> words; size_t i = 0;
        while (i <= l.s.size()) { size_t j = l.s.find(L' ', i); if (j == std::wstring::npos) j = l.s.size(); if (j > i) words.push_back(l.s.substr(i, j - i)); i = j + 1; }
        for (auto& w : words) {
            std::wstring tryS = cur.empty() ? w : cur + L" " + w;
            if (width(tryS) <= maxW) { cur = tryS; continue; }
            if (!cur.empty()) { push(cur); cur.clear(); }
            std::wstring rest = w;
            while (width(rest) > maxW && rest.size() > 1) {    // слово шире строки — режем по символам
                size_t lo = 1, hi = rest.size() - 1;
                while (lo < hi) { size_t mid = (lo + hi + 1) / 2; if (width(rest.substr(0, mid)) <= maxW) lo = mid; else hi = mid - 1; }
                push(rest.substr(0, lo)); rest = rest.substr(lo);
            }
            cur = rest;
        }
        if (!cur.empty()) push(cur);
    }
    return out;
}

// Поиск песни через Genius API (Bearer-токен). Пробуем несколько вариантов запроса: «исполнитель название», «Артист - Название» из заголовка видео, только название.
static bool GeniusFind(const std::wstring& artist, const std::wstring& title, GeniusSong& out) {
    if (g_genToken.empty()) return false;
    std::wstring hdr = L"Authorization: Bearer " + U2W(g_genToken) + L"\r\nAccept: application/json\r\n";
    auto run = [&](const std::wstring& q, const std::wstring& nt, const std::wstring& na) -> bool {
        std::string body; if (!HttpFetch(L"https://api.genius.com/search?q=" + U2W(UrlEnc(q)), hdr, kBrowserUA, body, 1u << 20)) return false;
        std::wstring w = U2W(body); JParser ps(w); JV root; if (!ps.parse(root, 0)) return false;
        return GeniusPick(root, nt, na, out);
    };
    std::wstring ct = CleanTitle(title), fa = FirstArtist(artist);
    if (ct.empty()) return false;
    if (run(fa + L" " + ct, NormKey(ct), NormKey(fa))) return true;
    size_t d = title.find(L" - "); if (d == std::wstring::npos) d = title.find(L" – ");
    if (d != std::wstring::npos) {
        std::wstring l1 = Trim(title.substr(0, d)), r1 = CleanTitle(title.substr(d + 3));
        if (!l1.empty() && !r1.empty() && run(l1 + L" " + r1, NormKey(r1), NormKey(FirstArtist(l1)))) return true;
    }
    return run(ct, NormKey(ct), L"");
}
struct FetchResult { std::wstring plain, synced, genUrl; std::vector<uint8_t> art; };
// Genius: обложка + текст страницы. LRCLIB (по точным данным Genius) — только чтобы добавить построчную синхронизацию.
static bool FetchLyricsAll(const std::wstring& artist, const std::wstring& title, double dur, FetchResult& fr) {
    GeniusSong gs;
    if (GeniusFind(artist, title, gs)) {
        fr.genUrl = gs.url; std::string img, html;
        if (!gs.artUrl.empty() && HttpFetch(gs.artUrl, L"", kBrowserUA, img, 8u << 20) && img.size() > 512) fr.art.assign(img.begin(), img.end());
        if (HttpFetch(gs.url, L"Accept: text/html,application/xhtml+xml\r\nAccept-Language: en-US,en;q=0.9\r\n", kBrowserUA, html, 10u << 20)) fr.plain = U2W(ExtractGeniusLyrics(html));
        std::string q = "/api/get?artist_name=" + UrlEnc(gs.artist) + "&track_name=" + UrlEnc(gs.title), body;
        if (dur > 0) body = HttpGet(q + "&duration=" + std::to_string((int)(dur + 0.5)));
        if (body.empty()) body = HttpGet(q);
        std::wstring p2, s2; if (LrcParse(body, p2, s2) && !s2.empty()) fr.synced = s2;
        if (fr.plain.empty() && !p2.empty()) fr.plain = p2;
        if (fr.synced.empty()) { std::wstring p3, s3; if (FetchLyrics(artist, title, dur, p3, s3) && !s3.empty()) { fr.synced = s3; if (fr.plain.empty()) fr.plain = p3; } }
        if (!fr.plain.empty() || !fr.synced.empty()) return true;
    }
    return FetchLyrics(artist, title, dur, fr.plain, fr.synced);
}
static void ParseLyrics(const std::wstring& synced, const std::wstring& plain, std::vector<LyrLine>& out, bool& isSynced) {
    out.clear(); isSynced = false;
    auto split = [](const std::wstring& t) {
        std::vector<std::wstring> v; size_t i = 0;
        while (i <= t.size()) { size_t j = t.find(L'\n', i); if (j == std::wstring::npos) j = t.size(); std::wstring l = t.substr(i, j - i); if (!l.empty() && l.back() == L'\r') l.pop_back(); v.push_back(l); i = j + 1; }
        return v;
    };
    if (!synced.empty()) {
        for (auto& l : split(synced)) {
            if (l.size() < 5 || l[0] != L'[') continue; size_t e = l.find(L']'); if (e == std::wstring::npos) continue;
            int mm = 0; double ss = 0; if (swscanf(l.c_str() + 1, L"%d:%lf", &mm, &ss) != 2) continue;
            out.push_back(LyrLine{ mm * 60.0 + ss, Trim(l.substr(e + 1)) });
        }
        if (!out.empty()) { isSynced = true; return; }
    }
    for (auto& l : split(plain)) out.push_back(LyrLine{ -1.0, l });
}
static DWORD WINAPI LyricsThread(LPVOID) {
    try { winrt::init_apartment(winrt::apartment_type::multi_threaded); } catch (...) {}
    HANDLE ev[2] = { g_lyrEv, g_quitEv };
    for (;;) {
        if (WaitForMultipleObjects(2, ev, FALSE, INFINITE) == WAIT_OBJECT_0 + 1) break;
        for (;;) {
            std::wstring key, artist, title; double dur = 0; unsigned ver = 0;
            { std::lock_guard<std::mutex> l(g_med.m); key = g_med.reqKey; artist = g_med.reqArtist; title = g_med.reqTitle; dur = g_med.reqDur; ver = g_med.reqVer; }
            FetchResult fr; bool found = false; std::vector<LyrLine> lines; bool isSy = false;
            try {   // любая ошибка сети/разбора не должна ронять оболочку
                found = !title.empty() && FetchLyricsAll(artist, title, dur, fr);
                if (found) ParseLyrics(fr.synced, fr.plain, lines, isSy);
                if (lines.size() > 500) lines.resize(500);
            } catch (...) { found = false; lines.clear(); fr = FetchResult(); }
            bool redo = false;
            {
                std::lock_guard<std::mutex> l(g_med.m);
                if (ver != g_med.reqVer) redo = true;
                else if (g_med.key == key) {
                    g_med.lyr.swap(lines); g_med.synced = isSy; g_med.lyrState = (found && !g_med.lyr.empty()) ? 2 : 3; g_med.lyrVer++;
                    g_med.genUrl = fr.genUrl;
                    if (!fr.art.empty()) { g_med.art.swap(fr.art); g_med.artVer++; }   // обложка с Genius заменяет миниатюру плеера
                }
            }
            if (g_hwnd) PostMessageW(g_hwnd, WM_ICON, 0, 0);
            if (!redo) break;
        }
        if (WaitForSingleObject(g_quitEv, 0) == WAIT_OBJECT_0) break;
    }
    return 0;
}
static void MediaPoll(const wmc::GlobalSystemMediaTransportControlsSessionManager& mgr) {
    auto ses = mgr.GetCurrentSession();
    if (!ses) { { std::lock_guard<std::mutex> l(g_med.m); g_med.has = false; g_med.playing = false; } if (g_hwnd) PostMessageW(g_hwnd, WM_ICON, 0, 0); return; }
    int cmd = g_medCmd.exchange(0);
    if (cmd == 1) ses.TrySkipPreviousAsync().get(); else if (cmd == 2) ses.TryTogglePlayPauseAsync().get(); else if (cmd == 3) ses.TrySkipNextAsync().get();
    auto props = ses.TryGetMediaPropertiesAsync().get();
    std::wstring title = props.Title().c_str(), artist = props.Artist().c_str();
    if (artist.empty()) { size_t p = title.find(L" - "); if (p != std::wstring::npos) { artist = title.substr(0, p); title = title.substr(p + 3); } }
    if (EndsWithI(artist, L" - Topic")) artist.resize(artist.size() - 8);
    bool playing = ses.GetPlaybackInfo().PlaybackStatus() == wmc::GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;
    auto tl = ses.GetTimelineProperties();
    double pos = std::chrono::duration<double>(tl.Position()).count(), dur = std::chrono::duration<double>(tl.EndTime() - tl.StartTime()).count();
    if (playing) { try { double age = std::chrono::duration<double>(winrt::clock::now() - tl.LastUpdatedTime()).count(); if (age > 0 && age < 30) pos += age; } catch (...) {} }   // позиция в GSMTC обновляется редко — добавляем возраст отметки
    std::wstring key = artist + L"|" + title, curKey; bool artEmpty;
    { std::lock_guard<std::mutex> l(g_med.m); curKey = g_med.key; artEmpty = g_med.art.empty(); }
    static int retry = 0; bool newKey = key != curKey; if (newKey) retry = 0;
    bool needArt = newKey || (artEmpty && retry++ < 8);
    std::vector<uint8_t> art;
    if (needArt) {
        try {
            auto ref = props.Thumbnail();
            if (ref) {
                auto st = ref.OpenReadAsync().get(); uint32_t sz = (uint32_t)st.Size();
                if (sz > 0 && sz < (8u << 20)) { wsr::DataReader rd(st.GetInputStreamAt(0)); rd.LoadAsync(sz).get(); art.resize(sz); rd.ReadBytes(art); }
            }
        } catch (...) { art.clear(); }
    }
    bool startReq = false;
    {
        std::lock_guard<std::mutex> l(g_med.m);
        g_med.has = !(title.empty() && artist.empty()); g_med.playing = playing; g_med.pos = pos; g_med.dur = dur; g_med.posAt = NowSec();
        if (needArt && (newKey || !art.empty())) { g_med.art.swap(art); g_med.artVer++; }
        if (newKey) {
            g_med.key = key; g_med.title = title; g_med.artist = artist; g_med.genUrl.clear(); g_med.lyr.clear(); g_med.synced = false; g_med.lyrState = title.empty() ? 0 : 1; g_med.lyrVer++;
            g_med.reqKey = key; g_med.reqArtist = artist; g_med.reqTitle = title; g_med.reqDur = dur; g_med.reqVer++; startReq = true;
        }
    }
    if (startReq && g_lyrEv) SetEvent(g_lyrEv);
    if (g_hwnd) PostMessageW(g_hwnd, WM_ICON, 0, 0);
}
static DWORD WINAPI MediaThread(LPVOID) {
    try { winrt::init_apartment(winrt::apartment_type::multi_threaded); } catch (...) { return 0; }
    wmc::GlobalSystemMediaTransportControlsSessionManager mgr{ nullptr };
    HANDLE ev[2] = { g_quitEv, g_medEv };
    for (;;) {
        if (!mgr) { try { mgr = wmc::GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get(); } catch (...) {} }
        if (mgr) { try { MediaPoll(mgr); } catch (...) {} }
        if (WaitForMultipleObjects(2, ev, FALSE, 250) == WAIT_OBJECT_0) break;
    }
    return 0;
}

// --- UI медиа-панели (слева внизу: при наведении раскрывается) ---
static Tw g_mpT, g_lyrTw; static bool g_mpOpen = false; static int g_mpTab = 0;
static float g_lyrScr = 0, g_lyrMax = 0; static Rc g_mpLyrView = { 0, 0, 0, 0 };
static std::vector<LyrLine> g_mpLyr; static unsigned g_mpLyrVer = 0xffffffffu; static bool g_mpSynced = false; static int g_mpLyrState = 0;

static void UpdateMpArt(const std::vector<uint8_t>& b) {
    if (g_mpArt) { g_mpArt->Release(); g_mpArt = nullptr; }
    if (b.empty() || !g_wic || !g_rt) return;
    IStream* st = SHCreateMemStream(b.data(), (UINT)b.size()); if (!st) return;
    IWICBitmapDecoder* dec = nullptr; IWICBitmapFrameDecode* fr = nullptr; IWICFormatConverter* cv = nullptr;
    if (SUCCEEDED(g_wic->CreateDecoderFromStream(st, nullptr, WICDecodeMetadataCacheOnLoad, &dec)) && dec && SUCCEEDED(dec->GetFrame(0, &fr)) && fr &&
        SUCCEEDED(g_wic->CreateFormatConverter(&cv)) && cv &&
        SUCCEEDED(cv->Initialize(fr, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.f, WICBitmapPaletteTypeMedianCut)))
        g_rt->CreateBitmapFromWicBitmap(cv, nullptr, &g_mpArt);
    if (cv) cv->Release(); if (fr) fr->Release(); if (dec) dec->Release(); st->Release();
}
static void DrawArt(Rc r, float rad) {
    if (!g_mpArt) { FillRR(r, rad, Alpha(ThemeCol(T_CARD), 0.9f)); Txt(L"♪", r, r.w() * 0.4f, ThemeCol(T_DIM), 1); return; }
    D2D1_SIZE_F sz = g_mpArt->GetSize(); float s = std::max(r.w() / sz.width, r.h() / sz.height), w = sz.width * s, h = sz.height * s;
    ID2D1RoundedRectangleGeometry* geo = nullptr;
    if (FAILED(g_d2d->CreateRoundedRectangleGeometry(D2D1::RoundedRect(DR(r), rad, rad), &geo)) || !geo) return;
    g_rt->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), geo), nullptr);
    g_rt->DrawBitmap(g_mpArt, D2D1::RectF(r.l + (r.w() - w) / 2, r.t + (r.h() - h) / 2, r.l + (r.w() + w) / 2, r.t + (r.h() + h) / 2), g_a, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    g_rt->PopLayer(); geo->Release();
}
static std::vector<LyrLine> WrapLyrUi(const std::vector<LyrLine>& in, float maxW) {
    return WrapLyr(in, maxW, [](const std::wstring& s) { return TxtW(s, 16, true); });
}
static std::wstring MSS(double s) { if (s < 0) s = 0; int t = (int)s; wchar_t b[16]; swprintf(b, 16, L"%d:%02d", t / 60, t % 60); return b; }

static void DrawMedia() {
    std::wstring title, artist, genUrl; bool has, playing; double pos, dur, posAt; bool newArt = false; std::vector<uint8_t> art; unsigned artVer; std::vector<LyrLine> lyrRaw; bool needWrap = false;
    {
        std::lock_guard<std::mutex> l(g_med.m);
        has = g_med.has; playing = g_med.playing; title = g_med.title; artist = g_med.artist; pos = g_med.pos; dur = g_med.dur; posAt = g_med.posAt; artVer = g_med.artVer;
        if (artVer != g_mpArtVer) { art = g_med.art; newArt = true; }
        genUrl = g_med.genUrl;
        if (g_med.lyrVer != g_mpLyrVer) { lyrRaw = g_med.lyr; needWrap = true; g_mpSynced = g_med.synced; g_mpLyrVer = g_med.lyrVer; g_lyrScr = 0; g_lyrTw.jump(0); }
        g_mpLyrState = g_med.lyrState;
    }
    if (needWrap) g_mpLyr = WrapLyrUi(lyrRaw, 306.f);
    if (newArt) { UpdateMpArt(art); g_mpArtVer = artVer; }
    g_mpLyrView = Rc{ 0, 0, 0, 0 };
    if (!has || g_startOpen || g_start.val() > 0.02f) { g_mpOpen = false; g_mpT.go(0, 0.15); return; }

    const float W0 = 250, H0 = 72, W1 = 340, H1 = 440, ax = 16, ab = g_H - TB_H - 16;
    float v = Cl(g_mpT.val(), 0.f, 1.f), w = Lerpf(W0, W1, v), h = Lerpf(H0, H1, v); Rc r = R(ax, ab - h, w, h);
    g_mpOpen = !g_modalOpen && !g_ctxOpen && !g_dragging && In(Infl(r, 6), g_mouse);
    g_mpT.go(g_mpOpen ? 1.f : 0.f, 0.3, E_CUBIC);
    Col txt = ThemeCol(T_TXT), dim = ThemeCol(T_DIM), acc = ThemeCol(T_ACC);
    Shadow(r, 20, 18, 0.45f);
    FillRR(r, 20, Alpha(Mix(ThemeCol(T_PANEL), ThemeCol(T_BG1), 0.35f), Cl(g_cfg.opacity + 0.15f, 0.4f, 0.98f)));
    StrokeRR(r, 20, Alpha(txt, 0.08f));
    AddHit(r, ID_MP_PANEL, 0);
    double p = pos + (playing ? (NowSec() - posAt) : 0.0); if (dur > 0 && p > dur) p = dur;
    float oa = g_a, on = g_hitsOn;

    float ac = Cl(1.f - v * 2.2f, 0.f, 1.f);   // свёрнутый вид
    if (ac > 0.01f) {
        g_a = oa * ac; DrawArt(R(r.l + 8, r.b - 64, 56, 56), 12);
        Txt(title, R(r.l + 72, r.b - 62, 130, 28), 14, txt, 0, true); Txt(artist, R(r.l + 72, r.b - 36, 130, 24), 12, dim, 0);
        for (int i = 0; i < 3; ++i) {
            float bh = playing ? 6 + 14 * fabsf(sinf((float)g_now * (3.f + i * 1.3f) + i)) : 6.f;
            FillRR(R(r.r - 36 + i * 8, r.b - 36 - bh / 2, 4, bh), 2, acc);
        }
        if (playing) Kick(0.1);
        g_a = oa;
    }
    float ae = Cl(v * 2.2f - 1.2f, 0.f, 1.f);  // раскрытый вид
    if (ae > 0.01f) {
        g_a = oa * ae; g_hitsOn = (bool)on && v > 0.9f;
        Rc seg = R(r.l + 16, r.t + 14, r.w() - 32, 40); FillRR(seg, 12, Alpha(ThemeCol(T_CARD), 0.8f));
        Tw& tt = TW(ID_MP_T0, 1); tt.go((float)g_mpTab, 0.25); float tv = tt.val(); float half = (seg.w() - 8) / 2;
        FillRR(R(seg.l + 4 + tv * half, seg.t + 4, half, 32), 9, acc);
        Txt(L"Плеер", R(seg.l + 4, seg.t + 4, half, 32), 14, Mix(kDark, txt, tv), 1, true);
        Txt(L"Текст", R(seg.l + 4 + half, seg.t + 4, half, 32), 14, Mix(txt, kDark, tv), 1, true);
        if (Hit* ht = AddHit(R(seg.l + 4, seg.t + 4, half, 32), ID_MP_T0, 1)) ht->click = [] { g_mpTab = 0; };
        if (Hit* ht = AddHit(R(seg.l + 4 + half, seg.t + 4, half, 32), ID_MP_T1, 1)) ht->click = [] { g_mpTab = 1; };
        float aP = ae * (1.f - tv), aL = ae * tv; bool hon = g_hitsOn;
        if (aP > 0.01f) {
            g_a = oa * aP; g_hitsOn = hon && g_mpTab == 0; float cx = r.l + r.w() / 2;
            Rc ar = R(cx - 95, r.t + 68, 190, 190); Shadow(ar, 16, 14, 0.4f); DrawArt(ar, 16);
            Txt(title, R(r.l + 20, r.t + 266, r.w() - 40, 28), 17, txt, 1, true); Txt(artist, R(r.l + 20, r.t + 294, r.w() - 40, 22), 13, dim, 1);
            Rc bar = R(r.l + 24, r.t + 334, r.w() - 48, 6); FillRR(bar, 3, Alpha(dim, 0.35f));
            FillRR(R(bar.l, bar.t, bar.w() * Cl(dur > 0 ? (float)(p / dur) : 0.f, 0.f, 1.f), 6), 3, acc);
            Txt(MSS(p), R(r.l + 24, r.t + 344, 80, 18), 11, dim, 0); Txt(dur > 0 ? MSS(dur) : std::wstring(), R(r.r - 104, r.t + 344, 80, 18), 11, dim, 2);
            for (int i = 0; i < 3; ++i) {
                int id = ID_MP_PREV + i; float rad = i == 1 ? 26.f : 20.f, bx = cx + (i - 1) * 72.f, by = r.t + 398; float hv = Hov(id, g_hover == id);
                Rc br = R(bx - rad, by - rad, rad * 2, rad * 2);
                Circle(bx, by, rad, i == 1 ? Mix(acc, Col{ 1, 1, 1, 1 }, hv * 0.2f) : Alpha(acc, 0.1f + 0.3f * hv));
                if (i == 1) {
                    if (playing) { FillRR(R(bx - 8, by - 9, 5, 18), 1.5f, kDark); FillRR(R(bx + 3, by - 9, 5, 18), 1.5f, kDark); }
                    else { Pt t[3] = { { bx - 6, by - 10 }, { bx - 6, by + 10 }, { bx + 10, by } }; Poly(t, 3, kDark); }
                } else MediaIcon(i, br, txt);
                if (Hit* ht = AddHit(br, id, 1)) { int c = i == 0 ? 1 : i == 1 ? 2 : 3; ht->click = [c] { MediaCmd(c); }; }
            }
        }
        if (aL > 0.01f) {
            g_a = oa * aL; g_hitsOn = hon && g_mpTab == 1;
            Txt((artist.empty() ? title : title + L" — " + artist) + (g_mpSynced || g_mpLyr.empty() ? L"" : L"  · без тайминга"), R(r.l + 20, r.t + 62, r.w() - 40, 24), 12, dim, 0);
            Rc vw = R(r.l + 16, r.t + 90, r.w() - 32, r.h() - 90 - 56); if (g_mpTab == 1) g_mpLyrView = vw;
            const float LH = 28;
            if (g_mpLyrState == 1) Txt(L"Ищу текст…", vw, 14, dim, 1);
            else if (g_mpLyr.empty()) Txt(L"Текст не найден", vw, 14, dim, 1);
            else {
                int n = (int)g_mpLyr.size(), cur = -1, curSrc = -1;
                if (g_mpSynced) for (int i = 0; i < n; ++i) { if (g_mpLyr[i].t <= p + 0.6) cur = i; else break; }
                if (cur >= 0) { curSrc = g_mpLyr[cur].src; while (cur > 0 && g_mpLyr[cur - 1].src == curSrc) --cur; }
                g_lyrMax = std::max(0.f, n * LH - vw.h());
                if (g_mpSynced) { g_lyrTw.go(cur < 0 ? 0.f : Cl(cur * LH - vw.h() / 2 + LH / 2, 0.f, g_lyrMax), 0.25); g_lyrScr = g_lyrTw.val(); }
                else g_lyrScr = Cl(g_lyrScr, 0.f, g_lyrMax);
                g_rt->PushAxisAlignedClip(DR(vw), D2D1_ANTIALIAS_MODE_ALIASED);
                for (int i = 0; i < n; ++i) {
                    float y = vw.t + i * LH - g_lyrScr; if (y + LH < vw.t || y > vw.b) continue;
                    bool c = (curSrc >= 0 && g_mpLyr[i].src == curSrc); Col col = g_mpSynced ? (c ? txt : Alpha(dim, 0.8f)) : txt;
                    Txt(g_mpLyr[i].s, R(vw.l, y, vw.w(), LH), c ? 16.f : 13.f, col, 1, c);
                }
                g_rt->PopAxisAlignedClip();
            }
            std::wstring q = artist + L" " + title, gu = genUrl;
            Button(ID_MP_GENIUS, R(r.l + 16, r.b - 48, r.w() - 32, 34), L"Открыть в Genius", false, [q, gu] {
                std::wstring url = gu.empty() ? U2W("https://genius.com/search?q=" + UrlEnc(q)) : gu; ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL); });
        }
        g_hitsOn = (bool)on;
    }
    g_a = oa; g_hitsOn = (bool)on;
}

// ============================ 10. РАБОЧИЙ СТОЛ ============================
struct IconRt { Tw x, y, ap; bool init = false; };
static std::map<int, IconRt> g_irt;
static void DropIcon(int uid) {
    Shortcut* s = FindSc(uid); if (!s) return;
    int cols, rows; GridDims(cols, rows); IconRt& rt = g_irt[uid];
    int c = (int)floorf((rt.x.cur + CELL_W / 2 - GX0) / CELL_W), r = (int)floorf((rt.y.cur + CELL_H / 2 - GY0) / CELL_H);
    c = std::max(0, std::min(cols - 1, c)); r = std::max(0, std::min(rows - 1, r));
    for (auto& o : g_cfg.icons) if (o.uid != uid && !o.dying && o.col == c && o.row == r) { o.col = s->col; o.row = s->row; break; }
    s->col = c; s->row = r; MarkDirty(); Kick(0.3);
}
static void OpenRename(int uid) {
    Shortcut* s = FindSc(uid); if (!s) return;
    g_renameBuf = s->name; g_modalTitle = L"Переименовать ярлык"; g_modalOpen = true; g_modalT.go(1, 0.3, E_BACK);
    g_modalOk = [uid] { Shortcut* s2 = FindSc(uid); if (s2 && !g_renameBuf.empty()) { s2->name = g_renameBuf; MarkDirty(); } };
    SetFocusField(FLD_RENAME);
}
static void CloseModal(bool ok) {
    if (!g_modalOpen) return; g_modalOpen = false; g_modalT.go(0, 0.2); if (ok && g_modalOk) g_modalOk(); g_modalOk = nullptr; SetFocusField(0);
}
static void ChooseWallpaper() {
    std::wstring p = PickFile(fImg, 2); if (p.empty()) return; g_cfg.wallpaper = p; MarkDirty(); Invalidate();
}
static void OpenBgMenu(Pt p) {
    OpenCtx(p, { { L"Добавить ярлык…", [] { AddShortcutDialog(); } }, { L"Сменить обои…", [] { ChooseWallpaper(); } }, { L"Импорт с рабочего стола", [] { int n = ImportDesktop(); ShowToast(n ? L"Импортировано: " + std::to_wstring(n) : std::wstring(L"Новых ярлыков нет")); } }, { L"Упорядочить по имени", [] { ArrangeIcons(); } }, { L"Открыть меню Пуск", [] { OpenStart(true); } } });
}
static void DrawDesktopIcon(Shortcut& s, IconRt& rt, bool dragging) {
    int id = ID_ICON + s.uid; float x = rt.x.val(), y = rt.y.val(), ap = rt.ap.val();
    float h = Hov(id, g_hover == id && !s.dying && !g_dragging); Col acc = ThemeCol(T_ACC), txt = ThemeCol(T_TXT);
    float oa = g_a; g_a *= Cl(ap, 0.f, 1.f);
    Rc cell = R(x, y, CELL_W, CELL_H); float sc = (0.5f + 0.5f * ap) * (1 + 0.12f * h) * (dragging ? 1.1f : 1.f);
    bool sel = g_selIcons.count(s.uid) > 0; float hs = std::max(h, sel ? 1.f : 0.f);
    FillRR(Infl(cell, -4), 18, Alpha(acc, 0.16f * hs)); StrokeRR(Infl(cell, -4), 18, Alpha(acc, 0.35f * hs));
    float K = g_cfg.iconScale; float is = 64 * K * sc; Rc ir = R(x + CELL_W / 2 - is / 2, y + 46 * K - is / 2, is, is);
    if (dragging) Shadow(ir, 14, 14, 0.4f);
    DrawIcon(IconKeyOf(s), ir);
    Rc lr = R(x + 4, y + 86 * K, CELL_W - 8, 28 * K); float fs = 13.f * std::max(0.85f, std::min(K, 1.3f));
    Txt(s.name, R(lr.l + 1, lr.t + 1, lr.w(), lr.h()), fs, Col{ 0, 0, 0, 0.55f }, 1); Txt(s.name, lr, fs, txt, 1);
    if (!s.dying) {
        if (Hit* ht = AddHit(cell, id, 1)) {
            int uid = s.uid; float cx = x, cy = y;
            ht->click = [uid] {
                if (CtrlDown()) { if (!g_selIcons.erase(uid)) g_selIcons.insert(uid); }
                else if (!g_selIcons.count(uid)) { g_selIcons.clear(); g_selIcons.insert(uid); }
                Invalidate();
            };
            ht->dbl = [uid] {
                if (g_selIcons.size() > 1 && g_selIcons.count(uid)) { LaunchSelectedIcons(); return; }
                g_selIcons.clear(); Shortcut* q = FindSc(uid); if (q && !q->dying) LaunchShortcut(*q);
            };
            ht->right = [uid] {
                std::vector<CtxItem> it;
                if (g_selIcons.size() > 1 && g_selIcons.count(uid)) it.push_back({ L"Запустить выбранные (" + std::to_wstring(g_selIcons.size()) + L")", [] { LaunchSelectedIcons(); } });
                it.push_back({ L"Запустить", [uid] { Shortcut* q = FindSc(uid); if (q) LaunchShortcut(*q); } });
                it.push_back({ L"Переименовать", [uid] { OpenRename(uid); } });
                it.push_back({ L"Удалить", [uid] { Shortcut* q = FindSc(uid); if (q) { q->dying = true; g_selIcons.erase(uid); RebuildAllow(); MarkDirty(); } } });
                OpenCtx(g_mouse, std::move(it));
            };
            ht->drag = [uid, cx, cy](Pt p, int ph) {
                if (ph == 0) { g_dragUid = uid; g_dragOff = Pt{ p.x - cx, p.y - cy }; g_dragging = false; }
                else if (ph == 1) { if (g_didDrag && g_dragUid == uid) { g_dragging = true; IconRt& r2 = g_irt[uid]; r2.x.jump(Cl(p.x - g_dragOff.x, 0.f, g_W - CELL_W)); r2.y.jump(Cl(p.y - g_dragOff.y, 0.f, g_H - TB_H - CELL_H)); } }
                else { if (g_dragging && g_dragUid == uid) DropIcon(uid); g_dragUid = 0; g_dragging = false; Kick(0.3); }
            };
        }
    }
    g_a = oa;
}
static void DrawDesktop() {
    if (Hit* h = AddHit(R(0, 0, g_W, g_H - TB_H), -5, 0)) { h->right = [] { OpenBgMenu(g_mouse); }; h->click = [] { if (!g_selIcons.empty()) { g_selIcons.clear(); Invalidate(); } }; }
    std::vector<int> dead;
    for (int pass = 0; pass < 2; ++pass)
        for (size_t i = 0; i < g_cfg.icons.size(); ++i) {
            Shortcut& s = g_cfg.icons[i]; bool isDrag = (g_dragging && s.uid == g_dragUid);
            if ((pass == 1) != isDrag) continue;
            IconRt& rt = g_irt[s.uid]; float tx = GX0 + s.col * CELL_W, ty = GY0 + s.row * CELL_H;
            if (!rt.init) { rt.init = true; rt.x.jump(tx); rt.y.jump(ty); rt.ap.jump(0); rt.ap.go(1, 0.55, E_BACK, 0.05 * (double)i); }
            if (!isDrag) { rt.x.go(tx, 0.28); rt.y.go(ty, 0.28); }
            if (s.dying) { rt.ap.go(0, 0.25); if (rt.ap.val() <= 0.01f) { if (pass == 0) dead.push_back(s.uid); continue; } }
            DrawDesktopIcon(s, rt, isDrag);
        }
    if (!dead.empty()) {
        for (int uid : dead) { g_cfg.icons.erase(std::remove_if(g_cfg.icons.begin(), g_cfg.icons.end(), [uid](const Shortcut& s) { return s.uid == uid; }), g_cfg.icons.end()); g_irt.erase(uid); }
        MarkDirty();
    }
    if (g_selIcons.size() >= 2 && !g_startOpen)
        Button(ID_LAUNCHSEL, R(g_W / 2 - 160, g_H - TB_H - 76, 320, 48), L"▶  Запустить выбранные (" + std::to_wstring(g_selIcons.size()) + L")", true, [] { LaunchSelectedIcons(); });
    if (g_cfg.icons.empty())
        Txt(L"Рабочий стол пуст — нажмите ПКМ → «Добавить ярлык» или откройте Пуск", R(0, g_H / 2 - 40, g_W, 40), 18, Alpha(ThemeCol(T_DIM), 0.9f), 1);
}

// ============================ 11. ФОН ============================
static void DrawBackground() {
    Rc full = R(0, 0, g_W, g_H);
    if (g_wall) {
        D2D1_SIZE_F sz = g_wall->GetSize(); float s = std::max(g_W / sz.width, g_H / sz.height), w = sz.width * s, h = sz.height * s;
        g_rt->DrawBitmap(g_wall, D2D1::RectF((g_W - w) / 2, (g_H - h) / 2, (g_W + w) / 2, (g_H + h) / 2), 1.f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        FillRect_(full, Alpha(ThemeCol(T_BG1), 0.30f)); return;
    }
    float t = (float)g_now, ang = g_cfg.animBg ? t * 0.12f : 0.6f, mid = g_cfg.animBg ? 0.5f + 0.15f * sinf(t * 0.4f) : 0.5f;
    Pt c = { g_W / 2, g_H / 2 }; float rad = sqrtf(g_W * g_W + g_H * g_H) / 2;
    D2D1_GRADIENT_STOP st[3]; st[0].position = 0; st[0].color = CF(ThemeCol(T_BG1)); st[1].position = mid; st[1].color = CF(ThemeCol(T_BG2)); st[2].position = 1; st[2].color = CF(ThemeCol(T_BG3));
    ID2D1GradientStopCollection* gs = nullptr;
    if (SUCCEEDED(g_rt->CreateGradientStopCollection(st, 3, &gs)) && gs) {
        ID2D1LinearGradientBrush* lb = nullptr;
        if (SUCCEEDED(g_rt->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(c.x - cosf(ang) * rad, c.y - sinf(ang) * rad), D2D1::Point2F(c.x + cosf(ang) * rad, c.y + sinf(ang) * rad)), gs, &lb)) && lb) {
            g_rt->FillRectangle(DR(full), lb); lb->Release();
        }
        gs->Release();
    }
    if (g_cfg.animBg) for (int k = 0; k < 3; ++k) {   // мягкие движущиеся «пятна»
        float cx = g_W * (0.5f + 0.38f * sinf(t * 0.2f + k * 2.1f)), cy = g_H * (0.5f + 0.38f * cosf(t * 0.17f + k * 1.7f)), rr = g_H * 0.7f;
        Col a = ThemeCol(T_ACC); D2D1_GRADIENT_STOP s2[2]; s2[0].position = 0; s2[0].color = D2D1::ColorF(a.r, a.g, a.b, 0.20f); s2[1].position = 1; s2[1].color = D2D1::ColorF(a.r, a.g, a.b, 0.f);
        ID2D1GradientStopCollection* g2 = nullptr;
        if (SUCCEEDED(g_rt->CreateGradientStopCollection(s2, 2, &g2)) && g2) {
            ID2D1RadialGradientBrush* rb = nullptr;
            if (SUCCEEDED(g_rt->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(D2D1::Point2F(cx, cy), D2D1::Point2F(0, 0), rr, rr), g2, &rb)) && rb) {
                g_rt->FillRectangle(DR(full), rb); rb->Release();
            }
            g2->Release();
        }
    }
}

// ============================ 12. НАСТРОЙКИ (внутри Пуска) ============================
static void DrawSettings(float aa) {
    g_a = aa; Rc vw = g_view[1]; float sc = g_scrTw[1].val();
    g_rt->PushAxisAlignedClip(DR(vw), D2D1_ANTIALIAS_MODE_ALIASED); g_clip = vw;
    float cw = std::min(vw.w(), 880.f), x = vw.l, y = vw.t - sc; const float gap = 16.f;
    Col txt = ThemeCol(T_TXT), dim = ThemeCol(T_DIM), acc = ThemeCol(T_ACC), card = ThemeCol(T_CARD);
    auto CardBox = [&](float h) -> Rc {
        Rc r = R(x, y, cw, h); Shadow(r, 18, 14, 0.28f); FillRR(r, 18, Alpha(card, Cl(g_cfg.opacity, 0.4f, 0.95f))); StrokeRR(r, 18, Alpha(txt, 0.06f)); y += h + gap; return r;
    };
    { // тема
        Rc c = CardBox(176); Txt(L"Цветовая тема", R(c.l + 24, c.t + 14, 320, 30), 17, txt, 0, true);
        for (int i = 0; i < 6; ++i) {
            Rc sw = R(c.l + 24 + i * 88, c.t + 56, 72, 72); GradRR(sw, 16, g_themes[i].c[T_BG1], g_themes[i].c[T_BG3]);
            Circle(sw.r - 20, sw.b - 20, 9, g_themes[i].c[T_ACC]);
            bool sel = (g_cfg.theme == i); int hid = ID_THEME + i; float h = Hov(hid, g_hover == hid);
            Tw& st = TW(hid, 1); st.go(sel ? 1.f : 0.f, 0.25, E_BACK); float k = st.val();
            if (k > 0.01f) StrokeRR(Infl(sw, 3 + 2 * (1 - k)), 18, Alpha(acc, Cl(k, 0, 1)), 3); else if (h > 0.01f) StrokeRR(Infl(sw, 2), 18, Alpha(txt, 0.5f * h), 2);
            Txt(g_themes[i].name, R(sw.l - 8, sw.b + 4, sw.w() + 16, 22), 12, sel ? txt : dim, 1);
            if (Hit* ht = AddHit(sw, hid, 1)) ht->click = [i] { SetTheme(i); };
        }
    }
    { // прозрачность
        Rc c = CardBox(108); Txt(L"Прозрачность панелей", R(c.l + 24, c.t + 14, 360, 30), 17, txt, 0, true);
        wchar_t b[16]; swprintf(b, 16, L"%d%%", (int)(g_cfg.opacity * 100 + 0.5f)); Txt(b, R(c.r - 124, c.t + 14, 100, 30), 15, dim, 2);
        Slider(ID_SL_OP, R(c.l + 24, c.t + 60, c.w() - 48, 28), (g_cfg.opacity - 0.35f) / 0.65f, [](float t) { g_cfg.opacity = 0.35f + 0.65f * t; MarkDirty(); Invalidate(); });
    }
    { // размер иконок
        Rc c = CardBox(108); Txt(L"Размер иконок на рабочем столе", R(c.l + 24, c.t + 14, 460, 30), 17, txt, 0, true);
        wchar_t b[16]; swprintf(b, 16, L"%d%%", (int)(g_cfg.iconScale * 100 + 0.5f)); Txt(b, R(c.r - 124, c.t + 14, 100, 30), 15, dim, 2);
        Slider(ID_SL_SCALE, R(c.l + 24, c.t + 60, c.w() - 48, 28), (g_cfg.iconScale - 0.6f) / 1.0f, [](float t) { g_cfg.iconScale = 0.6f + t; ApplyScale(); NormalizeGrid(); MarkDirty(); Invalidate(); });
    }
    { // анимированный фон
        Rc c = CardBox(84); Txt(L"Анимированный фон", R(c.l + 24, c.t + 14, 400, 28), 17, txt, 0, true);
        Txt(L"Плавно переливающийся градиент (не влияет на обои)", R(c.l + 24, c.t + 42, 520, 22), 13, dim, 0);
        Toggle(ID_SW_ANIM, R(c.r - 24 - 56, c.t + 27, 56, 30), g_cfg.animBg, [] { g_cfg.animBg = !g_cfg.animBg; MarkDirty(); Kick(0.3); });
    }
    { // обои
        Rc c = CardBox(132); Txt(L"Обои", R(c.l + 24, c.t + 14, 300, 28), 17, txt, 0, true);
        Txt(g_cfg.wallpaper.empty() ? std::wstring(L"Не выбраны — используется градиент") : g_cfg.wallpaper, R(c.l + 24, c.t + 42, c.w() - 48, 22), 13, dim, 0);
        Button(ID_WALLSEL, R(c.l + 24, c.t + 76, 160, 40), L"Выбрать…", true, [] { ChooseWallpaper(); });
        Button(ID_WALLRST, R(c.l + 196, c.t + 76, 140, 40), L"Сбросить", false, [] { g_cfg.wallpaper.clear(); MarkDirty(); Invalidate(); });
    }
    { // скрывать панель задач
        Rc c = CardBox(84); Txt(L"Своя панель задач вместо панели Windows", R(c.l + 24, c.t + 14, 460, 28), 17, txt, 0, true);
        Txt(L"Вкл: наша панель всегда видна и пропадает при полном экране (F11, видео). Выкл: панель Windows", R(c.l + 24, c.t + 42, c.w() - 140, 22), 13, dim, 0);
        Toggle(ID_SW_HIDE, R(c.r - 24 - 56, c.t + 27, 56, 30), g_cfg.hideTaskbar, [] {
            g_cfg.hideTaskbar = !g_cfg.hideTaskbar; if (g_cfg.hideTaskbar) HideSystemUi(); else RestoreSystem(); ApplyRect(); MarkDirty(); });
    }
    { // закрывать Explorer
        Rc c = CardBox(84); Txt(L"Закрывать Проводник при запуске", R(c.l + 24, c.t + 14, 460, 28), 17, txt, 0, true);
        Txt(L"Рекомендуется выкл: Проводник и панель задач остаются. Вернётся при выходе", R(c.l + 24, c.t + 42, c.w() - 140, 22), 13, dim, 0);
        Toggle(ID_SW_KILL, R(c.r - 24 - 56, c.t + 27, 56, 30), g_cfg.killExplorer, [] {
            g_cfg.killExplorer = !g_cfg.killExplorer;
            if (g_cfg.killExplorer) { KillExplorer(); g_killsLeft = 2; g_killDeadline = g_now + 8.0; } else EnsureExplorer();
            MarkDirty(); });
    }
    g_scrMax[1] = std::max(0.f, (y - gap) - (vw.t - sc) - vw.h());
    g_rt->PopAxisAlignedClip(); g_clip = Rc{ -1e9f, -1e9f, 1e9f, 1e9f };
}

// ============================ 13. МЕНЮ ПУСК (на весь экран) ============================
static void DrawStart() {
    float v = g_start.val(); if (v < 0.003f && !g_startOpen) return;
    float baseA = g_a; g_a = v; g_hitsOn = g_startOpen;
    float H2 = g_H - TB_H, off = (1.f - v) * 44.f; Rc full = R(0, 0, g_W, H2);
    if (g_startOpen) AddHit(full, -1, 0);
    Col txt = ThemeCol(T_TXT), dim = ThemeCol(T_DIM), acc = ThemeCol(T_ACC), card = ThemeCol(T_CARD);
    FillRR(R(0, off, g_W, H2), 0, Alpha(Mix(ThemeCol(T_BG1), ThemeCol(T_PANEL), 0.55f), Cl(g_cfg.opacity + 0.12f, 0.3f, 0.98f)));
    const float LX = 36, LW = 360; float top = 36 + off;
    Field(FLD_SEARCH, R(LX, top, LW, 50), g_search, L"Поиск приложений…");
    // вкладки (сегмент с плавающей «пилюлей»)
    float tv = g_tab.val(); Rc seg = R(LX, top + 66, LW, 48); FillRR(seg, 14, Alpha(card, 0.8f));
    float half = (LW - 8) / 2; FillRR(R(LX + 4 + tv * half, seg.t + 4, half, 40), 10, acc);
    Txt(L"Приложения", R(LX + 4, seg.t + 4, half, 40), 15, Mix(kDark, txt, tv), 1, true);
    Txt(L"Настройки", R(LX + 4 + half, seg.t + 4, half, 40), 15, Mix(txt, kDark, tv), 1, true);
    if (Hit* h = AddHit(R(LX + 4, seg.t + 4, half, 40), ID_TAB0, 1)) h->click = [] { SetTab(0); };
    if (Hit* h = AddHit(R(LX + 4 + half, seg.t + 4, half, 40), ID_TAB1, 1)) h->click = [] { SetTab(1); };
    // нижние кнопки
    Button(ID_POWER, R(LX, off + H2 - 40 - 52 - 12 - 52 - 12 - 52, LW, 52), L"Питание", false, [] {
        OpenCtx(g_mouse, { { L"Блокировка", [] { LockWorkStation(); } },
            { L"Сон", [] { ShellExecuteW(nullptr, L"open", L"rundll32.exe", L"powrprof.dll,SetSuspendState 0,1,0", nullptr, SW_HIDE); } },
            { L"Перезагрузка", [] { ShellExecuteW(nullptr, L"open", L"shutdown.exe", L"/r /t 0", nullptr, SW_HIDE); } },
            { L"Выключение", [] { ShellExecuteW(nullptr, L"open", L"shutdown.exe", L"/s /t 0", nullptr, SW_HIDE); } } }); });
    Button(ID_ADD, R(LX, off + H2 - 40 - 52 - 12 - 52, LW, 52), L"＋  Добавить ярлык", true, [] { AddShortcutDialog(); });
    Button(ID_EXIT, R(LX, off + H2 - 40 - 52, LW, 52), L"⏻  Выйти", false, [] { QuitShell(); });
    if (!g_selApps.empty()) Button(ID_LAUNCHAPPS, R(LX, off + H2 - 284, LW, 52), L"▶  Запустить выбранные (" + std::to_wstring(g_selApps.size()) + L")", true, [] { LaunchSelectedApps(); });
    // правая часть
    float RX = LX + LW + 40, RW = g_W - RX - 36, vtop = top + 62, vbot = off + H2 - 28;
    g_view[0] = g_view[1] = R(RX, vtop, RW, vbot - vtop);
    std::wstring q = Lower(g_search); std::vector<int> idx;
    for (size_t i = 0; i < g_appsUI.size(); ++i) if (q.empty() || Lower(g_appsUI[i].name).find(q) != std::wstring::npos) idx.push_back((int)i);
    g_firstApp = idx.empty() ? -1 : idx[0];
    float aApps = v * (1.f - tv), aSet = v * tv;
    if (aApps > 0.01f) {
        g_a = aApps; g_hitsOn = g_startOpen && g_tabIdx == 0;
        wchar_t hb[96]; swprintf(hb, 96, q.empty() ? L"Все приложения (%d)" : L"Найдено: %d", (int)idx.size());
        Txt(hb, R(RX, top, RW, 48), 22, txt, 0, true);
        Txt(L"Ctrl+клик — выбрать несколько приложений", R(RX, top, RW, 48), 13, dim, 2);
        Rc vw = g_view[0]; float sc = g_scrTw[0].val(); const float gap = 16, tw0 = 176, th = 140;
        int cols = std::max(1, (int)((RW + gap) / (tw0 + gap))); float tw = (RW - gap * (cols - 1)) / cols;
        int rows = ((int)idx.size() + cols - 1) / cols; float content = rows > 0 ? rows * (th + gap) - gap : 0;
        g_scrMax[0] = std::max(0.f, content - vw.h());
        g_rt->PushAxisAlignedClip(DR(vw), D2D1_ANTIALIAS_MODE_ALIASED); g_clip = vw;
        for (int k = 0; k < (int)idx.size(); ++k) {
            int row = k / cols, col = k % cols; float x = RX + col * (tw + gap), y = vw.t + row * (th + gap) - sc;
            if (y + th < vw.t - 4 || y > vw.b + 4) continue;
            float p = Cl(v * 1.6f - (float)std::min(k, 24) * 0.035f, 0.f, 1.f); float oa = g_a; g_a = aApps * p;
            const AppEntry a = g_appsUI[idx[k]];
            Tile(ID_TILE + k, R(x, y + (1 - p) * 30, tw, th), a.name, AppKey(a.parse), [a] { if (CtrlDown() || !g_selApps.empty()) { if (!g_selApps.erase(a.parse)) g_selApps.insert(a.parse); Invalidate(); } else LaunchApp(a); },
                [a] { OpenCtx(g_mouse, { { L"Запустить", [a] { LaunchApp(a); } }, { g_selApps.count(a.parse) ? L"Снять выбор" : L"Выбрать (для запуска нескольких)", [a] { if (!g_selApps.erase(a.parse)) g_selApps.insert(a.parse); Invalidate(); } }, { L"Закрепить на рабочем столе", [a] { PinApp(a); } } }); },
                g_selApps.count(a.parse) > 0);
            g_a = oa;
        }
        g_rt->PopAxisAlignedClip(); g_clip = Rc{ -1e9f, -1e9f, 1e9f, 1e9f };
        if (idx.empty()) Txt(g_appsUI.empty() ? L"Список приложений загружается…" : L"Ничего не найдено", R(RX, vw.t, RW, 80), 18, dim, 1);
    }
    if (aSet > 0.01f) {
        g_hitsOn = g_startOpen && g_tabIdx == 1; g_a = aSet;
        Txt(L"Настройки", R(RX, top, RW, 48), 22, txt, 0, true);
        DrawSettings(aSet);
    }
    g_hitsOn = true; g_a = baseA;
}

// ============================ 14. ПАНЕЛЬ ЗАДАЧ / МЕНЮ / МОДАЛКА / ТОСТ ============================
static std::wstring TimeStr() { wchar_t b[32] = L""; GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, 0, nullptr, L"HH:mm", b, 32); return b; }
static std::wstring DateStr() { wchar_t b[64] = L""; GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, 0, nullptr, L"ddd, d MMM yyyy", b, 64, nullptr); return b; }

struct TaskWin { HWND h; std::wstring title, key; };
static std::vector<TaskWin> g_tasks;
static std::wstring ProcPath(DWORD pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid); if (!h) return L"";
    wchar_t b[MAX_PATH * 2]; DWORD n = MAX_PATH * 2; std::wstring r; if (QueryFullProcessImageNameW(h, 0, b, &n)) r = b;
    CloseHandle(h); return r;
}
static BOOL CALLBACK TaskEnum(HWND h, LPARAM lp) {
    auto* v = (std::vector<TaskWin>*)lp;
    if (h == g_hwnd || h == g_mini || !IsWindowVisible(h) || GetWindow(h, GW_OWNER)) return TRUE;
    LONG ex = GetWindowLongW(h, GWL_EXSTYLE); if ((ex & WS_EX_TOOLWINDOW) && !(ex & WS_EX_APPWINDOW)) return TRUE;
    DWORD cloaked = 0; DwmGetWindowAttribute(h, 14 /*DWMWA_CLOAKED*/, &cloaked, sizeof(cloaked)); if (cloaked) return TRUE;
    wchar_t t[256]; if (!GetWindowTextW(h, t, 256) || !t[0]) return TRUE;
    wchar_t cls[64]; GetClassNameW(h, cls, 64);
    if (!wcscmp(cls, L"Progman") || !wcscmp(cls, L"WorkerW") || !wcscmp(cls, L"Shell_TrayWnd") || !wcscmp(cls, L"Shell_SecondaryTrayWnd")) return TRUE;
    DWORD pid = 0; GetWindowThreadProcessId(h, &pid); if (pid == GetCurrentProcessId()) return TRUE;
    TaskWin w; w.h = h; w.title = t; w.key = ProcPath(pid); v->push_back(w); return TRUE;
}
static void RefreshTasks() {   // порядок кнопок стабильный: старые остаются на местах, новые — в конец
    std::vector<TaskWin> v; EnumWindows(TaskEnum, (LPARAM)&v);
    std::vector<TaskWin> n; bool changed = false;
    for (auto& o : g_tasks) for (auto& w : v) if (w.h == o.h) { if (w.title != o.title) changed = true; n.push_back(w); break; }
    for (auto& w : v) { bool have = false; for (auto& o : n) if (o.h == w.h) { have = true; break; } if (!have) { n.push_back(w); changed = true; } }
    if (n.size() != g_tasks.size()) changed = true;
    g_tasks.swap(n); if (changed) Invalidate();
}
static void DrawTaskbar() {
    float tb = g_tb.val(), off = std::max(0.f, 1.f - tb) * TB_H, y = g_H - TB_H + off; Rc bar = R(0, y, g_W, TB_H);
    Col txt = ThemeCol(T_TXT), dim = ThemeCol(T_DIM), acc = ThemeCol(T_ACC), card = ThemeCol(T_CARD);
    AddHit(bar, -6, 0);
    Shadow(Rc{ 0, y, g_W, y + 4 }, 0, 18, 0.45f);
    FillRR(bar, 0, Alpha(Mix(ThemeCol(T_PANEL), ThemeCol(T_BG1), 0.4f), Cl(g_cfg.opacity + 0.1f, 0.3f, 0.99f)));
    FillRect_(R(0, y, g_W, 1), Alpha(acc, 0.25f));
    float x = 14;
    { // Пуск
        Rc r = R(x, y + 6, 48, 48); float h = Hov(ID_STARTBTN, g_hover == ID_STARTBTN);
        FillRR(r, 12, Mix(Alpha(card, 0.6f), Alpha(acc, 0.55f), Cl(h + (g_startOpen ? 1.f : 0.f), 0.f, 1.f)));
        float s = 9, g = 3, ox = r.l + 24 - 10.5f, oy = r.t + 24 - 10.5f;
        for (int i = 0; i < 2; ++i) for (int j = 0; j < 2; ++j) FillRR(R(ox + i * (s + g), oy + j * (s + g), s, s), 2.5f, g_startOpen ? kDark : txt);
        if (Hit* ht = AddHit(r, ID_STARTBTN, 1)) ht->click = [] { OpenStart(!g_startOpen); };
    }
    float lx = 76, rx = g_W - 190;
    if (rx > lx + 60) {
        if (g_tasks.empty()) Txt(L"Нет открытых окон", R(lx, y, rx - lx, TB_H), 13, Alpha(dim, 0.8f), 0);
        float cw = 170, gp = 6; int n = (int)g_tasks.size();
        if (n > 0 && n * cw + (n - 1) * gp > rx - lx) cw = std::max(44.f, (rx - lx - gp * (n - 1)) / n);
        HWND fgw = GetForegroundWindow();
        for (int i = 0; i < n; ++i) {
            int id = ID_TASK0 + i; float x0 = lx + i * (cw + gp); if (x0 + cw > rx + 1) break;
            Rc r = R(x0, y + 10, cw, 40); float hv = Hov(id, g_hover == id); bool act = (g_tasks[i].h == fgw);
            FillRR(r, 10, Mix(Alpha(card, 0.55f), Alpha(acc, 0.45f), Cl(hv * 0.6f + (act ? 0.7f : 0.f), 0.f, 1.f)));
            if (act) FillRR(R(r.l + 12, r.b - 3, r.w() - 24, 3), 1.5f, acc);
            DrawIcon(g_tasks[i].key, R(r.l + 8, r.t + 8, 24, 24));
            if (cw > 80) Txt(g_tasks[i].title, R(r.l + 40, r.t, r.w() - 48, r.h()), 13, txt, 0);
            HWND hw = g_tasks[i].h;
            if (Hit* ht = AddHit(r, id, 1)) {
                ht->click = [hw] {
                    if (!IsWindow(hw)) return;
                    if (IsIconic(hw)) { ShowWindow(hw, SW_RESTORE); ForceFg(hw); }
                    else if (GetForegroundWindow() == hw) ShowWindowAsync(hw, SW_MINIMIZE);
                    else ForceFg(hw); };
                ht->right = [hw] { OpenCtx(g_mouse, { { L"Закрыть окно", [hw] { PostMessageW(hw, WM_CLOSE, 0, 0); } } }); };
            }
        }
    }
    Rc cr = R(g_W - 170, y, 156, TB_H);
    Txt(TimeStr(), R(cr.l, y + 7, cr.w(), 28), 20, txt, 2, true); Txt(DateStr(), R(cr.l, y + 34, cr.w(), 18), 12, dim, 2);
}
static void DrawCtx() {
    float v = g_ctxT.val(); if (v < 0.01f && !g_ctxOpen) return;
    float oa = g_a; g_a = Cl(v, 0.f, 1.f); bool on = g_hitsOn; g_hitsOn = g_ctxOpen;
    if (g_ctxOpen) if (Hit* b = AddHit(R(0, 0, g_W, g_H), -2, 0)) { b->click = [] { CloseCtx(); }; b->right = [] { CloseCtx(); }; }
    const float w = 260, ih = 40; float h = g_ctxItems.size() * ih + 16;
    float x = Cl(g_ctxPos.x, 8, std::max(8.f, g_W - w - 8)), y = Cl(g_ctxPos.y, 8, std::max(8.f, g_H - h - 8)) - (1 - Cl(v, 0.f, 1.f)) * 8;
    Rc box = R(x, y, w, h); Shadow(box, 14, 20, 0.5f);
    FillRR(box, 14, Alpha(Mix(ThemeCol(T_PANEL), ThemeCol(T_CARD), 0.5f), 0.97f)); StrokeRR(box, 14, Alpha(ThemeCol(T_TXT), 0.08f));
    for (size_t i = 0; i < g_ctxItems.size(); ++i) {
        int id = ID_CTX + (int)i; Rc ir = R(x + 8, y + 8 + i * ih, w - 16, ih); float hv = Hov(id, g_hover == id, 0.12);
        FillRR(ir, 9, Alpha(ThemeCol(T_ACC), 0.28f * hv)); Txt(g_ctxItems[i].label, R(ir.l + 12, ir.t, ir.w() - 20, ir.h()), 14, ThemeCol(T_TXT), 0);
        if (Hit* ht = AddHit(ir, id, 1)) { size_t k = i; ht->click = [k] { if (k < g_ctxItems.size()) { auto fn = g_ctxItems[k].fn; CloseCtx(); if (fn) fn(); } }; }
    }
    g_hitsOn = on; g_a = oa;
}
static void DrawModal() {
    float v = g_modalT.val(); if (v < 0.01f && !g_modalOpen) return;
    float oa = g_a; g_a = Cl(v, 0.f, 1.f); bool on = g_hitsOn; g_hitsOn = g_modalOpen;
    FillRect_(R(0, 0, g_W, g_H), Col{ 0, 0, 0, 0.5f }); if (g_modalOpen) AddHit(R(0, 0, g_W, g_H), -3, 0);
    Rc c = R(g_W / 2 - 260, g_H / 2 - 110 + (1 - Cl(v, 0.f, 1.f)) * 24, 520, 220); Shadow(c, 20, 26, 0.5f);
    FillRR(c, 20, Alpha(Mix(ThemeCol(T_PANEL), ThemeCol(T_BG1), 0.3f), 0.98f)); StrokeRR(c, 20, Alpha(ThemeCol(T_TXT), 0.08f));
    Txt(g_modalTitle, R(c.l + 28, c.t + 20, c.w() - 56, 32), 20, ThemeCol(T_TXT), 0, true);
    Field(FLD_RENAME, R(c.l + 28, c.t + 68, c.w() - 56, 50), g_renameBuf, L"Название");
    Button(ID_MCANCEL, R(c.r - 28 - 130 - 12 - 130, c.b - 28 - 44, 130, 44), L"Отмена", false, [] { CloseModal(false); });
    Button(ID_MOK, R(c.r - 28 - 130, c.b - 28 - 44, 130, 44), L"Готово", true, [] { CloseModal(true); });
    g_hitsOn = on; g_a = oa;
}
static void DrawToast() {
    float oa = g_a; if (g_toast.to > 0.5f && g_now > g_toastUntil) g_toast.go(0, 0.4, E_CUBIC);
    float v = g_toast.val(); if (v < 0.005f) { g_a = oa; return; }
    g_a = Cl(v, 0.f, 1.f); float w = TxtW(g_toastTxt, 16, true) + 72; Rc r = R((g_W - w) / 2, Lerpf(-70.f, 26.f, v), w, 52);
    Shadow(r, 16, 18, 0.45f); FillRR(r, 16, Alpha(Mix(ThemeCol(T_PANEL), kDark, 0.3f), 0.97f)); StrokeRR(r, 16, Alpha(ThemeCol(T_TXT), 0.1f));
    FillRR(R(r.l + 10, r.t + 11, 4, 30), 2, g_toastWarn ? Col{ 1.f, 0.38f, 0.38f, 1.f } : ThemeCol(T_ACC));
    Txt(g_toastTxt, R(r.l + 26, r.t, w - 40, 52), 16, ThemeCol(T_TXT), 0, true); g_a = oa;
}

static void Render() {
    if (!EnsureRT()) return;
    RECT rc; GetClientRect(g_hwnd, &rc); if (rc.right <= 0 || rc.bottom <= 0) return;
    D2D1_SIZE_U ps = g_rt->GetPixelSize();
    if (ps.width != (UINT32)rc.right || ps.height != (UINT32)rc.bottom) g_rt->Resize(D2D1::SizeU((UINT32)rc.right, (UINT32)rc.bottom));
    g_W = rc.right * 96.f / g_dpi; g_H = rc.bottom * 96.f / g_dpi;
    { static float lw = 0, lh = 0; if (lw != g_W || lh != g_H) { lw = g_W; lh = g_H; NormalizeGrid(); MarkDirty(); } }
    g_hits.clear(); g_clip = Rc{ -1e9f, -1e9f, 1e9f, 1e9f }; g_hitsOn = true; g_a = 1.f;
    UpdateWallpaper();
    g_rt->BeginDraw(); g_rt->SetTransform(D2D1::Matrix3x2F::Identity()); g_rt->Clear(D2D1::ColorF(0, 0, 0, 1));
    DrawBackground(); DrawDesktop(); DrawMedia(); DrawStart(); if (g_cfg.hideTaskbar) DrawTaskbar(); DrawCtx(); DrawModal(); DrawToast();
    if (FAILED(g_rt->EndDraw()) ) DiscardRT();   // D2DERR_RECREATE_TARGET и пр. — пересоздадим в след. кадре
}

// ============================ 15. ФОКУС-ВОЧЕР / ОКНА ============================
static void PositionMini() {
    if (!g_mini) return;
    HMONITOR m = MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTOPRIMARY); MONITORINFO mi = {}; mi.cbSize = sizeof(mi); GetMonitorInfoW(m, &mi);
    int w = MulDiv(30, (int)g_dpi, 96), h = MulDiv(76, (int)g_dpi, 96);
    SetWindowPos(g_mini, HWND_TOPMOST, mi.rcMonitor.right - w, mi.rcMonitor.top + (mi.rcMonitor.bottom - mi.rcMonitor.top) / 3, w, h, SWP_NOACTIVATE);
    SetWindowRgn(g_mini, CreateRoundRectRgn(0, 0, w + 16, h + 1, 16, 16), TRUE);
}
#define WM_APPBAR (WM_APP + 40)
static bool g_abar = false;
static void AppBarRemove() {
    if (!g_abar || !g_hwnd) return; APPBARDATA ad = {}; ad.cbSize = sizeof(ad); ad.hWnd = g_hwnd; SHAppBarMessage(ABM_REMOVE, &ad); g_abar = false;
}
static void AppBarUpdate() {   // «Своя панель»: резервируем полосу внизу экрана, максимизированные окна не заезжают на неё
    if (!g_hwnd) return; if (!g_cfg.hideTaskbar) { AppBarRemove(); return; }
    APPBARDATA ad = {}; ad.cbSize = sizeof(ad); ad.hWnd = g_hwnd; ad.uCallbackMessage = WM_APPBAR;
    if (!g_abar) { if (!SHAppBarMessage(ABM_NEW, &ad)) return; g_abar = true; }
    MONITORINFO mi = {}; mi.cbSize = sizeof(mi); GetMonitorInfoW(MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTOPRIMARY), &mi);
    int h = MulDiv(60, (int)g_dpi, 96); ad.uEdge = ABE_BOTTOM; ad.rc = mi.rcMonitor; ad.rc.top = ad.rc.bottom - h;
    SHAppBarMessage(ABM_QUERYPOS, &ad); ad.rc.top = ad.rc.bottom - h; SHAppBarMessage(ABM_SETPOS, &ad);
}
static void ApplyRect() {
    TB_H = g_cfg.hideTaskbar ? 60.f : 0.f;
    HMONITOR m = MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTOPRIMARY); MONITORINFO mi = {}; mi.cbSize = sizeof(mi); GetMonitorInfoW(m, &mi);
    RECT r = g_cfg.hideTaskbar ? mi.rcMonitor : mi.rcWork;
    SetWindowPos(g_hwnd, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOZORDER | SWP_NOACTIVATE);
    AppBarUpdate(); PositionMini(); NormalizeGrid(); Invalidate();
}
// --- панель Windows: всегда сверху, но исчезает, когда поверх открыто полноэкранное окно (F11 в браузере, видео, игра) ---
static bool g_fsActive = false; static std::set<HWND> g_trayHidden;
static bool ShellSaysFullscreen() {
    QUERY_USER_NOTIFICATION_STATE s = QUNS_ACCEPTS_NOTIFICATIONS;
    if (FAILED(SHQueryUserNotificationState(&s))) return true;   // не удалось спросить — верим геометрии окна
    return s == QUNS_BUSY || s == QUNS_RUNNING_D3D_FULL_SCREEN;
}
static bool IsFullscreenWindow(HWND fg) {
    if (!fg || fg == g_hwnd || fg == g_mini || !IsWindowVisible(fg) || IsIconic(fg)) return false;
    wchar_t cls[64] = L""; GetClassNameW(fg, cls, 64);
    if (!wcscmp(cls, L"Progman") || !wcscmp(cls, L"WorkerW") || !wcscmp(cls, L"Shell_TrayWnd") || !wcscmp(cls, L"Shell_SecondaryTrayWnd")) return false;
    RECT r; if (!GetWindowRect(fg, &r)) return false;
    MONITORINFO mi = {}; mi.cbSize = sizeof(mi); if (!GetMonitorInfoW(MonitorFromWindow(fg, MONITOR_DEFAULTTONEAREST), &mi)) return false;
    if (r.left > mi.rcMonitor.left || r.top > mi.rcMonitor.top || r.right < mi.rcMonitor.right || r.bottom < mi.rcMonitor.bottom) return false;
    DWORD pid = 0; GetWindowThreadProcessId(fg, &pid);
    if (!pid || pid == GetCurrentProcessId()) return false;
    if (_wcsicmp(ProcExe(pid).c_str(), L"explorer.exe") == 0) return false;   // Alt+Tab, рабочий стол и т.п.
    return ShellSaysFullscreen();
}
static BOOL CALLBACK TrayCollect(HWND h, LPARAM lp) {
    wchar_t c[64] = L""; GetClassNameW(h, c, 64);
    if (!wcscmp(c, L"Shell_SecondaryTrayWnd")) ((std::vector<HWND>*)lp)->push_back(h);
    return TRUE;
}
static void UpdateMini() {   // язычок «вернуться в оболочку» не показываем поверх полноэкранного видео
    if (!g_mini) return;
    bool want = (GetForegroundWindow() != g_hwnd) && !g_fsActive;
    if (want != (IsWindowVisible(g_mini) != 0)) ShowWindow(g_mini, want ? SW_SHOWNOACTIVATE : SW_HIDE);
}
static void WatchFullscreen() {
    if (!g_hwnd) return;
    HWND fg = GetForegroundWindow();
    if (!fg) { UpdateMini(); return; }          // переходный момент (смена окна) — состояние не меняем
    bool fs = IsFullscreenWindow(fg); g_fsActive = fs;
    UpdateMini();
    if (g_cfg.hideTaskbar || g_cfg.killExplorer) return;   // системную панель не трогаем, если она и так скрыта/убита
    std::vector<HWND> trays; HWND t0 = FindWindowW(L"Shell_TrayWnd", nullptr); if (t0) trays.push_back(t0);
    EnumWindows(TrayCollect, (LPARAM)&trays);
    for (auto it = g_trayHidden.begin(); it != g_trayHidden.end();) { if (!IsWindow(*it)) it = g_trayHidden.erase(it); else ++it; }
    HMONITOR fm = fs ? MonitorFromWindow(fg, MONITOR_DEFAULTTONEAREST) : nullptr;
    for (HWND t : trays) {
        if (fs && MonitorFromWindow(t, MONITOR_DEFAULTTONEAREST) == fm) {
            if (IsWindowVisible(t)) { ShowWindow(t, SW_HIDE); g_trayHidden.insert(t); }
        } else {
            if (g_trayHidden.erase(t)) ShowWindow(t, SW_SHOWNA);
            if (IsWindowVisible(t) && !(GetWindowLongW(t, GWL_EXSTYLE) & WS_EX_TOPMOST))
                SetWindowPos(t, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
    }
}
static void FocusTick() {
    if (g_cfg.hideTaskbar) RefreshTasks();
    if (g_cfg.killExplorer && g_killsLeft > 0) {   // Windows перезапускает Explorer после kill — добиваем
        if (g_now > g_killDeadline) g_killsLeft = 0;
        else if (FindWindowW(L"Shell_TrayWnd", nullptr)) { KillExplorer(); g_killsLeft--; }
    }
    if (g_cfg.hideTaskbar) HideSystemUi();
    else if (!g_cfg.killExplorer) { static int ec = 0; if ((ec++ % 7) == 0) EnsureExplorer(); }   // Проводник должен жить
    if (IsIconic(g_hwnd)) ShowWindow(g_hwnd, SW_RESTORE);
    WatchFullscreen();
    HWND fg = GetForegroundWindow();
    if (!fg) return;
    if (fg == g_hwnd) {
        if (g_launchActive && g_launchSaw) { g_launchActive = false; g_recent.clear(); UpdatePresence(); }
        if (g_cfg.animBg && !g_timerOn) Kick(0.1);
        return;
    }
    if (fg == g_mini) return;
    DWORD pid = 0; GetWindowThreadProcessId(fg, &pid); if (!pid || pid == GetCurrentProcessId()) return;
    if (g_launchActive) g_launchSaw = true;
}

// ============================ 15b. ХУК WIN + КОМБИНАЦИИ ============================
static HHOOK g_kbHook = nullptr; static bool g_winDown = false, g_winCombo = false;
static LRESULT CALLBACK KbProc(int n, WPARAM w, LPARAM l) {
    if (n == HC_ACTION) {
        const KBDLLHOOKSTRUCT* k = (const KBDLLHOOKSTRUCT*)l;
        if (!(k->flags & LLKHF_INJECTED)) {
            bool dn = (w == WM_KEYDOWN || w == WM_SYSKEYDOWN), up = (w == WM_KEYUP || w == WM_SYSKEYUP);
            if (k->vkCode == VK_LWIN || k->vkCode == VK_RWIN) {
                if (dn && !g_winDown) { g_winDown = true; g_winCombo = false; }
                else if (up && g_winDown) {
                    g_winDown = false;
                    if (!g_winCombo) {   // «чистое» нажатие Win: гасим стандартный Пуск и открываем свой
                        INPUT in[2] = {}; in[0].type = in[1].type = INPUT_KEYBOARD; in[0].ki.wVk = in[1].ki.wVk = 0xE8; in[1].ki.dwFlags = KEYEVENTF_KEYUP;
                        SendInput(2, in, sizeof(INPUT));
                        if (g_hwnd) PostMessageW(g_hwnd, WM_WINKEY, 0, 0);
                    }
                }
            } else if (g_winDown && dn) {
                g_winCombo = true; DWORD vk = k->vkCode;
                if (vk == 'I' || vk == 'R' || vk == 'D' || vk == 'E' || vk == 'S' || vk == 'Q' || vk == 'P') {
                    bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
                    if (g_hwnd) PostMessageW(g_hwnd, WM_WINCOMBO, (WPARAM)vk, shift ? 1 : 0);
                    return 1;   // комбинацию обрабатываем сами, остальные (Win+L, Win+V, Win+стрелки…) отдаём системе
                }
            }
        }
    }
    return CallNextHookEx(nullptr, n, w, l);
}
static void DoWinCombo(DWORD vk, bool shift) {
    switch (vk) {
    case 'I': ShellExecuteW(nullptr, L"open", L"ms-settings:", nullptr, nullptr, SW_SHOWNORMAL); break;
    case 'R': {
        typedef void (WINAPI* RunDlgFn)(HWND, HICON, LPCWSTR, LPCWSTR, LPCWSTR, UINT);
        RunDlgFn f = (RunDlgFn)GetProcAddress(GetModuleHandleW(L"shell32.dll"), MAKEINTRESOURCEA(61));
        if (f) f(nullptr, nullptr, nullptr, nullptr, nullptr, 0); break; }
    case 'D': ShowShell(); OpenStart(false); break;
    case 'E': ShellRun(L"shell:MyComputerFolder", L"", nullptr); break;
    case 'S': if (shift) { ShellExecuteW(nullptr, L"open", L"ms-screenclip:", nullptr, nullptr, SW_SHOWNORMAL); break; } /* fallthrough */
    case 'Q': ShowShell(); OpenStart(true); break;
    case 'P': ShellExecuteW(nullptr, L"open", L"DisplaySwitch.exe", nullptr, nullptr, SW_SHOWNORMAL); break;
    }
}

// ============================ 16. ОКОННЫЕ ПРОЦЕДУРЫ ============================
static Pt ToDip(LPARAM lp) { return Pt{ (float)GET_X_LPARAM(lp) * 96.f / g_dpi, (float)GET_Y_LPARAM(lp) * 96.f / g_dpi }; }
static void UpdateHover(Pt p) { const Hit* h = HitAt(p); g_hover = h ? h->id : 0; g_cursorKind = h ? h->cur : 0; }
static void OnDown(Pt p) {
    SetCapture(g_hwnd); g_mouse = p; UpdateHover(p);
    const Hit* h = HitAt(p); g_downId = h ? h->id : 0; g_downValid = (h != nullptr); g_didDrag = false; g_downPos = p;
    if (g_focusId && (!h || h->cur != 2) && !g_modalOpen) SetFocusField(0);
    if (h) { g_downHit = *h; if (g_downHit.drag) g_downHit.drag(p, 0); }
    Invalidate();
}
static std::wstring ClipText() {
    std::wstring r; if (!OpenClipboard(g_hwnd)) return r;
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (h) { const wchar_t* s = (const wchar_t*)GlobalLock(h); if (s) { for (; *s && r.size() < 256; ++s) if (*s >= 32) r += *s; GlobalUnlock(h); } }
    CloseClipboard(); return r;
}
static void FieldChanged(int id) {
    if (id == FLD_SEARCH) { g_scr[0] = 0; g_scrTw[0].jump(0); }
    Invalidate();
}
static void OnChar(wchar_t c) {
    if (g_startOpen && !g_modalOpen && !g_focusId && g_tabIdx == 0 && c >= 32) SetFocusField(FLD_SEARCH);
    std::wstring* b = FieldBuf(g_focusId); if (!b) return;
    if (c == 8) { if (!b->empty()) b->pop_back(); FieldChanged(g_focusId); }
    else if (c == 13) {
        if (g_focusId == FLD_RENAME) CloseModal(true);
        else if (g_focusId == FLD_SEARCH) { if (!g_selApps.empty()) LaunchSelectedApps(); else if (g_firstApp >= 0 && g_firstApp < (int)g_appsUI.size()) { AppEntry a = g_appsUI[g_firstApp]; LaunchApp(a); } }
        else SetFocusField(0);
    }
    else if (c == 22) { std::wstring t = ClipText(); b->append(t); if (b->size() > 200) b->resize(200); FieldChanged(g_focusId); }
    else if (c >= 32) {
        if (b->size() < 200) { b->push_back(c); FieldChanged(g_focusId); }
    }
}
static LRESULT CALLBACK MiniProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    static bool hov = false, track = false;
    switch (m) {
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_MOUSEMOVE:
        if (!track) { TRACKMOUSEEVENT t = {}; t.cbSize = sizeof(t); t.dwFlags = TME_LEAVE; t.hwndTrack = h; TrackMouseEvent(&t); track = true; }
        if (!hov) { hov = true; InvalidateRect(h, nullptr, FALSE); } return 0;
    case WM_MOUSELEAVE: hov = false; track = false; InvalidateRect(h, nullptr, FALSE); return 0;
    case WM_LBUTTONUP: ShowShell(); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); RECT rc; GetClientRect(h, &rc);
        Col a = ThemeCol(T_ACC); if (!hov) a = Mix(a, Col{ 0.1f, 0.1f, 0.14f, 1.f }, 0.35f);
        HBRUSH br = CreateSolidBrush(RGB((int)(a.r * 255), (int)(a.g * 255), (int)(a.b * 255))); FillRect(dc, &rc, br); DeleteObject(br);
        HBRUSH dk = CreateSolidBrush(RGB(14, 16, 26)); int s = MulDiv(7, (int)g_dpi, 96), g = MulDiv(3, (int)g_dpi, 96);
        int ox = (rc.right - 16 - (2 * s + g)) / 2 + 2, oy = (rc.bottom - (2 * s + g)) / 2;
        for (int i = 0; i < 2; ++i) for (int j = 0; j < 2; ++j) { RECT q = { ox + i * (s + g), oy + j * (s + g), ox + i * (s + g) + s, oy + j * (s + g) + s }; FillRect(dc, &q, dk); }
        DeleteObject(dk); EndPaint(h, &ps); return 0; }
    }
    return DefWindowProcW(h, m, w, l);
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (!g_hwnd) g_hwnd = h;
    g_now = NowSec();
    switch (m) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(h, &ps); Render(); EndPaint(h, &ps); return 0; }
    case WM_SIZE: Invalidate(); return 0;
    case WM_DPICHANGED: g_dpi = LOWORD(w); if (g_rt) g_rt->SetDpi((float)g_dpi, (float)g_dpi); ApplyRect(); return 0;
    case WM_DISPLAYCHANGE: ApplyRect(); return 0;
    case WM_APPBAR: if (w == ABN_POSCHANGED) AppBarUpdate(); return 0;
    case WM_SETTINGCHANGE: if (w == SPI_SETWORKAREA) ApplyRect(); return 0;   // панель задач переехала/перезапустился Проводник
    case WM_ACTIVATE: if (LOWORD(w) != WA_INACTIVE) Kick(0.2); return 0;
    case WM_TIMER:
        if (w == 1) {
            if (g_now < g_animUntil) Invalidate();
            else if (g_cfg.animBg && GetForegroundWindow() == h && !IsIconic(h)) { static int sk = 0; if ((sk++ & 1) == 0) Invalidate(); }
            else { KillTimer(h, 1); g_timerOn = false; }
        }
        else if (w == 2) { SYSTEMTIME st; GetLocalTime(&st); static int lm = -1; if (st.wMinute != lm) { lm = st.wMinute; Invalidate(); } if (g_dirty) SaveConfig(); }
        else if (w == 3) FocusTick();
        else if (w == 7) WatchFullscreen();
        else if (w == 6) Invalidate();
        return 0;
    case WM_APPS: { std::lock_guard<std::mutex> l2(g_appsMx); g_appsUI = g_apps; } Invalidate(); return 0;
    case WM_ICON: Invalidate(); return 0;
    case WM_WINKEY: { bool fg = (GetForegroundWindow() == h); ShowShell(); OpenStart(fg ? !g_startOpen : true); return 0; }
    case WM_WINCOMBO: DoWinCombo((DWORD)w, l != 0); return 0;
    case WM_MOUSEMOVE: {
        Pt p = ToDip(l); g_mouse = p; UpdateHover(p);
        if ((w & MK_LBUTTON) && g_downValid) {
            float dx = p.x - g_downPos.x, dy = p.y - g_downPos.y; if (!g_didDrag && dx * dx + dy * dy > 36.f) g_didDrag = true;
            if (g_downHit.drag) g_downHit.drag(p, 1);
        }
        Invalidate(); return 0; }
    case WM_LBUTTONDOWN: OnDown(ToDip(l)); return 0;
    case WM_LBUTTONDBLCLK: {
        Pt p = ToDip(l); g_mouse = p; UpdateHover(p); const Hit* ht = HitAt(p);
        if (ht && ht->dbl) { auto fn = ht->dbl; g_downValid = false; fn(); } else OnDown(p);
        return 0; }
    case WM_LBUTTONUP: {
        ReleaseCapture(); Pt p = ToDip(l); g_mouse = p;
        if (g_downValid) {
            Hit d = g_downHit; if (d.drag) d.drag(p, 2);
            if (!g_didDrag) { const Hit* ht = HitAt(p); if (ht && ht->id == d.id && ht->click) { auto fn = ht->click; g_downValid = false; fn(); } }
        }
        g_downValid = false; g_downId = 0; g_didDrag = false; if (g_dirty) SaveConfig(); UpdateHover(p); Invalidate(); return 0; }
    case WM_RBUTTONUP: { Pt p = ToDip(l); g_mouse = p; UpdateHover(p); const Hit* ht = HitAt(p); if (ht && ht->right) { auto fn = ht->right; fn(); } Invalidate(); return 0; }
    case WM_MOUSEWHEEL: {
        POINT pt = { GET_X_LPARAM(l), GET_Y_LPARAM(l) }; ScreenToClient(h, &pt);
        Pt p = { pt.x * 96.f / g_dpi, pt.y * 96.f / g_dpi }; if (g_mpOpen && g_mpTab == 1 && !g_mpSynced && In(g_mpLyrView, p)) { g_lyrScr = Cl(g_lyrScr - (float)GET_WHEEL_DELTA_WPARAM(w) * 0.5f, 0.f, g_lyrMax); Invalidate(); return 0; }
        if (g_mpOpen && !(g_mpTab == 1 && In(g_mpLyrView, p))) { MediaKey(GET_WHEEL_DELTA_WPARAM(w) > 0 ? VK_VOLUME_UP : VK_VOLUME_DOWN); return 0; }
        if (!g_startOpen || g_modalOpen || g_ctxOpen) return 0;
        int i = g_tabIdx; if (!In(g_view[i], p)) return 0;
        g_scr[i] = Cl(g_scr[i] - (float)GET_WHEEL_DELTA_WPARAM(w) * 0.75f, 0.f, g_scrMax[i]); g_scrTw[i].go(g_scr[i], 0.2); return 0; }
    case WM_SETCURSOR:
        if (LOWORD(l) == HTCLIENT) { SetCursor(LoadCursorW(nullptr, g_cursorKind == 1 ? IDC_HAND : g_cursorKind == 2 ? IDC_IBEAM : IDC_ARROW)); return TRUE; }
        break;
    case WM_CHAR: OnChar((wchar_t)w); return 0;
    case WM_KEYDOWN:
        if (w == VK_ESCAPE) {
            if (g_modalOpen) CloseModal(false); else if (g_ctxOpen) CloseCtx(); else if (g_focusId && g_focusId != FLD_SEARCH) SetFocusField(0); else if (g_startOpen) OpenStart(false); else if (!g_selIcons.empty()) { g_selIcons.clear(); Invalidate(); }
        }
        else if (w == VK_RETURN && !g_startOpen && !g_modalOpen && !g_focusId && !g_selIcons.empty()) LaunchSelectedIcons();
        return 0;
    case WM_HOTKEY: if (w == 1) QuitShell(); else ShowShell(); return 0;
    case WM_CLOSE: return 0;                     // Alt+F4 игнорируем; выход — Пуск → «Выйти» или Ctrl+Alt+Q
    case WM_QUERYENDSESSION: return TRUE;
    case WM_ENDSESSION: if (w) { SaveConfig(); RestoreSystem(); } return 0;
    case WM_DESTROY:
        if (g_kbHook) { UnhookWindowsHookEx(g_kbHook); g_kbHook = nullptr; }
        UnregisterHotKey(h, 1); UnregisterHotKey(h, 2); UnregisterHotKey(h, 3);
        KillTimer(h, 1); KillTimer(h, 2); KillTimer(h, 3); KillTimer(h, 6); KillTimer(h, 7);
        SaveConfig(); RestoreSystem(); SetEvent(g_quitEv); if (g_mini) DestroyWindow(g_mini);
        PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

// ============================ 17. wWinMain ============================
int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, PWSTR cmd, int) {
    if (cmd && wcsstr(cmd, L"/restore")) { RestoreSystem(); return 0; }       // аварийный запуск: вернуть панель и рабочий стол
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (cmd && wcsstr(cmd, L"/respawn")) { g_respawned = true; Sleep(1500); }
    HANDLE mtx = CreateMutexW(nullptr, TRUE, L"Local\\DimaKorzanShell_Single_9C1A7");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND w = FindWindowW(L"DimaKorzanShellMain", nullptr); if (w) { ShowWindow(w, SW_SHOW); SetForegroundWindow(w); }
        return 0;
    }
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 1;
    g_hi = hi; atexit(RestoreSystem); SetUnhandledExceptionFilter(CrashFilter);
    g_quitEv = CreateEventW(nullptr, TRUE, FALSE, nullptr); g_icoEv = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    g_lyrEv = CreateEventW(nullptr, FALSE, FALSE, nullptr); g_medEv = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    g_now = NowSec(); LoadConfig(); ApplyScale(); RebuildAllow();
    g_genToken = g_cfg.geniusToken.empty() ? std::string(kGeniusDefaultToken) : W2U(g_cfg.geniusToken);
    if (!g_cfg.imported) { ImportDesktop(); g_cfg.imported = true; MarkDirty(); }
    for (int k = 0; k < T_N; ++k) g_thFrom[k] = g_themes[g_cfg.theme].c[k];
    g_thT.jump(1.f);
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory), nullptr, (void**)&g_d2d))) return 1;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&g_dw))) return 1;
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&g_wic));

    WNDCLASSEXW wc = {}; wc.cbSize = sizeof(wc); wc.style = CS_DBLCLKS; wc.lpfnWndProc = WndProc; wc.hInstance = hi;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.lpszClassName = L"DimaKorzanShellMain"; RegisterClassExW(&wc);
    WNDCLASSEXW wm = {}; wm.cbSize = sizeof(wm); wm.lpfnWndProc = MiniProc; wm.hInstance = hi; wm.hCursor = LoadCursorW(nullptr, IDC_HAND);
    wm.lpszClassName = L"DimaKorzanShellMini"; RegisterClassExW(&wm);

    g_hwnd = CreateWindowExW(0, L"DimaKorzanShellMain", L"DimaKorzanShell", WS_POPUP, 0, 0, 640, 480, nullptr, nullptr, hi, nullptr);
    if (!g_hwnd) return 1;
    g_dpi = GetDpiForWindow(g_hwnd);
    int pref = 1; DwmSetWindowAttribute(g_hwnd, 33 /*DWMWA_WINDOW_CORNER_PREFERENCE*/, &pref, sizeof(pref));   // без скруглений окна
    g_mini = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED, L"DimaKorzanShellMini", L"", WS_POPUP, 0, 0, 30, 76, nullptr, nullptr, hi, nullptr);
    if (g_mini) SetLayeredWindowAttributes(g_mini, 0, 215, LWA_ALPHA);
    if (g_cfg.killExplorer) { KillExplorer(); g_killsLeft = 2; g_killDeadline = NowSec() + 8.0; }
    if (g_cfg.hideTaskbar) HideSystemUi();
    ApplyRect(); ShowWindow(g_hwnd, SW_SHOW); ForceFg(g_hwnd);

    RegisterHotKey(g_hwnd, 1, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'Q');               // аварийный выход
    RegisterHotKey(g_hwnd, 3, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, VK_SPACE);            // запасной вариант возврата в оболочку
    g_kbHook = SetWindowsHookExW(WH_KEYBOARD_LL, KbProc, g_hi, 0);
    SetTimer(g_hwnd, 2, 1000, nullptr); SetTimer(g_hwnd, 3, 700, nullptr); SetTimer(g_hwnd, 7, 250, nullptr);

    DiscSetClient(kDiscordAppId); UpdatePresence();
    HANDLE th[5];
    th[0] = CreateThread(nullptr, 0, AppsThread, nullptr, 0, nullptr);
    th[1] = CreateThread(nullptr, 0, IconThread, nullptr, 0, nullptr);
    th[2] = CreateThread(nullptr, 0, DiscordThread, nullptr, 0, nullptr);
    th[3] = CreateThread(nullptr, 0, MediaThread, nullptr, 0, nullptr);
    th[4] = CreateThread(nullptr, 0, LyricsThread, nullptr, 0, nullptr);

    g_now = NowSec(); g_tb.jump(0); g_tb.go(1, 0.8, E_CUBIC); g_toast.jump(0);
    FocusTick(); Kick(1.0);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }

    RestoreSystem(); SetEvent(g_quitEv);
    WaitForMultipleObjects(5, th, TRUE, 1500);
    DiscardRT();
    for (auto& kv : g_fmt) if (kv.second) kv.second->Release();
    if (g_wic) g_wic->Release(); if (g_dw) g_dw->Release(); if (g_d2d) g_d2d->Release();
    CoUninitialize(); if (mtx) ReleaseMutex(mtx);
    ExitProcess(0);   // не гоняем деструкторы статиков при ещё живых потоках
}