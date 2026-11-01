// RetroImageConv - color mapping, dithering, tiling and machine constraints
// SPDX-License-Identifier: MIT
#pragma once

#include "Image.h"
#include "Options.h"
#include "Presets.h"
#include <vector>
#include <atomic>

namespace ric {

// タイル (属性) ごとの使用可能色
struct TileAssign {
    int tileW = 0, tileH = 0, tilesX = 0, tilesY = 0;
    std::vector<int> subsetOfTile;                 // タイル -> subsets の番号
    std::vector<std::vector<uint8_t>> subsets;     // マスターパレットの番号の集合
    bool enabled() const { return tileW > 0 && tileH > 0 && !subsets.empty(); }
    int subsetAt(int x, int y) const { return subsetOfTile[(size_t)(y / tileH) * tilesX + (x / tileW)]; }
};

struct QuantizeParams {
    DitherMethod dither = DitherMethod::None;
    float strength = 1.f;
    bool serpentine = true;
    TilingMode tiling = TilingMode::Off;
    TileStyle style = TileStyle::Free;
    DistanceMetric metric = DistanceMetric::WeightedRGB;
    bool digitalPalette = false;
    bool linearLight = false;   // 混色・誤差を線形光で計算
    const std::atomic<bool>* cancel = nullptr;
};

// パレット方式: 各画素の番号を求める。opaque[i]==0 の画素は処理しない
void quantizePalette(const std::vector<Vec3>& rgb, const std::vector<uint8_t>& opaque, int w, int h,
                     const std::vector<RGBA8>& palette, const TileAssign* tiles, const QuantizeParams& p,
                     std::vector<uint8_t>& outIdx);

// ダイレクトカラー方式
void quantizeDirect(const std::vector<Vec3>& rgb, const std::vector<uint8_t>& opaque, int w, int h,
                    const ColorSpace& space, const QuantizeParams& p, std::vector<RGBA8>& out);

// YJK 方式 (横 4 ドットで J/K を共有)
void quantizeYJK(const std::vector<Vec3>& rgb, const std::vector<uint8_t>& opaque, int w, int h,
                 const QuantizeParams& p, std::vector<RGBA8>& out);

// タイル制約付きのパレットとタイル割り当てを求める
void solveTileConstraint(const std::vector<Vec3>& rgb, const std::vector<uint8_t>& opaque, int w, int h,
                         const ModeSpec& mode, bool includeBlack, bool mixAware, bool linearMix,
                         std::vector<RGBA8>& outPalette, TileAssign& outTiles);

} // namespace ric
