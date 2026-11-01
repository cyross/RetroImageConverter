// SPDX-License-Identifier: MIT
#include "Resize.h"
#include <cmath>
#include <algorithm>

namespace ric {

Layout computeLayout(int sw, int sh, double spar, int W, int H, double dpar, AspectMode mode,
                     double cropX, double cropY, double cropZoom) {
    Layout L;
    L.canvasW = W; L.canvasH = H;
    L.srcX = 0; L.srcY = 0; L.srcW = sw; L.srcH = sh;
    L.imgX = 0; L.imgY = 0; L.imgW = W; L.imgH = H;
    if (sw <= 0 || sh <= 0 || W <= 0 || H <= 0) return L;
    double srcVA = (double)sw * spar / sh;      // 元画像の見た目の縦横比
    double canvasVA = (double)W * dpar / H;     // 画面の見た目の縦横比
    switch (mode) {
    case AspectMode::Stretch:
        break;
    case AspectMode::KeepPad:
    case AspectMode::KeepNoPad: {
        int iw = W, ih = (int)std::lround(W * dpar / srcVA);
        if (ih > H) { ih = H; iw = (int)std::lround(H * srcVA / dpar); }
        iw = std::clamp(iw, 1, W); ih = std::clamp(ih, 1, H);
        L.imgW = iw; L.imgH = ih;
        if (mode == AspectMode::KeepPad) {
            L.imgX = (W - iw) / 2; L.imgY = (H - ih) / 2;
        } else {
            L.canvasW = iw; L.canvasH = ih;
        }
        break;
    }
    case AspectMode::Crop: {
        if (srcVA > canvasVA) L.srcW = sh * canvasVA / spar;
        else L.srcH = sw * spar / canvasVA;
        double z = std::clamp(cropZoom, 1.0, 64.0);
        L.srcW /= z; L.srcH /= z;
        L.srcX = (sw - L.srcW) * std::clamp(cropX, 0.0, 1.0);
        L.srcY = (sh - L.srcH) * std::clamp(cropY, 0.0, 1.0);
        break;
    }
    }
    return L;
}

ImageF toFloat(const Image& img) {
    ImageF f(img.width, img.height);
    for (size_t i = 0; i < img.pixels.size(); ++i) {
        const auto& p = img.pixels[i];
        f.rgb[i] = Vec3(p);
        f.alpha[i] = p.a / 255.f;
    }
    return f;
}

namespace {

struct Contrib { int first = 0; std::vector<float> w; };

float sinc(float x) {
    if (std::fabs(x) < 1e-6f) return 1.f;
    const float pi = 3.14159265358979f;
    return std::sin(pi * x) / (pi * x);
}

std::vector<Contrib> makeContribs(int srcN, double s0, double sLen, int dstN, ResizeFilter f) {
    std::vector<Contrib> c(dstN);
    double scale = sLen / dstN;
    for (int x = 0; x < dstN; ++x) {
        std::vector<std::pair<int, float>> tmp;
        if (f == ResizeFilter::Nearest) {
            int i = (int)std::floor(s0 + (x + 0.5) * scale);
            tmp.push_back({ std::clamp(i, 0, srcN - 1), 1.f });
        } else if (f == ResizeFilter::Area) {
            double a = s0 + x * scale, b = s0 + (x + 1) * scale;
            int i0 = (int)std::floor(a), i1 = (int)std::ceil(b);
            for (int i = i0; i < i1; ++i) {
                double ov = std::min(b, (double)i + 1) - std::max(a, (double)i);
                if (ov > 0) tmp.push_back({ std::clamp(i, 0, srcN - 1), (float)ov });
            }
        } else {
            double fs = std::max(scale, 1.0);
            double radius = (f == ResizeFilter::Lanczos3) ? 3.0 : 1.0;
            double support = radius * fs;
            double center = s0 + (x + 0.5) * scale;
            int i0 = (int)std::floor(center - support), i1 = (int)std::ceil(center + support);
            for (int i = i0; i <= i1; ++i) {
                float t = (float)((i + 0.5 - center) / fs);
                float w;
                if (f == ResizeFilter::Lanczos3) w = (std::fabs(t) < 3.f) ? sinc(t) * sinc(t / 3.f) : 0.f;
                else w = std::max(0.f, 1.f - std::fabs(t));
                if (w != 0.f) tmp.push_back({ std::clamp(i, 0, srcN - 1), w });
            }
        }
        if (tmp.empty()) {
            int i = std::clamp((int)std::floor(s0 + (x + 0.5) * scale), 0, srcN - 1);
            tmp.push_back({ i, 1.f });
        }
        int lo = srcN, hi = -1;
        float sum = 0;
        for (auto& t : tmp) { lo = std::min(lo, t.first); hi = std::max(hi, t.first); sum += t.second; }
        c[x].first = lo;
        c[x].w.assign(hi - lo + 1, 0.f);
        for (auto& t : tmp) c[x].w[t.first - lo] += t.second / (sum != 0 ? sum : 1.f);
    }
    return c;
}

} // namespace

ImageF resample(const ImageF& src, double sx, double sy, double sw, double sh, int dw, int dh, ResizeFilter f) {
    ImageF out(dw, dh);
    if (src.width <= 0 || src.height <= 0 || dw <= 0 || dh <= 0) return out;
    auto cx = makeContribs(src.width, sx, sw, dw, f);
    auto cy = makeContribs(src.height, sy, sh, dh, f);

    // 乗算済みアルファ (r,g,b,a) で水平方向 → 垂直方向
    struct P4 { float r, g, b, a; };
    std::vector<P4> tmp((size_t)dw * src.height);
    for (int y = 0; y < src.height; ++y) {
        const Vec3* row = &src.rgb[(size_t)y * src.width];
        const float* arow = &src.alpha[(size_t)y * src.width];
        for (int x = 0; x < dw; ++x) {
            P4 acc{ 0, 0, 0, 0 };
            const auto& c = cx[x];
            for (size_t k = 0; k < c.w.size(); ++k) {
                int i = c.first + (int)k;
                float w = c.w[k], a = arow[i];
                acc.r += row[i].r * a * w; acc.g += row[i].g * a * w; acc.b += row[i].b * a * w; acc.a += a * w;
            }
            tmp[(size_t)y * dw + x] = acc;
        }
    }
    for (int y = 0; y < dh; ++y) {
        const auto& c = cy[y];
        for (int x = 0; x < dw; ++x) {
            P4 acc{ 0, 0, 0, 0 };
            for (size_t k = 0; k < c.w.size(); ++k) {
                const P4& p = tmp[(size_t)(c.first + (int)k) * dw + x];
                float w = c.w[k];
                acc.r += p.r * w; acc.g += p.g * w; acc.b += p.b * w; acc.a += p.a * w;
            }
            size_t o = (size_t)y * dw + x;
            float a = std::clamp(acc.a, 0.f, 1.f);
            if (acc.a > 1e-6f) out.rgb[o] = clamp255(Vec3(acc.r / acc.a, acc.g / acc.a, acc.b / acc.a));
            else out.rgb[o] = Vec3(0, 0, 0);
            out.alpha[o] = a;
        }
    }
    return out;
}

Image scaleNearest(const Image& src, int dw, int dh) {
    Image out(dw, dh);
    out.pixelAspect = src.pixelAspect * ((double)src.width / dw) / ((double)src.height / dh);
    if (src.empty() || dw <= 0 || dh <= 0) return out;
    for (int y = 0; y < dh; ++y) {
        int sy = std::min(src.height - 1, (int)((int64_t)y * src.height / dh));
        for (int x = 0; x < dw; ++x) {
            int sxp = std::min(src.width - 1, (int)((int64_t)x * src.width / dw));
            out.at(x, y) = src.at(sxp, sy);
        }
    }
    return out;
}

Image applyOutputScale(const Image& src, bool parCorrect, int zoom) {
    if (src.empty()) return src;
    zoom = std::clamp(zoom, 1, 16);
    double fx = 1.0, fy = 1.0;
    if (parCorrect && std::fabs(src.pixelAspect - 1.0) > 1e-3) {
        if (src.pixelAspect < 1.0) fy = 1.0 / src.pixelAspect;
        else fx = src.pixelAspect;
        // 2:1 等の整数比に近ければ整数倍に
        if (std::fabs(fy - std::round(fy)) < 0.02) fy = std::round(fy);
        if (std::fabs(fx - std::round(fx)) < 0.02) fx = std::round(fx);
    }
    int dw = (int)std::lround(src.width * fx * zoom);
    int dh = (int)std::lround(src.height * fy * zoom);
    if (dw == src.width && dh == src.height) return src;
    Image out = scaleNearest(src, dw, dh);
    if (parCorrect) out.pixelAspect = 1.0;
    return out;
}

} // namespace ric
