// RetroImageConv - core image types
// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>

namespace ric {

struct RGBA8 {
    uint8_t r = 0, g = 0, b = 0, a = 255;
};
inline bool operator==(const RGBA8& x, const RGBA8& y) { return x.r == y.r && x.g == y.g && x.b == y.b && x.a == y.a; }
inline bool operator!=(const RGBA8& x, const RGBA8& y) { return !(x == y); }
inline RGBA8 rgb(int r, int g, int b) { return RGBA8{ (uint8_t)r, (uint8_t)g, (uint8_t)b, 255 }; }

// 8bit RGBA image. pixelAspect = 表示時の 1 ドットの (幅 / 高さ)。
// 例: 640x200 で縦長ドット(2:1)の場合 0.5
struct Image {
    int width = 0;
    int height = 0;
    double pixelAspect = 1.0;
    std::vector<RGBA8> pixels;

    Image() = default;
    Image(int w, int h, RGBA8 fill = RGBA8{}) : width(w), height(h), pixels((size_t)w * h, fill) {}
    bool empty() const { return width <= 0 || height <= 0; }
    RGBA8& at(int x, int y) { return pixels[(size_t)y * width + x]; }
    const RGBA8& at(int x, int y) const { return pixels[(size_t)y * width + x]; }
    bool hasAlpha() const {
        for (auto& p : pixels) if (p.a != 255) return true;
        return false;
    }
};

// パレット形式画像 (最大256色)
struct IndexedImage {
    int width = 0;
    int height = 0;
    double pixelAspect = 1.0;
    std::vector<uint8_t> indices;
    std::vector<RGBA8> palette;
    std::vector<uint8_t> alpha; // 空なら全不透明。各画素 0 or 255

    bool empty() const { return width <= 0 || height <= 0; }
    Image toRGBA() const {
        Image img(width, height);
        img.pixelAspect = pixelAspect;
        for (size_t i = 0; i < indices.size(); ++i) {
            RGBA8 c = indices[i] < palette.size() ? palette[indices[i]] : RGBA8{};
            c.a = alpha.empty() ? 255 : alpha[i];
            img.pixels[i] = c;
        }
        return img;
    }
};

struct Vec3 {
    float r = 0, g = 0, b = 0;
    Vec3() = default;
    Vec3(float r_, float g_, float b_) : r(r_), g(g_), b(b_) {}
    explicit Vec3(const RGBA8& c) : r(c.r), g(c.g), b(c.b) {}
    Vec3 operator+(const Vec3& o) const { return { r + o.r, g + o.g, b + o.b }; }
    Vec3 operator-(const Vec3& o) const { return { r - o.r, g - o.g, b - o.b }; }
    Vec3 operator*(float s) const { return { r * s, g * s, b * s }; }
    Vec3& operator+=(const Vec3& o) { r += o.r; g += o.g; b += o.b; return *this; }
    Vec3& operator-=(const Vec3& o) { r -= o.r; g -= o.g; b -= o.b; return *this; }
    Vec3& operator*=(float s) { r *= s; g *= s; b *= s; return *this; }
    float& operator[](int i) { return i == 0 ? r : (i == 1 ? g : b); }
    float operator[](int i) const { return i == 0 ? r : (i == 1 ? g : b); }
};

inline float clamp255(float v) { return v < 0.f ? 0.f : (v > 255.f ? 255.f : v); }
inline Vec3 clamp255(const Vec3& v) { return { clamp255(v.r), clamp255(v.g), clamp255(v.b) }; }
inline uint8_t toU8(float v) { return (uint8_t)std::lround(clamp255(v)); }
inline RGBA8 toRGBA8(const Vec3& v, uint8_t a = 255) { return RGBA8{ toU8(v.r), toU8(v.g), toU8(v.b), a }; }

// 浮動小数画像 (0..255 スケール, アルファ 0..1)
struct ImageF {
    int width = 0;
    int height = 0;
    std::vector<Vec3> rgb;
    std::vector<float> alpha;
    ImageF() = default;
    ImageF(int w, int h) : width(w), height(h), rgb((size_t)w * h), alpha((size_t)w * h, 1.f) {}
};

} // namespace ric
