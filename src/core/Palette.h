// RetroImageConv - palette generation
// SPDX-License-Identifier: MIT
#pragma once

#include "Color.h"
#include <vector>

namespace ric {

struct ColorSample {
    Vec3 c;
    float w = 1.f;
};

// 色のヒストグラムを作る (近い色はまとめる)。weights は nullptr 可
std::vector<ColorSample> buildHistogram(const std::vector<Vec3>& pixels, const std::vector<float>* weights, const ColorSpace& space);

struct PaletteGenOptions {
    const std::vector<RGBA8>* fixedSet = nullptr; // 指定時はこの中から選ぶ
    bool includeBlack = false;
    int kmeansIterations = 6;
    bool sortByLuma = true;
};

// メディアンカット + k-means で n 色のパレットを作り、色空間に丸める
std::vector<RGBA8> generatePalette(const std::vector<ColorSample>& hist, int n, const ColorSpace& space, const PaletteGenOptions& opt);

} // namespace ric
