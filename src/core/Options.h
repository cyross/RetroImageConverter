// RetroImageConv - conversion options
// SPDX-License-Identifier: MIT
#pragma once

#include "Color.h"
#include <string>
#include <vector>

namespace ric {

enum class DitherMethod {
    None,
    Bayer2, Bayer4, Bayer8,          // 組織的ディザ
    FloydSteinberg, JarvisJudiceNinke, Stucki, Atkinson, Sierra3, SierraLite // 誤差拡散
};

// タイリング (2 色のタイルパターンで中間色を作る、当時の CG で多用された手法)
enum class TilingMode {
    Off,
    Checker,   // 市松 50%
    Quad,      // 2x2 : 25/50/75%
    Fine4x4,   // 4x4 : 1/16 刻み
    VStripe,   // 縦縞 50%
    HStripe    // 横縞 50%
};

// タイリングの配色の癖
enum class TileStyle {
    Free,      // 制限なし
    Style88,   // 88風: 原色同士は 1 成分違い + 黒/白との混色を中心に
    Style98    // 98風: 近い色同士のみ混ぜて滑らかに
};

enum class ResizeFilter { Lanczos3, Bilinear, Area, Nearest };
enum class ScaleMode { Machine, Original, Custom };
enum class AspectMode {
    Stretch,    // 機種の画面比率に合わせて引き伸ばす
    KeepPad,    // 元画像の比率を保ち、余白で画面サイズに揃える
    KeepNoPad,  // 元画像の比率を保ち、余白なし
    Crop        // 元画像の比率を保ち、はみ出しを切り抜く
};
enum class AlphaMode { Flatten, Keep, Binary };

struct ConvertOptions {
    std::string presetId = "98";
    int modeIndex = 0;

    ScaleMode scale = ScaleMode::Machine;
    int customWidth = 640, customHeight = 400;
    AspectMode aspect = AspectMode::KeepPad;
    // 切り抜き (AspectMode::Crop) の範囲: 位置 0..1 (0.5 = 中央)、拡大率 1.0 以上
    float cropX = 0.5f, cropY = 0.5f, cropZoom = 1.0f;
    ResizeFilter filter = ResizeFilter::Lanczos3;

    DitherMethod dither = DitherMethod::None;
    float ditherStrength = 1.0f;     // 0..1
    bool serpentine = true;
    bool linearLight = true;         // 混色を線形光で計算 (CRT 実機の見え方に近い。false = 一般的なツールと同じ sRGB 計算)
    TilingMode tiling = TilingMode::Off;
    TileStyle tileStyle = TileStyle::Free;

    // 色数の上書き: 0 = プリセット通り / 2..256 = パレット色数 / 65536 = RGB565 / 16777216 = フルカラー
    int colorsOverride = 0;
    DistanceMetric metric = DistanceMetric::WeightedRGB;
    bool includeBlack = true;        // パレットに黒を必ず含める
    bool keepTileConstraint = true;  // 機種のタイル/属性制約を適用

    // 画質調整 (-100..100, gamma 0.2..5)
    float brightness = 0, contrast = 0, saturation = 0, gamma = 1.0f;

    AlphaMode alpha = AlphaMode::Flatten;
    RGBA8 background{ 0, 0, 0, 255 };
    int alphaThreshold = 128;
};

// ---- 名前テーブル (CLI / GUI 共通) ----
struct EnumName { int value; const char* key; const char* label; };
const std::vector<EnumName>& ditherNames();
const std::vector<EnumName>& tilingNames();
const std::vector<EnumName>& tileStyleNames();
const std::vector<EnumName>& filterNames();
const std::vector<EnumName>& scaleNames();
const std::vector<EnumName>& aspectNames();
const std::vector<EnumName>& alphaNames();
const std::vector<EnumName>& metricNames();
const std::vector<EnumName>& colorsNames();
bool parseEnum(const std::vector<EnumName>& table, const std::string& key, int& out);
int enumIndex(const std::vector<EnumName>& table, int value);

} // namespace ric
