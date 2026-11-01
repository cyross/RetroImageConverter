// RetroImageConv - Win32 GUI
// SPDX-License-Identifier: MIT
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <objbase.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>

#include <atomic>
#include <iterator>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "resource.h"
#include "../core/Converter.h"
#include "../core/ImageIO.h"
#include "../core/Resize.h"

#ifdef _MSC_VER
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "msimg32.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif

using namespace ric;

namespace {

const wchar_t* kAppName = L"RetroImageConv";
const wchar_t* kAppVersion = L"1.1.0";

// ---------------------------------------------------------------- 文字列
std::wstring W(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}
std::string U8(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}
std::wstring getText(HWND h) {
    int n = GetWindowTextLengthW(h);
    std::wstring s((size_t)n + 1, L'\0');
    GetWindowTextW(h, s.data(), n + 1);
    s.resize((size_t)n);
    return s;
}
int getInt(HWND h, int def) {
    std::wstring s = getText(h);
    if (s.empty()) return def;
    return _wtoi(s.c_str());
}

// ---------------------------------------------------------------- ID
enum : int {
    IDC_OPEN = 1001, IDC_SRCINFO, IDC_PRESET, IDC_MODE, IDC_MODEINFO, IDC_RECOMMEND,
    IDC_SCALE, IDC_W, IDC_H, IDC_ASPECT, IDC_FILTER,
    IDC_CROPX, IDC_CROPX_V, IDC_CROPY, IDC_CROPY_V, IDC_CROPZ, IDC_CROPZ_V, IDC_CROPRESET, IDC_CROPHINT,
    IDC_COLORS, IDC_METRIC, IDC_BLACK, IDC_TILECON, IDC_LINEAR,
    IDC_DITHER, IDC_STRENGTH, IDC_STRENGTH_V, IDC_SERP, IDC_TILING, IDC_STYLE,
    IDC_BRI, IDC_BRI_V, IDC_CON, IDC_CON_V, IDC_SAT, IDC_SAT_V, IDC_GAM, IDC_GAM_V, IDC_ADJRESET,
    IDC_ALPHA, IDC_BGCOLOR, IDC_THRESH,
    IDC_FORMAT, IDC_QUALITY, IDC_OUTPAR, IDC_OUTZOOM, IDC_MAGUSER, IDC_MAGMEMO, IDC_SAVE,
    IDC_VIEW, IDC_ZOOM, IDC_VIEWPAR, IDC_STATUS,
    IDM_OPEN = 2001, IDM_SAVE, IDM_EXIT, IDM_RECONVERT, IDM_MAGINFO, IDM_ABOUT,
};
const UINT WM_APP_DONE = WM_APP + 1;
const UINT_PTR TIMER_CONVERT = 1;

// ---------------------------------------------------------------- プレビュー
struct DisplayBuf {
    int w = 0, h = 0;
    double par = 1.0;
    std::vector<uint32_t> bgra;
    void build(const Image& img) {
        w = img.width; h = img.height; par = img.pixelAspect;
        bgra.assign((size_t)w * h, 0);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const RGBA8& c = img.at(x, y);
                int chk = (((x >> 3) + (y >> 3)) & 1) ? 0x80 : 0x60;
                int a = c.a;
                int r = (c.r * a + chk * (255 - a)) / 255;
                int g = (c.g * a + chk * (255 - a)) / 255;
                int b = (c.b * a + chk * (255 - a)) / 255;
                bgra[(size_t)y * w + x] = (uint32_t)(b | (g << 8) | (r << 16));
            }
    }
    bool empty() const { return w == 0 || h == 0; }
};

struct App {
    HINSTANCE inst = nullptr;
    HWND hwnd = nullptr, panel = nullptr, preview = nullptr, status = nullptr;
    HFONT font = nullptr, fontBold = nullptr;
    UINT dpi = 96;
    int panelScroll = 0, panelContentH = 0;

    // パネルの行
    struct Item { HWND h; int weight; int fixedDip; };
    struct Row { HWND label = nullptr; std::vector<Item> items; int heightDip = 26; bool header = false; };
    std::vector<Row> rows;
    std::vector<HWND> toolbar;

    HWND c[3000] = {}; // ID → HWND

    // 画像
    std::filesystem::path srcPath;
    std::shared_ptr<const Image> src;
    LoadedImage loaded;
    std::unique_ptr<ConvertResult> result;
    DisplayBuf dispSrc, dispDst;
    RGBA8 bg{ 0, 0, 0, 255 };

    // 変換ジョブ
    uint64_t gen = 0;
    std::shared_ptr<std::atomic<bool>> cancel;
    bool busy = false;
    bool suppress = false;

    // プレビュー状態
    int viewMode = 0;  // 0:変換後 1:元画像 2:並べて
    double zoom = 0;   // 0 = フィット
    bool viewPar = true;
    int panX = 0, panY = 0;
    bool dragging = false;
    POINT dragStart{};
    int panStartX = 0, panStartY = 0;

    // 切り抜き範囲のドラッグ
    bool srcDrawValid = false;
    double srcDrawX = 0, srcDrawY = 0, srcDrawW = 0, srcDrawH = 0; // 元画像を描いた画面上の矩形
    bool cropDragging = false;
    POINT cropDragStart{};
    double cropStartX = 0, cropStartY = 0;   // ドラッグ開始時の切り抜き左上 (元画像座標)
    float cropX = 0.5f, cropY = 0.5f, cropZoom = 1.0f;

    int S(int dip) const { return MulDiv(dip, (int)dpi, 96); }
};
App g;

HWND C(int id) { return g.c[id - 1000]; }

// ---------------------------------------------------------------- コントロール生成
HWND mk(const wchar_t* cls, const wchar_t* text, DWORD style, int id, HWND parent, DWORD ex = 0) {
    HWND h = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, parent,
                             (HMENU)(INT_PTR)id, g.inst, nullptr);
    if (id >= 1000 && id < 4000) g.c[id - 1000] = h;
    return h;
}
HWND mkCombo(int id, const std::vector<std::wstring>& items, HWND parent = nullptr) {
    HWND h = mk(WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, id, parent ? parent : g.panel);
    for (auto& s : items) SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)s.c_str());
    SendMessageW(h, CB_SETCURSEL, 0, 0);
    return h;
}
std::vector<std::wstring> names(const std::vector<EnumName>& t) {
    std::vector<std::wstring> v;
    for (auto& e : t) v.push_back(W(e.label));
    return v;
}
HWND mkCheck(int id, const wchar_t* text, bool on = false) {
    HWND h = mk(WC_BUTTONW, text, BS_AUTOCHECKBOX | WS_TABSTOP, id, g.panel);
    Button_SetCheck(h, on ? BST_CHECKED : BST_UNCHECKED);
    return h;
}
HWND mkButton(int id, const wchar_t* text) { return mk(WC_BUTTONW, text, BS_PUSHBUTTON | WS_TABSTOP, id, g.panel); }
HWND mkEdit(int id, const wchar_t* text, bool number) {
    return mk(WC_EDITW, text, ES_AUTOHSCROLL | WS_TABSTOP | (number ? ES_NUMBER : 0), id, g.panel, WS_EX_CLIENTEDGE);
}
HWND mkStatic(const wchar_t* text, int id = 0, DWORD style = 0) { return mk(WC_STATICW, text, SS_LEFT | SS_NOPREFIX | style, id ? id : -1, g.panel); }
HWND mkTrack(int id, int mn, int mx, int pos) {
    HWND h = mk(TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, id, g.panel);
    SendMessageW(h, TBM_SETRANGE, TRUE, MAKELPARAM(mn, mx));
    SendMessageW(h, TBM_SETPOS, TRUE, pos);
    return h;
}

void addHeader(const wchar_t* text) {
    App::Row r; r.header = true; r.heightDip = 24;
    r.label = mkStatic(text);
    g.rows.push_back(r);
}
void addRow(const wchar_t* label, std::vector<App::Item> items, int hDip = 26) {
    App::Row r; r.heightDip = hDip;
    if (label) r.label = mkStatic(label);
    r.items = std::move(items);
    g.rows.push_back(r);
}

int comboSel(int id) { return (int)SendMessageW(C(id), CB_GETCURSEL, 0, 0); }
void comboSet(int id, int i) { SendMessageW(C(id), CB_SETCURSEL, i, 0); }
bool checked(int id) { return Button_GetCheck(C(id)) == BST_CHECKED; }
int trackPos(int id) { return (int)SendMessageW(C(id), TBM_GETPOS, 0, 0); }
void trackSet(int id, int v) { SendMessageW(C(id), TBM_SETPOS, TRUE, v); }

// ---------------------------------------------------------------- レイアウト
void layoutPanel() {
    RECT rc; GetClientRect(g.panel, &rc);
    int w = rc.right;
    {
        // 内容の高さに合わせてスクロール位置を補正
        int total = g.S(6) + g.S(8);
        for (auto& r : g.rows) total += g.S(r.heightDip);
        g.panelScroll = std::clamp(g.panelScroll, 0, std::max(0, total - (int)rc.bottom));
    }
    int y = g.S(6) - g.panelScroll;
    const int labelW = g.S(92), pad = g.S(8), gap = g.S(4);
    HDWP dw = BeginDeferWindowPos((int)g.rows.size() * 3);
    for (auto& r : g.rows) {
        int rh = g.S(r.heightDip);
        if (r.header) {
            dw = DeferWindowPos(dw, r.label, nullptr, pad, y + g.S(4), w - pad * 2, g.S(18), SWP_NOZORDER | SWP_NOACTIVATE);
            y += rh;
            continue;
        }
        int x0 = r.label ? pad + labelW : pad;
        if (r.label) dw = DeferWindowPos(dw, r.label, nullptr, pad, y + g.S(4), labelW - gap, g.S(18), SWP_NOZORDER | SWP_NOACTIVATE);
        int avail = w - pad - x0;
        int fixed = 0, weights = 0;
        for (auto& it : r.items) { if (it.fixedDip) fixed += g.S(it.fixedDip); else weights += it.weight; }
        int flex = std::max(0, avail - fixed - gap * ((int)r.items.size() - 1));
        int x = x0;
        for (size_t i = 0; i < r.items.size(); ++i) {
            auto& it = r.items[i];
            int iw = it.fixedDip ? g.S(it.fixedDip) : (weights ? flex * it.weight / weights : flex);
            if (i + 1 == r.items.size() && !it.fixedDip) iw = w - pad - x;
            wchar_t cls[32] = {};
            GetClassNameW(it.h, cls, 32);
            int ih = rh - g.S(4);
            if (lstrcmpiW(cls, WC_COMBOBOXW) == 0) ih = g.S(320);
            dw = DeferWindowPos(dw, it.h, nullptr, x, y + g.S(1), iw, ih, SWP_NOZORDER | SWP_NOACTIVATE);
            x += iw + gap;
        }
        y += rh;
    }
    EndDeferWindowPos(dw);
    g.panelContentH = y + g.panelScroll + g.S(8);
    SCROLLINFO si{ sizeof(si), SIF_RANGE | SIF_PAGE | SIF_POS, 0, g.panelContentH, (UINT)rc.bottom, g.panelScroll };
    SetScrollInfo(g.panel, SB_VERT, &si, TRUE);
}

void layoutMain() {
    RECT rc; GetClientRect(g.hwnd, &rc);
    SendMessageW(g.status, WM_SIZE, 0, 0);
    RECT sr; GetWindowRect(g.status, &sr);
    int sbH = sr.bottom - sr.top;
    int panelW = g.S(340);
    int h = rc.bottom - sbH;
    SetWindowPos(g.panel, nullptr, 0, 0, panelW, h, SWP_NOZORDER);
    int x = panelW + g.S(6), y = g.S(6);
    int tbH = g.S(24);
    SetWindowPos(C(IDC_VIEW), nullptr, x, y, g.S(150), g.S(300), SWP_NOZORDER);
    SetWindowPos(C(IDC_ZOOM), nullptr, x + g.S(156), y, g.S(110), g.S(300), SWP_NOZORDER);
    SetWindowPos(C(IDC_VIEWPAR), nullptr, x + g.S(272), y, g.S(200), tbH, SWP_NOZORDER);
    int py = y + tbH + g.S(6);
    SetWindowPos(g.preview, nullptr, x, py, std::max(10, (int)rc.right - x - g.S(6)), std::max(10, h - py - g.S(6)), SWP_NOZORDER);
    // ステータスバーの区切り
    int parts[2] = { std::max(100, (int)rc.right - g.S(220)), -1 };
    SendMessageW(g.status, SB_SETPARTS, 2, (LPARAM)parts);
    layoutPanel();
}

void applyFonts() {
    if (g.font) DeleteObject(g.font);
    if (g.fontBold) DeleteObject(g.fontBold);
    NONCLIENTMETRICSW ncm{ sizeof(ncm) };
    SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, g.dpi);
    g.font = CreateFontIndirectW(&ncm.lfMessageFont);
    LOGFONTW lb = ncm.lfMessageFont;
    lb.lfWeight = FW_BOLD;
    g.fontBold = CreateFontIndirectW(&lb);
    EnumChildWindows(g.hwnd, [](HWND h, LPARAM) -> BOOL { SendMessageW(h, WM_SETFONT, (WPARAM)g.font, TRUE); return TRUE; }, 0);
    for (auto& r : g.rows) if (r.header) SendMessageW(r.label, WM_SETFONT, (WPARAM)g.fontBold, TRUE);
}

// ---------------------------------------------------------------- 状態表示
void setStatus(const std::wstring& a, const std::wstring& b = L"") {
    SendMessageW(g.status, SB_SETTEXTW, 0, (LPARAM)a.c_str());
    if (!b.empty()) SendMessageW(g.status, SB_SETTEXTW, 1, (LPARAM)b.c_str());
}

void updateTrackLabels() {
    wchar_t b[32];
    swprintf(b, 32, L"%d%%", trackPos(IDC_STRENGTH)); SetWindowTextW(C(IDC_STRENGTH_V), b);
    swprintf(b, 32, L"%d", trackPos(IDC_BRI)); SetWindowTextW(C(IDC_BRI_V), b);
    swprintf(b, 32, L"%d", trackPos(IDC_CON)); SetWindowTextW(C(IDC_CON_V), b);
    swprintf(b, 32, L"%d", trackPos(IDC_SAT)); SetWindowTextW(C(IDC_SAT_V), b);
    swprintf(b, 32, L"%.2f", trackPos(IDC_GAM) / 100.0); SetWindowTextW(C(IDC_GAM_V), b);
    swprintf(b, 32, L"%.1f%%", g.cropX * 100.0); SetWindowTextW(C(IDC_CROPX_V), b);
    swprintf(b, 32, L"%.1f%%", g.cropY * 100.0); SetWindowTextW(C(IDC_CROPY_V), b);
    swprintf(b, 32, L"x%.2f", g.cropZoom); SetWindowTextW(C(IDC_CROPZ_V), b);
}

void updateBgButton() {
    wchar_t b[32];
    swprintf(b, 32, L"#%02X%02X%02X", g.bg.r, g.bg.g, g.bg.b);
    SetWindowTextW(C(IDC_BGCOLOR), b);
}

const Preset& curPreset() {
    int i = std::clamp(comboSel(IDC_PRESET), 0, (int)presets().size() - 1);
    return presets()[i];
}

void updateModeInfo() {
    const Preset& p = curPreset();
    int mi = std::clamp(comboSel(IDC_MODE), 0, (int)p.modes.size() - 1);
    SetWindowTextW(C(IDC_MODEINFO), W(p.modes[mi].summary()).c_str());
    bool custom = comboSel(IDC_SCALE) == 2;
    EnableWindow(C(IDC_W), custom);
    EnableWindow(C(IDC_H), custom);
    bool crop = comboSel(IDC_ASPECT) == enumIndex(aspectNames(), (int)AspectMode::Crop) && comboSel(IDC_SCALE) != 1;
    for (int id : { IDC_CROPX, IDC_CROPY, IDC_CROPZ, IDC_CROPRESET }) EnableWindow(C(id), crop);
    EnableWindow(C(IDC_QUALITY), outputFormats()[std::max(0, comboSel(IDC_FORMAT))] == FileFormat::JPG);
    bool mag = outputFormats()[std::max(0, comboSel(IDC_FORMAT))] == FileFormat::MAG;
    EnableWindow(C(IDC_MAGUSER), mag);
    EnableWindow(C(IDC_MAGMEMO), mag);
}

void applyRecommendedToControls() {
    const Preset& p = curPreset();
    int mi = std::clamp(comboSel(IDC_MODE), 0, (int)p.modes.size() - 1);
    ConvertOptions o;
    applyRecommended(p.modes[mi], o);
    g.suppress = true;
    comboSet(IDC_DITHER, enumIndex(ditherNames(), (int)o.dither));
    trackSet(IDC_STRENGTH, (int)std::lround(o.ditherStrength * 100));
    comboSet(IDC_TILING, enumIndex(tilingNames(), (int)o.tiling));
    comboSet(IDC_STYLE, enumIndex(tileStyleNames(), (int)o.tileStyle));
    g.suppress = false;
    updateTrackLabels();
}

void fillModes() {
    const Preset& p = curPreset();
    SendMessageW(C(IDC_MODE), CB_RESETCONTENT, 0, 0);
    for (auto& m : p.modes) SendMessageW(C(IDC_MODE), CB_ADDSTRING, 0, (LPARAM)W(m.label).c_str());
    comboSet(IDC_MODE, 0);
}

ConvertOptions gatherOptions() {
    ConvertOptions o;
    const Preset& p = curPreset();
    o.presetId = p.id;
    o.modeIndex = std::max(0, comboSel(IDC_MODE));
    o.scale = (ScaleMode)scaleNames()[std::max(0, comboSel(IDC_SCALE))].value;
    o.customWidth = std::clamp(getInt(C(IDC_W), 640), 1, 16384);
    o.customHeight = std::clamp(getInt(C(IDC_H), 400), 1, 16384);
    o.aspect = (AspectMode)aspectNames()[std::max(0, comboSel(IDC_ASPECT))].value;
    o.cropX = g.cropX;
    o.cropY = g.cropY;
    o.cropZoom = g.cropZoom;
    o.filter = (ResizeFilter)filterNames()[std::max(0, comboSel(IDC_FILTER))].value;
    o.colorsOverride = colorsNames()[std::max(0, comboSel(IDC_COLORS))].value;
    o.metric = (DistanceMetric)metricNames()[std::max(0, comboSel(IDC_METRIC))].value;
    o.includeBlack = checked(IDC_BLACK);
    o.keepTileConstraint = checked(IDC_TILECON);
    o.linearLight = checked(IDC_LINEAR);
    o.dither = (DitherMethod)ditherNames()[std::max(0, comboSel(IDC_DITHER))].value;
    o.ditherStrength = trackPos(IDC_STRENGTH) / 100.f;
    o.serpentine = checked(IDC_SERP);
    o.tiling = (TilingMode)tilingNames()[std::max(0, comboSel(IDC_TILING))].value;
    o.tileStyle = (TileStyle)tileStyleNames()[std::max(0, comboSel(IDC_STYLE))].value;
    o.brightness = (float)trackPos(IDC_BRI);
    o.contrast = (float)trackPos(IDC_CON);
    o.saturation = (float)trackPos(IDC_SAT);
    o.gamma = trackPos(IDC_GAM) / 100.f;
    o.alpha = (AlphaMode)alphaNames()[std::max(0, comboSel(IDC_ALPHA))].value;
    o.background = g.bg;
    o.alphaThreshold = std::clamp(getInt(C(IDC_THRESH), 128), 0, 255);
    return o;
}

SaveParams gatherSaveParams() {
    SaveParams sp;
    sp.format = outputFormats()[std::max(0, comboSel(IDC_FORMAT))];
    sp.jpegQuality = std::clamp(getInt(C(IDC_QUALITY), 90), 1, 100);
    sp.parCorrect = checked(IDC_OUTPAR);
    sp.zoom = std::max(1, comboSel(IDC_OUTZOOM) + 1);
    sp.mag.user = utf8ToSjis(U8(getText(C(IDC_MAGUSER))));
    sp.mag.memo = utf8ToSjis(U8(getText(C(IDC_MAGMEMO))));
    return sp;
}

// ---------------------------------------------------------------- 変換
void scheduleConvert() {
    if (g.suppress || !g.src) return;
    SetTimer(g.hwnd, TIMER_CONVERT, 120, nullptr);
}

void startConvert() {
    KillTimer(g.hwnd, TIMER_CONVERT);
    if (!g.src) return;
    if (g.cancel) g.cancel->store(true);
    g.cancel = std::make_shared<std::atomic<bool>>(false);
    uint64_t id = ++g.gen;
    auto src = g.src;
    auto cancel = g.cancel;
    ConvertOptions opt = gatherOptions();
    HWND hwnd = g.hwnd;
    g.busy = true;
    setStatus(L"変換中...");
    std::thread([=]() {
        auto* r = new ConvertResult(convert(*src, opt, cancel.get()));
        if (!PostMessageW(hwnd, WM_APP_DONE, (WPARAM)id, (LPARAM)r)) delete r;
    }).detach();
}

void onConvertDone(uint64_t id, ConvertResult* r) {
    std::unique_ptr<ConvertResult> res(r);
    if (id != g.gen) return;
    g.busy = false;
    if (!res->ok) {
        if (res->error != "cancel") setStatus(L"変換エラー: " + W(res->error));
        return;
    }
    g.result = std::move(res);
    g.dispDst.build(g.result->image);
    wchar_t b[256];
    swprintf(b, 256, L"変換後: %dx%d  使用色数 %d  (%.2f秒)  %ls", g.result->image.width, g.result->image.height,
             g.result->usedColors, g.result->seconds, W(g.result->mode.label).c_str());
    setStatus(b);
    InvalidateRect(g.preview, nullptr, FALSE);
}

// ---------------------------------------------------------------- ファイル
void updateSourceInfo() {
    std::wstring s;
    if (!g.src) s = L"(画像をドロップするか「開く」で選択)";
    else {
        wchar_t b[256];
        swprintf(b, 256, L"%ls  %dx%d", g.srcPath.filename().wstring().c_str(), g.src->width, g.src->height);
        s = b;
        if (g.loaded.isMag) {
            swprintf(b, 256, L"\nMAG %d色 %ls", g.loaded.magInfo.colors, W(g.loaded.magInfo.describeScreenMode()).c_str());
            s += b;
        }
    }
    SetWindowTextW(C(IDC_SRCINFO), s.c_str());
}

void loadFile(const std::filesystem::path& p) {
    LoadedImage li;
    std::string err;
    HCURSOR old = SetCursor(LoadCursor(nullptr, IDC_WAIT));
    bool ok = loadImageFile(p, li, err);
    SetCursor(old);
    if (!ok) {
        MessageBoxW(g.hwnd, (L"読み込みに失敗しました。\n" + W(err)).c_str(), kAppName, MB_ICONERROR);
        return;
    }
    if (!li.warnings.empty()) MessageBoxW(g.hwnd, W(li.warnings).c_str(), kAppName, MB_ICONWARNING);
    g.srcPath = p;
    g.loaded = std::move(li);
    g.src = std::make_shared<const Image>(g.loaded.image);
    g.dispSrc.build(*g.src);
    g.result.reset();
    g.dispDst = DisplayBuf{};
    g.panX = g.panY = 0;
    updateSourceInfo();
    std::wstring title = std::wstring(kAppName) + L" - " + p.filename().wstring();
    SetWindowTextW(g.hwnd, title.c_str());
    startConvert();
    InvalidateRect(g.preview, nullptr, FALSE);
}

void onOpen() {
    wchar_t file[MAX_PATH * 4] = {};
    OPENFILENAMEW ofn{ sizeof(ofn) };
    ofn.hwndOwner = g.hwnd;
    ofn.lpstrFilter = L"画像ファイル (*.png;*.jpg;*.jpeg;*.gif;*.bmp;*.webp;*.mag;*.tif;*.tiff)\0*.png;*.jpg;*.jpeg;*.gif;*.bmp;*.webp;*.mag;*.tif;*.tiff\0"
                      L"MAG ファイル (*.mag)\0*.mag\0すべてのファイル (*.*)\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = (DWORD)std::size(file);
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    if (GetOpenFileNameW(&ofn)) loadFile(file);
}

void onSave() {
    if (!g.src) { MessageBoxW(g.hwnd, L"画像が読み込まれていません。", kAppName, MB_ICONINFORMATION); return; }
    SaveParams sp = gatherSaveParams();
    auto fmts = outputFormats();
    std::wstring filter;
    for (auto f : fmts) {
        std::wstring ext = W(formatExtension(f));
        filter += W(formatName(f)) + L" (*" + ext + L")";
        filter.push_back(L'\0');
        filter += L"*" + ext;
        filter.push_back(L'\0');
    }
    filter.push_back(L'\0');
    std::wstring def = g.srcPath.stem().wstring() + L"_" + W(curPreset().id) + W(formatExtension(sp.format));
    wchar_t file[MAX_PATH * 4] = {};
    lstrcpynW(file, def.c_str(), (int)std::size(file));
    std::wstring dir = g.srcPath.parent_path().wstring();
    OPENFILENAMEW ofn{ sizeof(ofn) };
    ofn.hwndOwner = g.hwnd;
    ofn.lpstrFilter = filter.c_str();
    ofn.nFilterIndex = (DWORD)(std::max(0, comboSel(IDC_FORMAT)) + 1);
    ofn.lpstrFile = file;
    ofn.nMaxFile = (DWORD)std::size(file);
    ofn.lpstrInitialDir = dir.c_str();
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    if (!GetSaveFileNameW(&ofn)) return;
    std::filesystem::path out = file;
    if (ofn.nFilterIndex >= 1 && ofn.nFilterIndex <= fmts.size()) sp.format = fmts[ofn.nFilterIndex - 1];
    if (formatFromPath(out) != sp.format) out += W(formatExtension(sp.format));

    // 表示中の結果が最新でなければ同期変換
    std::unique_ptr<ConvertResult> fresh;
    const ConvertResult* r = g.result.get();
    if (!r || g.busy) {
        HCURSOR old = SetCursor(LoadCursor(nullptr, IDC_WAIT));
        fresh = std::make_unique<ConvertResult>(convert(*g.src, gatherOptions()));
        SetCursor(old);
        r = fresh.get();
    }
    if (!r->ok) { MessageBoxW(g.hwnd, (L"変換に失敗しました。\n" + W(r->error)).c_str(), kAppName, MB_ICONERROR); return; }
    sp.mag.screenMode = r->mode.magScreenMode;
    std::string err;
    if (!saveImageFile(out, r->image, r->indexed ? &r->indexedImage : nullptr, sp, err)) {
        MessageBoxW(g.hwnd, (L"保存に失敗しました。\n" + W(err)).c_str(), kAppName, MB_ICONERROR);
        return;
    }
    setStatus(L"保存しました: " + out.filename().wstring());
}

void showMagInfo() {
    if (!g.loaded.isMag) { MessageBoxW(g.hwnd, L"MAG ファイルが読み込まれていません。", kAppName, MB_ICONINFORMATION); return; }
    const MagInfo& m = g.loaded.magInfo;
    wchar_t b[1024];
    swprintf(b, 1024, L"機種名: %ls\nユーザー: %ls\nメモ: %ls\n\n色数: %d\nスクリーンモード: %ls\n機種コード: $%02X  機種依存フラグ: $%02X\n表示範囲: (%d,%d)-(%d,%d)",
             W(sjisToUtf8(m.machineName)).c_str(), W(sjisToUtf8(m.user)).c_str(), W(sjisToUtf8(m.memo)).c_str(),
             m.colors, W(m.describeScreenMode()).c_str(), m.machineCode, m.machineFlags, m.x0, m.y0, m.x1, m.y1);
    MessageBoxW(g.hwnd, b, L"MAG 情報", MB_ICONINFORMATION);
}

// ---------------------------------------------------------------- プレビュー描画
void imageDisplaySize(const DisplayBuf& d, double& dw, double& dh) {
    double sx = 1, sy = 1;
    if (g.viewPar && std::fabs(d.par - 1.0) > 1e-3) {
        if (d.par < 1) sy = 1.0 / d.par; else sx = d.par;
    }
    dw = d.w * sx; dh = d.h * sy;
}

bool drawImage(HDC dc, const DisplayBuf& d, RECT area, const wchar_t* caption, RECT* drawn = nullptr) {
    if (d.empty()) {
        SetTextColor(dc, RGB(160, 160, 160));
        SetBkMode(dc, TRANSPARENT);
        DrawTextW(dc, caption, -1, &area, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return false;
    }
    double dw, dh;
    imageDisplaySize(d, dw, dh);
    int aw = area.right - area.left, ah = area.bottom - area.top;
    double scale = g.zoom > 0 ? g.zoom : std::min(aw / dw, ah / dh);
    if (scale <= 0) return false;
    int ow = std::max(1, (int)std::lround(dw * scale)), oh = std::max(1, (int)std::lround(dh * scale));
    int x = area.left + (aw - ow) / 2 + (g.zoom > 0 ? g.panX : 0);
    int y = area.top + (ah - oh) / 2 + (g.zoom > 0 ? g.panY : 0);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = d.w;
    bi.bmiHeader.biHeight = -d.h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    int save = SaveDC(dc);
    IntersectClipRect(dc, area.left, area.top, area.right, area.bottom);
    SetStretchBltMode(dc, scale < 1.0 ? HALFTONE : COLORONCOLOR);
    SetBrushOrgEx(dc, 0, 0, nullptr);
    StretchDIBits(dc, x, y, ow, oh, 0, 0, d.w, d.h, d.bgra.data(), &bi, DIB_RGB_COLORS, SRCCOPY);
    RestoreDC(dc, save);
    if (drawn) *drawn = RECT{ x, y, x + ow, y + oh };
    return true;
}

// ---- 切り抜き範囲
bool cropActive() {
    if (!g.src) return false;
    ConvertOptions o = gatherOptions();
    return o.aspect == AspectMode::Crop && o.scale != ScaleMode::Original;
}

// 元画像座標の切り抜き範囲
bool cropSourceRect(double& x, double& y, double& w, double& h) {
    if (!g.src) return false;
    return cropRect(g.src->width, g.src->height, g.src->pixelAspect, gatherOptions(), x, y, w, h);
}

// 画面上の切り抜き範囲
bool cropScreenRect(RECT& r) {
    if (!g.srcDrawValid) return false;
    double x, y, w, h;
    if (!cropSourceRect(x, y, w, h)) return false;
    double kx = g.srcDrawW / g.src->width, ky = g.srcDrawH / g.src->height;
    r.left = (LONG)std::lround(g.srcDrawX + x * kx);
    r.top = (LONG)std::lround(g.srcDrawY + y * ky);
    r.right = (LONG)std::lround(g.srcDrawX + (x + w) * kx);
    r.bottom = (LONG)std::lround(g.srcDrawY + (y + h) * ky);
    return true;
}

void drawCropOverlay(HDC dc, RECT clip) {
    RECT cr;
    if (!cropScreenRect(cr)) return;
    RECT img{ (LONG)g.srcDrawX, (LONG)g.srcDrawY, (LONG)(g.srcDrawX + g.srcDrawW), (LONG)(g.srcDrawY + g.srcDrawH) };
    int save = SaveDC(dc);
    IntersectClipRect(dc, clip.left, clip.top, clip.right, clip.bottom);
    // 範囲外を暗くする
    HDC one = CreateCompatibleDC(dc);
    HBITMAP bm = CreateCompatibleBitmap(dc, 1, 1);
    HGDIOBJ ob = SelectObject(one, bm);
    SetPixel(one, 0, 0, RGB(0, 0, 0));
    BLENDFUNCTION bf{ AC_SRC_OVER, 0, 150, 0 };
    RECT parts[4] = {
        { img.left, img.top, img.right, cr.top },
        { img.left, cr.bottom, img.right, img.bottom },
        { img.left, cr.top, cr.left, cr.bottom },
        { cr.right, cr.top, img.right, cr.bottom },
    };
    for (auto& p : parts)
        if (p.right > p.left && p.bottom > p.top)
            AlphaBlend(dc, p.left, p.top, p.right - p.left, p.bottom - p.top, one, 0, 0, 1, 1, bf);
    SelectObject(one, ob);
    DeleteObject(bm);
    DeleteDC(one);
    // 枠
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    HPEN black = CreatePen(PS_SOLID, 1, RGB(0, 0, 0));
    HPEN yellow = CreatePen(PS_SOLID, std::max(1, g.S(2)), RGB(255, 220, 0));
    HGDIOBJ op = SelectObject(dc, black);
    Rectangle(dc, cr.left - 2, cr.top - 2, cr.right + 2, cr.bottom + 2);
    SelectObject(dc, yellow);
    Rectangle(dc, cr.left, cr.top, cr.right, cr.bottom);
    SelectObject(dc, op);
    SelectObject(dc, oldBrush);
    DeleteObject(black);
    DeleteObject(yellow);
    RestoreDC(dc, save);
}

void syncCropControls() {
    g.suppress = true;
    trackSet(IDC_CROPX, (int)std::lround(g.cropX * 1000));
    trackSet(IDC_CROPY, (int)std::lround(g.cropY * 1000));
    trackSet(IDC_CROPZ, (int)std::lround(g.cropZoom * 100));
    g.suppress = false;
    updateTrackLabels();
}

// 切り抜き左上 (元画像座標) から位置 0..1 を設定
void setCropTopLeft(double x, double y, double w, double h) {
    double slackX = g.src->width - w, slackY = g.src->height - h;
    g.cropX = slackX > 1e-6 ? (float)std::clamp(x / slackX, 0.0, 1.0) : 0.5f;
    g.cropY = slackY > 1e-6 ? (float)std::clamp(y / slackY, 0.0, 1.0) : 0.5f;
    syncCropControls();
    scheduleConvert();
    InvalidateRect(g.preview, nullptr, FALSE);
}

LRESULT CALLBACK PreviewProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
        HGDIOBJ oldBmp = SelectObject(mem, bmp);
        HBRUSH br = CreateSolidBrush(RGB(32, 32, 36));
        FillRect(mem, &rc, br);
        DeleteObject(br);
        HGDIOBJ oldFont = SelectObject(mem, g.font);
        g.srcDrawValid = false;
        auto drawSource = [&](RECT area, const wchar_t* cap) {
            RECT dr;
            if (drawImage(mem, g.dispSrc, area, cap, &dr)) {
                g.srcDrawValid = true;
                g.srcDrawX = dr.left; g.srcDrawY = dr.top;
                g.srcDrawW = dr.right - dr.left; g.srcDrawH = dr.bottom - dr.top;
                if (cropActive()) drawCropOverlay(mem, area);
            }
        };
        if (g.viewMode == 2) {
            RECT l = rc, r = rc;
            l.right = rc.right / 2 - 2; r.left = rc.right / 2 + 2;
            drawSource(l, L"元画像");
            drawImage(mem, g.dispDst, r, g.src ? L"変換中..." : L"");
        } else if (g.viewMode == 1) {
            drawSource(rc, L"画像をここにドロップ");
        } else {
            drawImage(mem, g.dispDst, rc, g.src ? L"変換中..." : L"画像をここにドロップ");
        }
        SelectObject(mem, oldFont);
        BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, oldBmp);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_LBUTTONDOWN: {
        POINT p{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        RECT cr;
        if (cropActive() && cropScreenRect(cr)) {
            RECT img{ (LONG)g.srcDrawX, (LONG)g.srcDrawY, (LONG)(g.srcDrawX + g.srcDrawW), (LONG)(g.srcDrawY + g.srcDrawH) };
            if (PtInRect(&img, p)) {
                double x, y, cw, ch;
                cropSourceRect(x, y, cw, ch);
                if (!PtInRect(&cr, p)) {
                    // クリック位置を中心に移動
                    double cxs = (p.x - g.srcDrawX) * g.src->width / g.srcDrawW;
                    double cys = (p.y - g.srcDrawY) * g.src->height / g.srcDrawH;
                    setCropTopLeft(cxs - cw / 2, cys - ch / 2, cw, ch);
                    cropSourceRect(x, y, cw, ch);
                }
                g.cropDragging = true;
                g.cropDragStart = p;
                g.cropStartX = x; g.cropStartY = y;
                SetCapture(h);
                return 0;
            }
        }
        if (g.zoom > 0) {
            g.dragging = true;
            g.dragStart = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            g.panStartX = g.panX; g.panStartY = g.panY;
            SetCapture(h);
        }
        return 0;
    }
    case WM_MOUSEMOVE:
        if (g.cropDragging) {
            double x, y, w, hh;
            if (cropSourceRect(x, y, w, hh) && g.srcDrawW > 0 && g.srcDrawH > 0) {
                double nx = g.cropStartX + (GET_X_LPARAM(lp) - g.cropDragStart.x) * g.src->width / g.srcDrawW;
                double ny = g.cropStartY + (GET_Y_LPARAM(lp) - g.cropDragStart.y) * g.src->height / g.srcDrawH;
                setCropTopLeft(nx, ny, w, hh);
            }
            return 0;
        }
        if (g.dragging) {
            g.panX = g.panStartX + GET_X_LPARAM(lp) - g.dragStart.x;
            g.panY = g.panStartY + GET_Y_LPARAM(lp) - g.dragStart.y;
            InvalidateRect(h, nullptr, FALSE);
        }
        return 0;
    case WM_LBUTTONUP:
        if (g.cropDragging) { g.cropDragging = false; ReleaseCapture(); startConvert(); }
        if (g.dragging) { g.dragging = false; ReleaseCapture(); }
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            POINT p; GetCursorPos(&p); ScreenToClient(h, &p);
            RECT cr;
            if (cropActive() && cropScreenRect(cr) && PtInRect(&cr, p)) { SetCursor(LoadCursor(nullptr, IDC_SIZEALL)); return TRUE; }
        }
        break;
    case WM_LBUTTONDBLCLK:
        g.zoom = 0; g.panX = g.panY = 0;
        comboSet(IDC_ZOOM, 0);
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    case WM_MOUSEWHEEL: {
        static const double steps[] = { 0.25, 0.5, 1, 2, 3, 4, 6, 8 };
        int delta = GET_WHEEL_DELTA_WPARAM(wp);
        if ((GET_KEYSTATE_WPARAM(wp) & MK_CONTROL) && cropActive()) {
            g.cropZoom = std::clamp(g.cropZoom * (delta > 0 ? 1.1f : 1.f / 1.1f), 1.f, 4.f);
            syncCropControls();
            scheduleConvert();
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        double cur = g.zoom;
        if (cur <= 0) cur = 1;
        int idx = 2;
        for (int i = 0; i < 8; ++i) if (std::fabs(steps[i] - cur) < 1e-6) idx = i;
        idx = std::clamp(idx + (delta > 0 ? 1 : -1), 0, 7);
        g.zoom = steps[idx];
        static const int zi[] = { 1, 2, 3, 4, 5, 6, 7, 8 };
        comboSet(IDC_ZOOM, zi[idx]);
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    }
    }
    return DefWindowProcW(h, msg, wp, lp);
}

// ---------------------------------------------------------------- パネル
void scrollPanelTo(int pos) {
    RECT rc; GetClientRect(g.panel, &rc);
    int maxPos = std::max(0, g.panelContentH - (int)rc.bottom);
    pos = std::clamp(pos, 0, maxPos);
    if (pos == g.panelScroll) return;
    int dy = g.panelScroll - pos;
    g.panelScroll = pos;
    SCROLLINFO si{ sizeof(si), SIF_POS };
    si.nPos = pos;
    SetScrollInfo(g.panel, SB_VERT, &si, TRUE);
    // 子ウィンドウごと画面をずらし、空いた部分だけ再描画する
    ScrollWindowEx(g.panel, 0, dy, nullptr, nullptr, nullptr, nullptr, SW_SCROLLCHILDREN | SW_INVALIDATE | SW_ERASE);
    UpdateWindow(g.panel);
}

LRESULT CALLBACK PanelProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_COMMAND:
    case WM_NOTIFY:
        return SendMessageW(g.hwnd, msg, wp, lp);
    case WM_HSCROLL:
        if (lp) return SendMessageW(g.hwnd, msg, wp, lp);
        break;
    case WM_VSCROLL: {
        RECT rc; GetClientRect(h, &rc);
        int pos = g.panelScroll;
        switch (LOWORD(wp)) {
        case SB_LINEUP: pos -= g.S(26); break;
        case SB_LINEDOWN: pos += g.S(26); break;
        case SB_PAGEUP: pos -= rc.bottom; break;
        case SB_PAGEDOWN: pos += rc.bottom; break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: {
            SCROLLINFO si{ sizeof(si), SIF_TRACKPOS };
            GetScrollInfo(h, SB_VERT, &si);
            pos = si.nTrackPos;
            break;
        }
        case SB_TOP: pos = 0; break;
        case SB_BOTTOM: pos = g.panelContentH; break;
        }
        scrollPanelTo(pos);
        return 0;
    }
    case WM_MOUSEWHEEL:
        scrollPanelTo(g.panelScroll - GET_WHEEL_DELTA_WPARAM(wp) * g.S(26) * 3 / WHEEL_DELTA);
        return 0;
    case WM_SIZE:
        layoutPanel();
        return 0;
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)wp;
        SetBkMode(dc, TRANSPARENT);
        return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
    }
    }
    return DefWindowProcW(h, msg, wp, lp);
}

void buildPanel() {
    addHeader(L"入力");
    addRow(nullptr, { { mkButton(IDC_OPEN, L"画像を開く... (Ctrl+O)"), 1, 0 } });
    addRow(nullptr, { { mkStatic(L"", IDC_SRCINFO), 1, 0 } }, 36);

    addHeader(L"機種プリセット");
    std::vector<std::wstring> pn;
    for (auto& p : presets()) pn.push_back(W(p.id + "  " + p.description));
    addRow(L"プリセット", { { mkCombo(IDC_PRESET, pn), 1, 0 } });
    addRow(L"モード", { { mkCombo(IDC_MODE, {}), 1, 0 } });
    addRow(nullptr, { { mkStatic(L"", IDC_MODEINFO), 1, 0 } }, 36);
    addRow(nullptr, { { mkButton(IDC_RECOMMEND, L"推奨ディザ設定に戻す"), 1, 0 } });

    addHeader(L"サイズ・縦横比");
    addRow(L"縮小", { { mkCombo(IDC_SCALE, names(scaleNames())), 1, 0 } });
    addRow(L"サイズ指定", { { mkEdit(IDC_W, L"640", true), 1, 0 }, { mkStatic(L"x"), 0, 12 }, { mkEdit(IDC_H, L"400", true), 1, 0 } });
    addRow(L"縦横比", { { mkCombo(IDC_ASPECT, names(aspectNames())), 1, 0 } });
    addRow(L"切り抜き 横位置", { { mkTrack(IDC_CROPX, 0, 1000, 500), 1, 0 }, { mkStatic(L"", IDC_CROPX_V), 0, 40 } });
    addRow(L"切り抜き 縦位置", { { mkTrack(IDC_CROPY, 0, 1000, 500), 1, 0 }, { mkStatic(L"", IDC_CROPY_V), 0, 40 } });
    addRow(L"切り抜き 拡大", { { mkTrack(IDC_CROPZ, 100, 400, 100), 1, 0 }, { mkStatic(L"", IDC_CROPZ_V), 0, 40 } });
    addRow(nullptr, { { mkButton(IDC_CROPRESET, L"切り抜きを中央に戻す"), 1, 0 } });
    addRow(nullptr, { { mkStatic(L"元画像の表示上で枠をドラッグして移動、Ctrl+ホイールで拡大できます", IDC_CROPHINT), 1, 0 } }, 36);
    addRow(L"補間", { { mkCombo(IDC_FILTER, names(filterNames())), 1, 0 } });

    addHeader(L"色");
    addRow(L"色数", { { mkCombo(IDC_COLORS, names(colorsNames())), 1, 0 } });
    addRow(L"色差", { { mkCombo(IDC_METRIC, names(metricNames())), 1, 0 } });
    addRow(nullptr, { { mkCheck(IDC_BLACK, L"黒を必ず含める", true), 1, 0 }, { mkCheck(IDC_TILECON, L"タイル/属性制約", true), 1, 0 } });
    addRow(nullptr, { { mkCheck(IDC_LINEAR, L"混色を線形光で計算 (実機CRTに近い)", true), 1, 0 } });

    addHeader(L"ディザリング・タイリング");
    addRow(L"ディザ", { { mkCombo(IDC_DITHER, names(ditherNames())), 1, 0 } });
    addRow(L"強度", { { mkTrack(IDC_STRENGTH, 0, 100, 100), 1, 0 }, { mkStatic(L"", IDC_STRENGTH_V), 0, 40 } });
    addRow(nullptr, { { mkCheck(IDC_SERP, L"蛇行走査 (誤差拡散)", true), 1, 0 } });
    addRow(L"タイリング", { { mkCombo(IDC_TILING, names(tilingNames())), 1, 0 } });
    addRow(L"配色の傾向", { { mkCombo(IDC_STYLE, names(tileStyleNames())), 1, 0 } });

    addHeader(L"画質調整");
    addRow(L"明るさ", { { mkTrack(IDC_BRI, -100, 100, 0), 1, 0 }, { mkStatic(L"", IDC_BRI_V), 0, 40 } });
    addRow(L"コントラスト", { { mkTrack(IDC_CON, -100, 100, 0), 1, 0 }, { mkStatic(L"", IDC_CON_V), 0, 40 } });
    addRow(L"彩度", { { mkTrack(IDC_SAT, -100, 100, 0), 1, 0 }, { mkStatic(L"", IDC_SAT_V), 0, 40 } });
    addRow(L"ガンマ", { { mkTrack(IDC_GAM, 20, 300, 100), 1, 0 }, { mkStatic(L"", IDC_GAM_V), 0, 40 } });
    addRow(nullptr, { { mkButton(IDC_ADJRESET, L"画質調整をリセット"), 1, 0 } });

    addHeader(L"アルファ (透明度)");
    addRow(L"処理", { { mkCombo(IDC_ALPHA, names(alphaNames())), 1, 0 } });
    addRow(L"背景色", { { mkButton(IDC_BGCOLOR, L"#000000"), 1, 0 } });
    addRow(L"しきい値", { { mkEdit(IDC_THRESH, L"128", true), 1, 0 } });

    addHeader(L"出力");
    std::vector<std::wstring> fn;
    for (auto f : outputFormats()) fn.push_back(W(formatName(f)));
    addRow(L"形式", { { mkCombo(IDC_FORMAT, fn), 1, 0 } });
    addRow(L"JPEG 品質", { { mkEdit(IDC_QUALITY, L"90", true), 1, 0 } });
    addRow(nullptr, { { mkCheck(IDC_OUTPAR, L"ドット比を補正して出力 (例 640x200→640x400)"), 1, 0 } });
    addRow(L"出力倍率", { { mkCombo(IDC_OUTZOOM, { L"1倍", L"2倍", L"3倍", L"4倍" }), 1, 0 } });
    addRow(L"MAG ユーザー", { { mkEdit(IDC_MAGUSER, L"", false), 1, 0 } });
    addRow(L"MAG メモ", { { mkEdit(IDC_MAGMEMO, L"", false), 1, 0 } });
    addRow(nullptr, { { mkButton(IDC_SAVE, L"名前を付けて保存... (Ctrl+S)"), 1, 0 } }, 30);

    SendMessageW(C(IDC_MAGUSER), EM_LIMITTEXT, 18, 0);
    comboSet(IDC_ASPECT, enumIndex(aspectNames(), (int)AspectMode::KeepPad));
    comboSet(IDC_PRESET, 1); // 98
    fillModes();
    applyRecommendedToControls();
    updateTrackLabels();
    updateBgButton();
    updateModeInfo();
}

HMENU buildMenu() {
    HMENU bar = CreateMenu();
    HMENU file = CreatePopupMenu();
    AppendMenuW(file, MF_STRING, IDM_OPEN, L"開く(&O)...\tCtrl+O");
    AppendMenuW(file, MF_STRING, IDM_SAVE, L"名前を付けて保存(&S)...\tCtrl+S");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, IDM_EXIT, L"終了(&X)");
    HMENU tool = CreatePopupMenu();
    AppendMenuW(tool, MF_STRING, IDM_RECONVERT, L"再変換(&R)\tF5");
    AppendMenuW(tool, MF_STRING, IDM_MAGINFO, L"MAG 情報(&M)...");
    HMENU help = CreatePopupMenu();
    AppendMenuW(help, MF_STRING, IDM_ABOUT, L"バージョン情報(&A)...");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)file, L"ファイル(&F)");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)tool, L"ツール(&T)");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)help, L"ヘルプ(&H)");
    return bar;
}

void showAbout() {
    std::wstring s = std::wstring(kAppName) + L" " + kAppVersion +
        L"\nレトロ機種向け 画像減色・縮小コンバーター\n\n"
        L"MIT License\n\n"
        L"MAG フォーマット: まきちゃんネット 策定の画像形式\n"
        L"機種名は略称で表記しています。固定パレットの色は独自の近似値です。";
    MessageBoxW(g.hwnd, s.c_str(), L"バージョン情報", MB_ICONINFORMATION);
}

// ---------------------------------------------------------------- メインウィンドウ
LRESULT CALLBACK MainProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        g.hwnd = h;
        g.dpi = GetDpiForWindow(h);
        g.panel = CreateWindowExW(WS_EX_CONTROLPARENT, L"RICPanel", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPCHILDREN, 0, 0, 10, 10, h, nullptr, g.inst, nullptr);
        g.preview = CreateWindowExW(WS_EX_CLIENTEDGE, L"RICPreview", L"", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, h, nullptr, g.inst, nullptr);
        g.status = CreateWindowExW(0, STATUSCLASSNAMEW, L"", WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 0, 0, h, (HMENU)(INT_PTR)IDC_STATUS, g.inst, nullptr);
        mkCombo(IDC_VIEW, { L"変換後を表示", L"元画像を表示", L"並べて表示" }, h);
        mkCombo(IDC_ZOOM, { L"フィット", L"25%", L"50%", L"100%", L"200%", L"300%", L"400%", L"600%", L"800%" }, h);
        HWND vp = mk(WC_BUTTONW, L"ドット比を補正して表示", BS_AUTOCHECKBOX, IDC_VIEWPAR, h);
        Button_SetCheck(vp, BST_CHECKED);
        buildPanel();
        applyFonts();
        layoutMain();
        DragAcceptFiles(h, TRUE);
        setStatus(L"画像を開いてください", L"");
        return 0;
    }
    case WM_SIZE:
        if (g.status) layoutMain();
        return 0;
    case WM_GETMINMAXINFO: {
        auto* mm = (MINMAXINFO*)lp;
        mm->ptMinTrackSize = { g.S(760), g.S(480) };
        return 0;
    }
    case WM_DPICHANGED: {
        g.dpi = HIWORD(wp);
        RECT* r = (RECT*)lp;
        applyFonts();
        SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        layoutMain();
        return 0;
    }
    case WM_DROPFILES: {
        HDROP drop = (HDROP)wp;
        wchar_t file[MAX_PATH * 4];
        if (DragQueryFileW(drop, 0, file, (UINT)std::size(file))) loadFile(file);
        DragFinish(drop);
        return 0;
    }
    case WM_TIMER:
        if (wp == TIMER_CONVERT) startConvert();
        return 0;
    case WM_APP_DONE:
        onConvertDone((uint64_t)wp, (ConvertResult*)lp);
        return 0;
    case WM_HSCROLL:
        if ((HWND)lp == C(IDC_CROPX)) g.cropX = trackPos(IDC_CROPX) / 1000.f;
        else if ((HWND)lp == C(IDC_CROPY)) g.cropY = trackPos(IDC_CROPY) / 1000.f;
        else if ((HWND)lp == C(IDC_CROPZ)) g.cropZoom = trackPos(IDC_CROPZ) / 100.f;
        updateTrackLabels();
        if (!g.suppress) scheduleConvert();
        InvalidateRect(g.preview, nullptr, FALSE);
        return 0;
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)wp;
        SetBkMode(dc, TRANSPARENT);
        return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
    }
    case WM_COMMAND: {
        int id = LOWORD(wp), code = HIWORD(wp);
        switch (id) {
        case IDC_OPEN: case IDM_OPEN: onOpen(); return 0;
        case IDC_SAVE: case IDM_SAVE: onSave(); return 0;
        case IDM_EXIT: DestroyWindow(h); return 0;
        case IDM_RECONVERT: startConvert(); return 0;
        case IDM_MAGINFO: showMagInfo(); return 0;
        case IDM_ABOUT: showAbout(); return 0;
        case IDC_PRESET:
            if (code == CBN_SELCHANGE) {
                fillModes(); applyRecommendedToControls(); updateModeInfo(); scheduleConvert();
                InvalidateRect(g.preview, nullptr, FALSE);
            }
            return 0;
        case IDC_MODE:
            if (code == CBN_SELCHANGE) { applyRecommendedToControls(); updateModeInfo(); scheduleConvert(); InvalidateRect(g.preview, nullptr, FALSE); }
            return 0;
        case IDC_RECOMMEND:
            applyRecommendedToControls(); scheduleConvert();
            return 0;
        case IDC_CROPRESET:
            g.cropX = g.cropY = 0.5f; g.cropZoom = 1.f;
            syncCropControls(); scheduleConvert();
            InvalidateRect(g.preview, nullptr, FALSE);
            return 0;
        case IDC_ASPECT:
            if (code == CBN_SELCHANGE) {
                updateModeInfo();
                if (cropActive() && g.viewMode == 0) { g.viewMode = 2; comboSet(IDC_VIEW, 2); }
                scheduleConvert();
                InvalidateRect(g.preview, nullptr, FALSE);
            }
            return 0;
        case IDC_ADJRESET:
            g.suppress = true;
            trackSet(IDC_BRI, 0); trackSet(IDC_CON, 0); trackSet(IDC_SAT, 0); trackSet(IDC_GAM, 100);
            g.suppress = false;
            updateTrackLabels(); scheduleConvert();
            return 0;
        case IDC_BGCOLOR: {
            static COLORREF custom[16] = {};
            CHOOSECOLORW cc{ sizeof(cc) };
            cc.hwndOwner = h;
            cc.rgbResult = RGB(g.bg.r, g.bg.g, g.bg.b);
            cc.lpCustColors = custom;
            cc.Flags = CC_FULLOPEN | CC_RGBINIT;
            if (ChooseColorW(&cc)) {
                g.bg = rgb(GetRValue(cc.rgbResult), GetGValue(cc.rgbResult), GetBValue(cc.rgbResult));
                updateBgButton(); scheduleConvert();
            }
            return 0;
        }
        case IDC_VIEW:
            if (code == CBN_SELCHANGE) { g.viewMode = comboSel(IDC_VIEW); InvalidateRect(g.preview, nullptr, FALSE); }
            return 0;
        case IDC_ZOOM:
            if (code == CBN_SELCHANGE) {
                static const double z[] = { 0, 0.25, 0.5, 1, 2, 3, 4, 6, 8 };
                g.zoom = z[std::clamp(comboSel(IDC_ZOOM), 0, 8)];
                g.panX = g.panY = 0;
                InvalidateRect(g.preview, nullptr, FALSE);
            }
            return 0;
        case IDC_VIEWPAR:
            g.viewPar = checked(IDC_VIEWPAR);
            InvalidateRect(g.preview, nullptr, FALSE);
            return 0;
        case IDC_FORMAT: case IDC_OUTPAR: case IDC_OUTZOOM: case IDC_QUALITY: case IDC_MAGUSER: case IDC_MAGMEMO:
            updateModeInfo();
            return 0;
        case IDC_SCALE:
            if (code == CBN_SELCHANGE) { updateModeInfo(); scheduleConvert(); InvalidateRect(g.preview, nullptr, FALSE); }
            return 0;
        case IDC_W: case IDC_H: case IDC_THRESH:
            if (code == EN_CHANGE) { scheduleConvert(); InvalidateRect(g.preview, nullptr, FALSE); }
            return 0;
        default:
            if (code == CBN_SELCHANGE || code == BN_CLICKED) scheduleConvert();
            return 0;
        }
    }
    case WM_DESTROY:
        if (g.cancel) g.cancel->store(true);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

} // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int show) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_WIN95_CLASSES | ICC_BAR_CLASSES | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);
    g.inst = inst;

    WNDCLASSEXW wc{ sizeof(wc) };
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(IDI_APPICON));
    wc.hIconSm = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON,
                                   GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    wc.lpfnWndProc = MainProc;
    wc.lpszClassName = L"RICMain";
    RegisterClassExW(&wc);
    wc.lpfnWndProc = PanelProc;
    wc.lpszClassName = L"RICPanel";
    wc.hIcon = nullptr;
    wc.hIconSm = nullptr;
    RegisterClassExW(&wc);
    wc.lpfnWndProc = PreviewProc;
    wc.lpszClassName = L"RICPreview";
    wc.style = CS_DBLCLKS;
    wc.hbrBackground = nullptr;
    RegisterClassExW(&wc);

    UINT sysDpi = GetDpiForSystem();
    HWND hwnd = CreateWindowExW(0, L"RICMain", kAppName, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
                                MulDiv(1280, (int)sysDpi, 96), MulDiv(820, (int)sysDpi, 96), nullptr, buildMenu(), inst, nullptr);
    if (!hwnd) return 1;
    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);

    // コマンドラインのファイルを開く
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv) {
        if (argc > 1) loadFile(argv[1]);
        LocalFree(argv);
    }

    ACCEL acc[] = {
        { FCONTROL | FVIRTKEY, 'O', IDM_OPEN },
        { FCONTROL | FVIRTKEY, 'S', IDM_SAVE },
        { FVIRTKEY, VK_F5, IDM_RECONVERT },
    };
    HACCEL hacc = CreateAcceleratorTableW(acc, (int)std::size(acc));
    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        if (TranslateAcceleratorW(hwnd, hacc, &m)) continue;
        if (IsDialogMessageW(hwnd, &m)) continue;
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    DestroyAcceleratorTable(hacc);
    CoUninitialize();
    return 0;
}
