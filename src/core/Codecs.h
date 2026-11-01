// RetroImageConv - built-in encoders/decoders (BMP / GIF / WebP lossless)
// SPDX-License-Identifier: MIT
#pragma once

#include "Image.h"
#include <cstdint>
#include <string>
#include <vector>

namespace ric {

// BMP: 1/4/8/24/32bit 非圧縮の読み込み、24/32bit の書き出し
bool decodeBMP(const std::vector<uint8_t>& data, Image& out, std::string* error);
void encodeBMP(const Image& img, bool withAlpha, std::vector<uint8_t>& out);

// GIF (GIF89a, 1 フレーム)。パレットは 256 色以下。transparentIndex < 0 で透過なし
void encodeGIF(const IndexedImage& img, std::vector<uint8_t>& out);

// WebP ロスレス (VP8L)
void encodeWebPLossless(const Image& img, std::vector<uint8_t>& out);

// 256 色以下のパレット画像を作る (既にパレットがある場合はそれを使う)
IndexedImage makeIndexed256(const Image& img);

} // namespace ric
