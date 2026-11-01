// SPDX-License-Identifier: MIT
#include "Color.h"
#include <cmath>
#include <cstdio>

namespace ric {

float srgbToLinear(float v255) {
    float v = v255 / 255.f;
    if (v <= 0.04045f) return v / 12.92f;
    return std::pow((v + 0.055f) / 1.055f, 2.4f);
}

float linearToSrgb(float lin) {
    if (lin <= 0.f) return 0.f;
    if (lin >= 1.f) return 255.f;
    float v = (lin <= 0.0031308f) ? lin * 12.92f : 1.055f * std::pow(lin, 1.f / 2.4f) - 0.055f;
    return v * 255.f;
}

float luma(const Vec3& c) { return 0.299f * c.r + 0.587f * c.g + 0.114f * c.b; }

Vec3 rgbToLab(const Vec3& c) {
    float r = srgbToLinear(clamp255(c.r)), g = srgbToLinear(clamp255(c.g)), b = srgbToLinear(clamp255(c.b));
    float x = (0.4124564f * r + 0.3575761f * g + 0.1804375f * b) / 0.95047f;
    float y = (0.2126729f * r + 0.7151522f * g + 0.0721750f * b);
    float z = (0.0193339f * r + 0.1191920f * g + 0.9503041f * b) / 1.08883f;
    auto f = [](float t) { return t > 0.008856f ? std::cbrt(t) : (7.787f * t + 16.f / 116.f); };
    float fx = f(x), fy = f(y), fz = f(z);
    return { 116.f * fy - 16.f, 500.f * (fx - fy), 200.f * (fy - fz) };
}

uint8_t expandBits(int level, int bits) {
    if (bits >= 8) return (uint8_t)level;
    int maxv = (1 << bits) - 1;
    return (uint8_t)((level * 255 + maxv / 2) / maxv);
}

uint8_t quantizeChannel(float v, int bits) {
    if (bits >= 8) return toU8(v);
    int maxv = (1 << bits) - 1;
    int level = (int)std::lround(clamp255(v) * maxv / 255.f);
    return expandBits(level, bits);
}

RGBA8 ColorSpace::snap(const Vec3& c) const {
    switch (kind) {
    case SpaceKind::Full:
        return toRGBA8(c);
    case SpaceKind::Bits:
    case SpaceKind::YJK:
        return RGBA8{ quantizeChannel(c.r, rBits), quantizeChannel(c.g, gBits), quantizeChannel(c.b, bBits), 255 };
    case SpaceKind::X68K: {
        // 6bit 値 = (5bit << 1) | I。I は 3 チャンネル共通
        RGBA8 best{};
        float bestErr = 1e30f;
        for (int I = 0; I < 2; ++I) {
            RGBA8 cand{};
            float err = 0;
            for (int ch = 0; ch < 3; ++ch) {
                float v = clamp255(c[ch]) * 63.f / 255.f;
                int l5 = (int)std::lround((v - I) / 2.f);
                l5 = std::clamp(l5, 0, 31);
                uint8_t o = expandBits((l5 << 1) | I, 6);
                float d = (float)o - c[ch];
                err += d * d;
                (ch == 0 ? cand.r : ch == 1 ? cand.g : cand.b) = o;
            }
            if (err < bestErr) { bestErr = err; best = cand; }
        }
        return best;
    }
    }
    return toRGBA8(c);
}

float ColorSpace::step(int ch) const {
    int b = ch == 0 ? rBits : (ch == 1 ? gBits : bBits);
    switch (kind) {
    case SpaceKind::Full: return 1.f;
    case SpaceKind::X68K: return 255.f / 31.f;
    default: return b >= 8 ? 1.f : 255.f / (float)((1 << b) - 1);
    }
}

double ColorSpace::totalColors() const {
    switch (kind) {
    case SpaceKind::Full: return 16777216.0;
    case SpaceKind::X68K: return 65536.0;
    case SpaceKind::YJK: return 19268.0;
    default: return std::pow(2.0, rBits + gBits + bBits);
    }
}

std::string ColorSpace::describe() const {
    char buf[64];
    switch (kind) {
    case SpaceKind::Full: return "24bit";
    case SpaceKind::X68K: return "GRB555+I";
    case SpaceKind::YJK: return "YJK";
    default:
        std::snprintf(buf, sizeof(buf), "RGB%d%d%d", rBits, gBits, bBits);
        return buf;
    }
}

} // namespace ric
