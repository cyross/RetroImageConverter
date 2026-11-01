// RetroImageConv - color utilities and machine color spaces
// SPDX-License-Identifier: MIT
#pragma once

#include "Image.h"
#include <string>

namespace ric {

// ---- 距離 ----
enum class DistanceMetric { WeightedRGB, Lab };

// "redmean" 近似の加重 RGB 距離 (二乗)
inline float distWeighted(const Vec3& a, const Vec3& b) {
    float rm = (a.r + b.r) * 0.5f;
    float dr = a.r - b.r, dg = a.g - b.g, db = a.b - b.b;
    return (2.f + rm / 256.f) * dr * dr + 4.f * dg * dg + (2.f + (255.f - rm) / 256.f) * db * db;
}
inline float distLab(const Vec3& a, const Vec3& b) {
    float dl = a.r - b.r, da = a.g - b.g, db = a.b - b.b;
    return dl * dl + da * da + db * db;
}

Vec3 rgbToLab(const Vec3& rgb);      // 入力 0..255 sRGB / 出力 L*a*b*
float srgbToLinear(float v255);       // 0..255 -> 0..1
float linearToSrgb(float lin);        // 0..1 -> 0..255
float luma(const Vec3& c);            // 0..255

// ---- 機種の色空間 ----
enum class SpaceKind {
    Full,   // 24bit
    Bits,   // 各チャンネル n bit
    X68K,   // GRB 5-5-5 + 輝度ビット(I)
    YJK     // YJK (4 ドット単位で色差を共有)
};

struct ColorSpace {
    SpaceKind kind = SpaceKind::Full;
    int rBits = 8, gBits = 8, bBits = 8;

    static ColorSpace full() { return {}; }
    static ColorSpace bits(int r, int g, int b) { ColorSpace s; s.kind = SpaceKind::Bits; s.rBits = r; s.gBits = g; s.bBits = b; return s; }
    static ColorSpace bits(int n) { return bits(n, n, n); }
    static ColorSpace x68k() { ColorSpace s; s.kind = SpaceKind::X68K; s.rBits = s.gBits = s.bBits = 5; return s; }
    static ColorSpace yjk() { ColorSpace s; s.kind = SpaceKind::YJK; s.rBits = s.gBits = s.bBits = 5; return s; }

    // 表現可能な最も近い色に丸める
    RGBA8 snap(const Vec3& c) const;
    // 1 チャンネルの量子化ステップ (順序ディザ用)
    float step(int ch) const;
    // 総色数 (表示用)
    double totalColors() const;
    std::string describe() const;
};

uint8_t quantizeChannel(float v, int bits);   // 0..255 -> 丸めた 8bit 値
uint8_t expandBits(int level, int bits);      // level(0..2^bits-1) -> 8bit

} // namespace ric
