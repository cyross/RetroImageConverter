// SPDX-License-Identifier: MIT
#include "Presets.h"
#include <cmath>
#include <cstdio>
#include <cctype>

namespace ric {

std::vector<RGBA8> paletteDigital8() {
    // 色コード: bit0 = B, bit1 = R, bit2 = G
    std::vector<RGBA8> p;
    for (int i = 0; i < 8; ++i) p.push_back(rgb((i & 2) ? 255 : 0, (i & 4) ? 255 : 0, (i & 1) ? 255 : 0));
    return p;
}

std::vector<RGBA8> paletteMX1() {
    // 1..15 番 (0 番は透明のため除外)。本ソフト独自の近似値
    return {
        rgb(0, 0, 0),       rgb(36, 194, 70),   rgb(100, 216, 120), rgb(80, 82, 230),
        rgb(124, 118, 250), rgb(206, 80, 72),   rgb(72, 228, 240),  rgb(246, 86, 84),
        rgb(252, 124, 120), rgb(206, 190, 84),  rgb(226, 204, 132), rgb(36, 168, 60),
        rgb(196, 90, 182),  rgb(200, 200, 200), rgb(255, 255, 255),
    };
}

std::vector<RGBA8> paletteFC() {
    // NTSC 風の信号レベルから YIQ で近似生成 (本ソフト独自の近似)
    const float lo[4] = { 0.350f, 0.518f, 0.962f, 1.550f };
    const float hi[4] = { 1.094f, 1.506f, 1.962f, 1.962f };
    const float black = 0.518f, white = 1.962f;
    const float pi = 3.14159265f;
    std::vector<RGBA8> out;
    auto add = [&](Vec3 c) {
        RGBA8 v = toRGBA8(c);
        for (auto& e : out) if (e == v) return;
        out.push_back(v);
    };
    add(Vec3(0, 0, 0));
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col <= 13; ++col) {
            float y, s = 0, th = 0;
            if (col == 0) { y = (hi[row] - black) / (white - black); }
            else if (col == 13) { y = (lo[row] - black) / (white - black); if (y < 0.02f) continue; }
            else {
                y = ((lo[row] + hi[row]) * 0.5f - black) / (white - black);
                s = (hi[row] - lo[row]) * 0.5f / (white - black);
                th = (20.f + (6 - col) * 30.f) * pi / 180.f;
            }
            float I = s * std::cos(th), Q = s * std::sin(th);
            float r = y + 0.956f * I + 0.621f * Q;
            float g = y - 0.272f * I - 0.647f * Q;
            float b = y - 1.106f * I + 1.703f * Q;
            add(Vec3(r * 255.f, g * 255.f, b * 255.f));
        }
    }
    return out;
}

std::vector<RGBA8> paletteGB(bool green) {
    if (green) return { rgb(28, 44, 24), rgb(76, 104, 52), rgb(140, 168, 80), rgb(196, 214, 140) };
    return { rgb(0, 0, 0), rgb(85, 85, 85), rgb(170, 170, 170), rgb(255, 255, 255) };
}

std::string ModeSpec::summary() const {
    char buf[256];
    std::string col;
    if (space.kind == SpaceKind::YJK) col = "YJK 19268";
    else if (!fixedPalette.empty()) col = std::to_string(fixedPalette.size()) + "(固定)";
    else if (paletteColors > 0) {
        char t[64];
        std::snprintf(t, sizeof(t), "%d/%.0f", paletteColors, space.totalColors());
        col = t;
    } else {
        char t[64];
        std::snprintf(t, sizeof(t), "%.0f", space.totalColors());
        col = t;
    }
    std::string sp = fixedPalette.empty() ? space.describe() : (digital ? "デジタル" : "固定パレット");
    std::snprintf(buf, sizeof(buf), "%dx%d  %s色  %s  ドット比 %.3f", width, height, col.c_str(), sp.c_str(), pixelAspect);
    std::string s = buf;
    if (tile.enabled()) {
        char t[128];
        if (tile.subPalettes > 0)
            std::snprintf(t, sizeof(t), "  [%dx%d毎 %d色, パレット%d本]", tile.tileW, tile.tileH, tile.colorsPerTile, tile.subPalettes);
        else
            std::snprintf(t, sizeof(t), "  [%dx%d毎 %d色]", tile.tileW, tile.tileH, tile.colorsPerTile);
        s += t;
    }
    return s;
}

namespace {

// ビルダー
struct MB {
    ModeSpec m;
    MB(const char* id, const char* label, int w, int h, double par) {
        m.id = id; m.label = label; m.width = w; m.height = h; m.pixelAspect = par;
    }
    MB& space(ColorSpace s) { m.space = s; return *this; }
    MB& pal(int n) { m.paletteColors = n; return *this; }
    MB& fixed(std::vector<RGBA8> p, bool dig = false) { m.fixedPalette = std::move(p); m.digital = dig; m.space = ColorSpace::full(); return *this; }
    MB& tile(int tw, int th, int cpt, int sub, bool shared0) { m.tile = { tw, th, cpt, sub, shared0 }; return *this; }
    MB& mag(int sm) { m.magScreenMode = sm; return *this; }
    MB& rec(DitherMethod d, float s = 1.f, TilingMode t = TilingMode::Off, TileStyle st = TileStyle::Free) {
        m.recDither = d; m.recStrength = s; m.recTiling = t; m.recStyle = st; return *this;
    }
    operator ModeSpec() const { return m; }
};

constexpr double PAR_43 = 4.0 / 3.0;
inline double par43(int w, int h) { return PAR_43 * h / w; } // 4:3 画面いっぱいに表示する場合

std::vector<ModeSpec> digital8Modes(bool has400, bool has320, bool style88) {
    std::vector<ModeSpec> v;
    TileStyle st = style88 ? TileStyle::Style88 : TileStyle::Style98;
    if (has400) v.push_back(MB("d8-400", "640x400 デジタル8色", 640, 400, 1.0).fixed(paletteDigital8(), true).mag(0x06).rec(DitherMethod::None, 1, TilingMode::Quad, st));
    v.push_back(MB("d8-200", "640x200 デジタル8色", 640, 200, 0.5).fixed(paletteDigital8(), true).mag(0x07).rec(DitherMethod::None, 1, TilingMode::Quad, st));
    if (has320) v.push_back(MB("d8-320", "320x200 デジタル8色", 320, 200, 1.0).fixed(paletteDigital8(), true).mag(0x06).rec(DitherMethod::None, 1, TilingMode::Quad, st));
    return v;
}

std::vector<Preset> buildPresets() {
    using D = DitherMethod;
    using T = TilingMode;
    using S = TileStyle;
    std::vector<Preset> P;

    // ---- 88 ----
    {
        Preset p{ "88", "8bit 国産PC 88系 (640x200)", {} };
        p.modes.push_back(MB("d8", "640x200 デジタル8色", 640, 200, 0.5).fixed(paletteDigital8(), true).mag(0x07).rec(D::None, 1, T::Quad, S::Style88));
        p.modes.push_back(MB("a8", "640x200 アナログ8色 (512色中)", 640, 200, 0.5).space(ColorSpace::bits(3)).pal(8).mag(0x03).rec(D::None, 1, T::Quad, S::Style88));
        P.push_back(p);
    }
    // ---- 98 ----
    {
        Preset p{ "98", "16bit 国産PC 98系 (640x400)", {} };
        p.modes.push_back(MB("a16", "640x400 アナログ16色 (4096色中)", 640, 400, 1.0).space(ColorSpace::bits(4)).pal(16).mag(0x00).rec(D::None, 1, T::Quad, S::Style98));
        p.modes.push_back(MB("a8", "640x400 アナログ8色 (4096色中)", 640, 400, 1.0).space(ColorSpace::bits(4)).pal(8).mag(0x02).rec(D::None, 1, T::Quad, S::Style98));
        p.modes.push_back(MB("d8", "640x400 デジタル8色", 640, 400, 1.0).fixed(paletteDigital8(), true).mag(0x06).rec(D::None, 1, T::Quad, S::Style88));
        p.modes.push_back(MB("a16-200", "640x200 アナログ16色 (4096色中)", 640, 200, 0.5).space(ColorSpace::bits(4)).pal(16).mag(0x01).rec(D::None, 1, T::Quad, S::Style98));
        p.modes.push_back(MB("d8-200", "640x200 デジタル8色", 640, 200, 0.5).fixed(paletteDigital8(), true).mag(0x07).rec(D::None, 1, T::Quad, S::Style88));
        P.push_back(p);
    }
    // ---- 21 ----
    {
        Preset p{ "21", "32bit 国産PC 98系後期 (256色)", {} };
        p.modes.push_back(MB("256-400", "640x400 256色 (1677万色中)", 640, 400, 1.0).pal(256).mag(0x80).rec(D::FloydSteinberg, 0.8f));
        p.modes.push_back(MB("256-480", "640x480 256色 (1677万色中)", 640, 480, 1.0).pal(256).mag(0x80).rec(D::FloydSteinberg, 0.8f));
        p.modes.push_back(MB("a16", "640x400 アナログ16色 (4096色中)", 640, 400, 1.0).space(ColorSpace::bits(4)).pal(16).mag(0x00).rec(D::None, 1, T::Quad, S::Style98));
        P.push_back(p);
    }
    // ---- X68K ----
    {
        Preset p{ "X68K", "68000系 国産PC (65536色)", {} };
        p.modes.push_back(MB("512-65536", "512x512 65536色", 512, 512, par43(512, 512)).space(ColorSpace::x68k()).rec(D::Bayer4, 0.5f));
        p.modes.push_back(MB("768-16", "768x512 16色 (65536色中)", 768, 512, par43(768, 512)).space(ColorSpace::x68k()).pal(16).mag(0x00).rec(D::FloydSteinberg, 0.85f));
        p.modes.push_back(MB("512-256", "512x512 256色 (65536色中)", 512, 512, par43(512, 512)).space(ColorSpace::x68k()).pal(256).mag(0x80).rec(D::FloydSteinberg, 0.8f));
        p.modes.push_back(MB("256-65536", "256x256 65536色", 256, 256, par43(256, 256)).space(ColorSpace::x68k()).rec(D::Bayer4, 0.5f));
        P.push_back(p);
    }
    // ---- SX1 ----
    {
        Preset p{ "SX1", "8bit 国産PC X系 初代", {} };
        auto m = digital8Modes(false, true, true);
        p.modes = m;
        P.push_back(p);
    }
    // ---- SX1T ----
    {
        Preset p{ "SX1T", "8bit 国産PC X系 turbo", {} };
        p.modes = digital8Modes(true, true, true);
        P.push_back(p);
    }
    // ---- SX1TZ ----
    {
        Preset p{ "SX1TZ", "8bit 国産PC X系 turbo Z (4096色)", {} };
        p.modes.push_back(MB("4096", "320x200 4096色", 320, 200, 1.0).space(ColorSpace::bits(4)).rec(D::Bayer4, 0.6f));
        p.modes.push_back(MB("64", "320x200 64色 (4096色中)", 320, 200, 1.0).space(ColorSpace::bits(4)).pal(64).mag(0x80).rec(D::FloydSteinberg, 0.85f));
        p.modes.push_back(MB("a8-640", "640x200 8色 (4096色中)", 640, 200, 0.5).space(ColorSpace::bits(4)).pal(8).mag(0x03).rec(D::None, 1, T::Quad, S::Style88));
        for (auto& m : digital8Modes(true, false, true)) p.modes.push_back(m);
        P.push_back(p);
    }
    // ---- F7 ----
    {
        Preset p{ "F7", "8bit 国産PC F系 (7/77)", {} };
        p.modes.push_back(MB("d8", "640x200 デジタル8色", 640, 200, 0.5).fixed(paletteDigital8(), true).mag(0x07).rec(D::None, 1, T::Quad, S::Style88));
        P.push_back(p);
    }
    // ---- F77A ----
    {
        Preset p{ "F77A", "8bit 国産PC F系 AV/40", {} };
        p.modes.push_back(MB("4096", "320x200 4096色", 320, 200, 1.0).space(ColorSpace::bits(4)).rec(D::Bayer4, 0.6f));
        p.modes.push_back(MB("a8", "640x200 8色 (4096色中)", 640, 200, 0.5).space(ColorSpace::bits(4)).pal(8).mag(0x03).rec(D::None, 1, T::Quad, S::Style88));
        p.modes.push_back(MB("262144", "320x200 262144色 (40)", 320, 200, 1.0).space(ColorSpace::bits(6)).rec(D::Bayer4, 0.4f));
        p.modes.push_back(MB("a8-400", "640x400 8色 (4096色中, 40)", 640, 400, 1.0).space(ColorSpace::bits(4)).pal(8).mag(0x02).rec(D::None, 1, T::Quad, S::Style88));
        p.modes.push_back(MB("d8", "640x200 デジタル8色", 640, 200, 0.5).fixed(paletteDigital8(), true).mag(0x07).rec(D::None, 1, T::Quad, S::Style88));
        P.push_back(p);
    }
    // ---- FTNS ----
    {
        Preset p{ "FTNS", "32bit 国産PC F系 (CD-ROM機)", {} };
        p.modes.push_back(MB("256", "640x480 256色 (1677万色中)", 640, 480, 1.0).pal(256).mag(0x80).rec(D::FloydSteinberg, 0.8f));
        p.modes.push_back(MB("16", "640x480 16色 (4096色中)", 640, 480, 1.0).space(ColorSpace::bits(4)).pal(16).mag(0x00).rec(D::FloydSteinberg, 0.85f));
        p.modes.push_back(MB("32768", "320x240 32768色", 320, 240, 1.0).space(ColorSpace::bits(5)).rec(D::Bayer4, 0.5f));
        P.push_back(p);
    }
    // ---- MX ----
    {
        Preset p{ "MX", "8bit 規格PC 初代", {} };
        p.modes.push_back(MB("sc2", "256x192 15色 (8x1ドット毎2色)", 256, 192, 8.0 / 7.0).fixed(paletteMX1()).tile(8, 1, 2, 0, false).rec(D::Bayer4, 0.6f));
        p.modes.push_back(MB("free", "256x192 15色 (制約なし)", 256, 192, 8.0 / 7.0).fixed(paletteMX1()).rec(D::Bayer4, 0.6f));
        P.push_back(p);
    }
    // ---- MX2 ----
    auto mx2Modes = [&]() {
        std::vector<ModeSpec> v;
        v.push_back(MB("sc5", "256x212 16色 (512色中) SC5", 256, 212, 8.0 / 7.0).space(ColorSpace::bits(3)).pal(16).mag(0x00).rec(D::FloydSteinberg, 0.85f));
        v.push_back(MB("sc7", "512x212 16色 (512色中) SC7", 512, 212, 4.0 / 7.0).space(ColorSpace::bits(3)).pal(16).mag(0x01).rec(D::FloydSteinberg, 0.85f));
        v.push_back(MB("sc8", "256x212 256色 (固定 G3R3B2) SC8", 256, 212, 8.0 / 7.0).space(ColorSpace::bits(3, 3, 2)).rec(D::Bayer4, 0.7f));
        return v;
    };
    {
        Preset p{ "MX2", "8bit 規格PC 2", mx2Modes() };
        P.push_back(p);
    }
    // ---- M2P / MTR ----
    for (const char* id : { "M2P", "MTR" }) {
        Preset p{ id, std::string(id) == "M2P" ? "8bit 規格PC 2+ (YJK)" : "16bit 規格PC 最終世代 (YJK)", {} };
        p.modes.push_back(MB("sc12", "256x212 19268色 (YJK) SC12", 256, 212, 8.0 / 7.0).space(ColorSpace::yjk()).rec(D::FloydSteinberg, 0.7f));
        for (auto& m : mx2Modes()) p.modes.push_back(m);
        P.push_back(p);
    }
    // ---- FC ----
    {
        Preset p{ "FC", "8bit 家庭用ゲーム機", {} };
        p.modes.push_back(MB("bg", "256x240 BG (16x16毎 4色パレット×4)", 256, 240, 8.0 / 7.0).fixed(paletteFC()).tile(16, 16, 4, 4, true).rec(D::Bayer4, 0.5f));
        p.modes.push_back(MB("free", "256x240 13色 (制約なし)", 256, 240, 8.0 / 7.0).fixed(paletteFC()).pal(13).rec(D::Bayer4, 0.5f));
        P.push_back(p);
    }
    // ---- PCE ----
    {
        Preset p{ "PCE", "8/16bit 家庭用ゲーム機 (512色)", {} };
        p.modes.push_back(MB("bg256", "256x224 BG (8x8毎 16色パレット×16)", 256, 224, 8.0 / 7.0).space(ColorSpace::bits(3)).tile(8, 8, 16, 16, true).rec(D::Bayer4, 0.6f));
        p.modes.push_back(MB("bg320", "320x224 BG (8x8毎 16色パレット×16)", 320, 224, 6.0 / 7.0).space(ColorSpace::bits(3)).tile(8, 8, 16, 16, true).rec(D::Bayer4, 0.6f));
        p.modes.push_back(MB("free", "256x224 256色 (512色中, 制約なし)", 256, 224, 8.0 / 7.0).space(ColorSpace::bits(3)).pal(256).rec(D::Bayer4, 0.6f));
        P.push_back(p);
    }
    // ---- SFC ----
    {
        Preset p{ "SFC", "16bit 家庭用ゲーム機 (32768色)", {} };
        p.modes.push_back(MB("m1", "256x224 BG (8x8毎 16色パレット×8)", 256, 224, 8.0 / 7.0).space(ColorSpace::bits(5)).tile(8, 8, 16, 8, true).rec(D::Bayer4, 0.5f));
        p.modes.push_back(MB("256", "256x224 256色 (32768色中)", 256, 224, 8.0 / 7.0).space(ColorSpace::bits(5)).pal(256).rec(D::FloydSteinberg, 0.7f));
        p.modes.push_back(MB("hi", "512x448 256色 (32768色中)", 512, 448, 8.0 / 7.0).space(ColorSpace::bits(5)).pal(256).rec(D::FloydSteinberg, 0.7f));
        P.push_back(p);
    }
    // ---- PS1 ----
    {
        Preset p{ "PS1", "32bit 家庭用ゲーム機 初代", {} };
        p.modes.push_back(MB("320-15", "320x240 32768色", 320, 240, 1.0).space(ColorSpace::bits(5)).rec(D::Bayer4, 0.5f));
        p.modes.push_back(MB("640-15", "640x480 32768色", 640, 480, 1.0).space(ColorSpace::bits(5)).rec(D::Bayer4, 0.5f));
        p.modes.push_back(MB("640-24", "640x480 1677万色", 640, 480, 1.0));
        p.modes.push_back(MB("tex8", "256x256 テクスチャ 256色 CLUT", 256, 256, 1.0).space(ColorSpace::bits(5)).pal(256).rec(D::FloydSteinberg, 0.7f));
        p.modes.push_back(MB("tex4", "256x256 テクスチャ 16色 CLUT", 256, 256, 1.0).space(ColorSpace::bits(5)).pal(16).rec(D::FloydSteinberg, 0.8f));
        P.push_back(p);
    }
    // ---- PS2 ----
    {
        Preset p{ "PS2", "128bit 家庭用ゲーム機", {} };
        p.modes.push_back(MB("640x448", "640x448 1677万色", 640, 448, par43(640, 448)));
        p.modes.push_back(MB("640x480", "640x480 1677万色", 640, 480, 1.0));
        p.modes.push_back(MB("720x480", "720x480 1677万色", 720, 480, par43(720, 480)));
        p.modes.push_back(MB("tex8", "256x256 テクスチャ 256色 CLUT", 256, 256, 1.0).pal(256).rec(D::FloydSteinberg, 0.7f));
        p.modes.push_back(MB("tex4", "256x256 テクスチャ 16色 CLUT", 256, 256, 1.0).pal(16).rec(D::FloydSteinberg, 0.8f));
        P.push_back(p);
    }
    // ---- GB ----
    {
        Preset p{ "GB", "携帯ゲーム機 モノクロ", {} };
        p.modes.push_back(MB("gray", "160x144 4階調 (グレー)", 160, 144, 1.0).fixed(paletteGB(false)).rec(D::Bayer4, 0.6f));
        p.modes.push_back(MB("green", "160x144 4階調 (緑液晶風)", 160, 144, 1.0).fixed(paletteGB(true)).rec(D::Bayer4, 0.6f));
        P.push_back(p);
    }
    // ---- GBC ----
    {
        Preset p{ "GBC", "携帯ゲーム機 カラー", {} };
        p.modes.push_back(MB("bg", "160x144 BG (8x8毎 4色パレット×8)", 160, 144, 1.0).space(ColorSpace::bits(5)).tile(8, 8, 4, 8, false).rec(D::Bayer4, 0.5f));
        p.modes.push_back(MB("56", "160x144 56色 (32768色中, 制約なし)", 160, 144, 1.0).space(ColorSpace::bits(5)).pal(56).rec(D::Bayer4, 0.5f));
        P.push_back(p);
    }
    // ---- GBA ----
    {
        Preset p{ "GBA", "携帯ゲーム機 32bit", {} };
        p.modes.push_back(MB("m3", "240x160 32768色", 240, 160, 1.0).space(ColorSpace::bits(5)).rec(D::Bayer4, 0.5f));
        p.modes.push_back(MB("m4", "240x160 256色 (32768色中)", 240, 160, 1.0).space(ColorSpace::bits(5)).pal(256).rec(D::FloydSteinberg, 0.7f));
        p.modes.push_back(MB("bg16", "240x160 BG (8x8毎 16色パレット×16)", 240, 160, 1.0).space(ColorSpace::bits(5)).tile(8, 8, 16, 16, true).rec(D::Bayer4, 0.5f));
        P.push_back(p);
    }
    // ---- VGA ----
    {
        Preset p{ "VGA", "VGA 互換グラフィックス", {} };
        p.modes.push_back(MB("320x200", "320x200 256色 (262144色中)", 320, 200, par43(320, 200)).space(ColorSpace::bits(6)).pal(256).mag(0x80).rec(D::FloydSteinberg, 0.8f));
        p.modes.push_back(MB("320x240", "320x240 256色 (262144色中)", 320, 240, 1.0).space(ColorSpace::bits(6)).pal(256).mag(0x80).rec(D::FloydSteinberg, 0.8f));
        p.modes.push_back(MB("640x480", "640x480 16色 (262144色中)", 640, 480, 1.0).space(ColorSpace::bits(6)).pal(16).mag(0x00).rec(D::FloydSteinberg, 0.85f));
        P.push_back(p);
    }
    // ---- WIN ----
    {
        Preset p{ "WIN", "WIN 系 デスクトップ標準解像度", {} };
        const int res[4][2] = { { 640, 480 }, { 800, 600 }, { 1024, 768 }, { 1280, 1024 } };
        for (auto& r : res) {
            char id[32], lb[64];
            std::snprintf(id, sizeof(id), "%dx%d-256", r[0], r[1]);
            std::snprintf(lb, sizeof(lb), "%dx%d 256色", r[0], r[1]);
            p.modes.push_back(MB(id, lb, r[0], r[1], 1.0).pal(256).mag(0x80).rec(D::FloydSteinberg, 0.8f));
        }
        for (auto& r : res) {
            char id[32], lb[64];
            std::snprintf(id, sizeof(id), "%dx%d-65536", r[0], r[1]);
            std::snprintf(lb, sizeof(lb), "%dx%d 65536色", r[0], r[1]);
            p.modes.push_back(MB(id, lb, r[0], r[1], 1.0).space(ColorSpace::bits(5, 6, 5)).rec(D::Bayer4, 0.4f));
        }
        P.push_back(p);
    }
    // ---- HD / FHD / 4K ----
    P.push_back(Preset{ "HD", "1280x720 フルカラー", { MB("full", "1280x720 フルカラー", 1280, 720, 1.0) } });
    P.push_back(Preset{ "FHD", "1920x1080 フルカラー", { MB("full", "1920x1080 フルカラー", 1920, 1080, 1.0) } });
    P.push_back(Preset{ "4K", "3840x2160 フルカラー", { MB("full", "3840x2160 フルカラー", 3840, 2160, 1.0) } });
    return P;
}

} // namespace

const std::vector<Preset>& presets() {
    static const std::vector<Preset> p = buildPresets();
    return p;
}

const Preset* findPreset(const std::string& id) {
    for (auto& p : presets()) {
        if (p.id.size() != id.size()) continue;
        bool eq = true;
        for (size_t i = 0; i < id.size(); ++i)
            if (std::toupper((unsigned char)p.id[i]) != std::toupper((unsigned char)id[i])) { eq = false; break; }
        if (eq) return &p;
    }
    return nullptr;
}

} // namespace ric
