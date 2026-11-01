// RetroImageConv - machine presets
// SPDX-License-Identifier: MIT
//
// 機種名は略称のみを使用しています。各数値は一般に知られた画面仕様の概略値であり、
// 固定パレットの RGB 値は本ソフト独自の近似値です。
#pragma once

#include "Color.h"
#include "Options.h"
#include <string>
#include <vector>

namespace ric {

// タイル/属性単位の色制約
struct TileConstraint {
    int tileW = 0, tileH = 0;   // 0 = 制約なし
    int colorsPerTile = 0;      // 1 タイルで使える色数
    int subPalettes = 0;        // 0 = タイルごとに自由に選ぶ (属性方式) / >0 = 共有サブパレット数
    bool sharedColor0 = false;  // 全サブパレットで 0 番色(背景色)を共有
    bool enabled() const { return tileW > 0 && tileH > 0 && colorsPerTile > 0; }
};

struct ModeSpec {
    std::string id;
    std::string label;
    int width = 640, height = 400;
    double pixelAspect = 1.0;          // 1 ドットの 幅/高さ
    ColorSpace space;
    int paletteColors = 0;             // 0 = ダイレクトカラー / >0 = 色空間から選ぶ色数
    std::vector<RGBA8> fixedPalette;   // 固定パレット (デジタル色など)
    bool digital = false;              // デジタル (固定) パレット
    TileConstraint tile;
    int magScreenMode = -1;            // MAG 保存時のスクリーンモード (-1 = 自動)

    // 推奨設定
    DitherMethod recDither = DitherMethod::None;
    float recStrength = 1.0f;
    TilingMode recTiling = TilingMode::Off;
    TileStyle recStyle = TileStyle::Free;

    bool isPalette() const { return paletteColors > 0 || !fixedPalette.empty() || tile.enabled(); }
    int maxColors() const { return fixedPalette.empty() ? paletteColors : (paletteColors > 0 ? paletteColors : (int)fixedPalette.size()); }
    std::string summary() const;
};

struct Preset {
    std::string id;           // "98" など
    std::string description;  // 簡単な説明
    std::vector<ModeSpec> modes;
};

const std::vector<Preset>& presets();
const Preset* findPreset(const std::string& id);

// 固定パレット
std::vector<RGBA8> paletteDigital8();     // GRB デジタル 8 色 (色コード順)
std::vector<RGBA8> paletteMX1();         // 15 色 (近似)
std::vector<RGBA8> paletteFC();           // 家庭用 8bit 機 (近似生成)
std::vector<RGBA8> paletteGB(bool green); // 4 階調

} // namespace ric
