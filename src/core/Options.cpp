// SPDX-License-Identifier: MIT
#include "Options.h"
#include <cctype>

namespace ric {

#define E(v, k, l) EnumName{ (int)(v), k, l }

const std::vector<EnumName>& ditherNames() {
    static const std::vector<EnumName> t = {
        E(DitherMethod::None, "none", "なし"),
        E(DitherMethod::Bayer2, "bayer2", "組織的 2x2"),
        E(DitherMethod::Bayer4, "bayer4", "組織的 4x4"),
        E(DitherMethod::Bayer8, "bayer8", "組織的 8x8"),
        E(DitherMethod::FloydSteinberg, "fs", "誤差拡散 Floyd-Steinberg"),
        E(DitherMethod::JarvisJudiceNinke, "jjn", "誤差拡散 Jarvis-Judice-Ninke"),
        E(DitherMethod::Stucki, "stucki", "誤差拡散 Stucki"),
        E(DitherMethod::Atkinson, "atkinson", "誤差拡散 Atkinson"),
        E(DitherMethod::Sierra3, "sierra", "誤差拡散 Sierra"),
        E(DitherMethod::SierraLite, "sierralite", "誤差拡散 Sierra Lite"),
    };
    return t;
}

const std::vector<EnumName>& tilingNames() {
    static const std::vector<EnumName> t = {
        E(TilingMode::Off, "off", "なし"),
        E(TilingMode::Checker, "checker", "市松 (50%)"),
        E(TilingMode::Quad, "quad", "2x2 タイル (25/50/75%)"),
        E(TilingMode::Fine4x4, "fine", "4x4 タイル (1/16刻み)"),
        E(TilingMode::VStripe, "vstripe", "縦縞 (50%)"),
        E(TilingMode::HStripe, "hstripe", "横縞 (50%)"),
    };
    return t;
}

const std::vector<EnumName>& tileStyleNames() {
    static const std::vector<EnumName> t = {
        E(TileStyle::Free, "free", "制限なし"),
        E(TileStyle::Style88, "88", "88風 (原色1成分違い+黒白混色)"),
        E(TileStyle::Style98, "98", "98風 (近似色同士で滑らかに)"),
    };
    return t;
}

const std::vector<EnumName>& filterNames() {
    static const std::vector<EnumName> t = {
        E(ResizeFilter::Lanczos3, "lanczos", "Lanczos3 (高品質)"),
        E(ResizeFilter::Bilinear, "bilinear", "バイリニア"),
        E(ResizeFilter::Area, "area", "面積平均"),
        E(ResizeFilter::Nearest, "nearest", "ニアレストネイバー"),
    };
    return t;
}

const std::vector<EnumName>& scaleNames() {
    static const std::vector<EnumName> t = {
        E(ScaleMode::Machine, "machine", "機種の解像度に合わせる"),
        E(ScaleMode::Original, "original", "元画像のサイズのまま"),
        E(ScaleMode::Custom, "custom", "サイズ指定"),
    };
    return t;
}

const std::vector<EnumName>& aspectNames() {
    static const std::vector<EnumName> t = {
        E(AspectMode::Stretch, "stretch", "機種の画面比に揃える (引き伸ばし)"),
        E(AspectMode::KeepPad, "keep", "元の比率を保つ (余白あり)"),
        E(AspectMode::KeepNoPad, "keep-nopad", "元の比率を保つ (余白なし)"),
        E(AspectMode::Crop, "crop", "元の比率を保つ (切り抜き)"),
    };
    return t;
}

const std::vector<EnumName>& alphaNames() {
    static const std::vector<EnumName> t = {
        E(AlphaMode::Flatten, "flatten", "背景色と合成 (不透明)"),
        E(AlphaMode::Keep, "keep", "アルファを保持"),
        E(AlphaMode::Binary, "binary", "2値化 (透明/不透明)"),
    };
    return t;
}

const std::vector<EnumName>& metricNames() {
    static const std::vector<EnumName> t = {
        E(DistanceMetric::WeightedRGB, "rgb", "加重RGB (高速)"),
        E(DistanceMetric::Lab, "lab", "L*a*b* (知覚的)"),
    };
    return t;
}

const std::vector<EnumName>& colorsNames() {
    static const std::vector<EnumName> t = {
        E(0, "preset", "プリセット通り"),
        E(2, "2", "2色"), E(4, "4", "4色"), E(8, "8", "8色"), E(16, "16", "16色"),
        E(32, "32", "32色"), E(64, "64", "64色"), E(128, "128", "128色"), E(256, "256", "256色"),
        E(65536, "65536", "65536色 (RGB565)"),
        E(16777216, "full", "フルカラー"),
    };
    return t;
}

#undef E

bool parseEnum(const std::vector<EnumName>& table, const std::string& key, int& out) {
    std::string k;
    for (char c : key) k += (char)std::tolower((unsigned char)c);
    for (auto& e : table) {
        if (k == e.key) { out = e.value; return true; }
    }
    return false;
}

int enumIndex(const std::vector<EnumName>& table, int value) {
    for (size_t i = 0; i < table.size(); ++i) if (table[i].value == value) return (int)i;
    return 0;
}

} // namespace ric
