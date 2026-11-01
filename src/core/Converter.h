// RetroImageConv - conversion pipeline
// SPDX-License-Identifier: MIT
#pragma once

#include "Image.h"
#include "Options.h"
#include "Presets.h"
#include <atomic>
#include <string>

namespace ric {

struct ConvertResult {
    bool ok = false;
    std::string error;
    Image image;                 // 変換結果 (RGBA, フルカラー格納)
    bool indexed = false;        // パレット方式か
    IndexedImage indexedImage;   // パレット方式の場合の番号画像
    ModeSpec mode;               // 実際に使ったモード (上書き適用後)
    int usedColors = 0;
    double seconds = 0;
};

// 色数の上書き等を反映したモードを返す
ModeSpec effectiveMode(const ConvertOptions& opt);

ConvertResult convert(const Image& src, const ConvertOptions& opt, const std::atomic<bool>* cancel = nullptr);

// 推奨ディザ設定をオプションに反映
void applyRecommended(const ModeSpec& mode, ConvertOptions& opt);

int countColors(const Image& img);

// 切り抜きモードのとき、元画像上の切り抜き範囲を返す (切り抜きでなければ false)
bool cropRect(int sw, int sh, double spar, const ConvertOptions& opt, double& x, double& y, double& w, double& h);

} // namespace ric
