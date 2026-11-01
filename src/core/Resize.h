// RetroImageConv - resampling and layout
// SPDX-License-Identifier: MIT
#pragma once

#include "Image.h"
#include "Options.h"

namespace ric {

struct Layout {
    int canvasW = 0, canvasH = 0;   // 出力キャンバス
    int imgX = 0, imgY = 0;         // キャンバス上の画像位置
    int imgW = 0, imgH = 0;         // 画像部分のサイズ
    double srcX = 0, srcY = 0, srcW = 0, srcH = 0; // 使用する元画像の範囲
};

// 元画像 (sw x sh, ドット比 spar) を W x H (ドット比 dpar) の画面へ配置する
// mode == Crop のとき cropX/cropY (0..1) で切り抜き位置、cropZoom (>=1) で拡大率を指定
Layout computeLayout(int sw, int sh, double spar, int W, int H, double dpar, AspectMode mode,
                     double cropX = 0.5, double cropY = 0.5, double cropZoom = 1.0);

// 範囲 (sx,sy,sw,sh) を dw x dh に再サンプル。アルファは乗算済みで処理
ImageF resample(const ImageF& src, double sx, double sy, double sw, double sh, int dw, int dh, ResizeFilter f);

ImageF toFloat(const Image& img);

// ニアレストネイバー拡大 (出力用)
Image scaleNearest(const Image& src, int dw, int dh);

// 表示比補正・整数倍 (出力用)。pixelAspect を考慮して見た目通りの比率にする
Image applyOutputScale(const Image& src, bool parCorrect, int zoom);

} // namespace ric
