// SPDX-License-Identifier: MIT
#include "Converter.h"
#include "Resize.h"
#include "Palette.h"
#include "Quantizer.h"
#include <chrono>
#include <cmath>
#include <unordered_set>

namespace ric {

ModeSpec effectiveMode(const ConvertOptions& opt) {
    const Preset* p = findPreset(opt.presetId);
    if (!p) p = &presets()[0];
    int mi = std::clamp(opt.modeIndex, 0, (int)p->modes.size() - 1);
    ModeSpec m = p->modes[mi];
    int n = opt.colorsOverride;
    if (n > 0) {
        if (n >= 16777216) {
            m.space = ColorSpace::full(); m.paletteColors = 0; m.fixedPalette.clear(); m.tile = {}; m.digital = false;
        } else if (n == 65536) {
            m.space = ColorSpace::bits(5, 6, 5); m.paletteColors = 0; m.fixedPalette.clear(); m.tile = {}; m.digital = false;
        } else {
            n = std::clamp(n, 2, 256);
            if (m.space.kind == SpaceKind::YJK) m.space = ColorSpace::bits(5);
            m.paletteColors = n;
            m.tile = {};
        }
        m.magScreenMode = -1;
    }
    if (!opt.keepTileConstraint) m.tile = {};
    return m;
}

void applyRecommended(const ModeSpec& mode, ConvertOptions& opt) {
    opt.dither = mode.recDither;
    opt.ditherStrength = mode.recStrength;
    opt.tiling = mode.recTiling;
    opt.tileStyle = mode.recStyle;
}

bool cropRect(int sw, int sh, double spar, const ConvertOptions& opt, double& x, double& y, double& w, double& h) {
    if (opt.aspect != AspectMode::Crop || opt.scale == ScaleMode::Original) return false;
    ModeSpec mode = effectiveMode(opt);
    int W = mode.width, H = mode.height;
    if (opt.scale == ScaleMode::Custom) { W = std::clamp(opt.customWidth, 1, 16384); H = std::clamp(opt.customHeight, 1, 16384); }
    Layout L = computeLayout(sw, sh, spar, W, H, mode.pixelAspect, opt.aspect, opt.cropX, opt.cropY, opt.cropZoom);
    x = L.srcX; y = L.srcY; w = L.srcW; h = L.srcH;
    return true;
}

int countColors(const Image& img) {
    std::unordered_set<uint32_t> s;
    for (auto& p : img.pixels) {
        if (p.a == 0) continue;
        s.insert(((uint32_t)p.r << 16) | ((uint32_t)p.g << 8) | p.b);
    }
    return (int)s.size();
}

namespace {

void adjust(ImageF& f, const ConvertOptions& o) {
    bool any = o.brightness != 0 || o.contrast != 0 || o.saturation != 0 || std::fabs(o.gamma - 1.f) > 1e-3f;
    if (!any) return;
    float b = o.brightness * 2.55f;
    float c = 1.f + o.contrast / 100.f;
    float s = 1.f + o.saturation / 100.f;
    float invG = 1.f / std::clamp(o.gamma, 0.1f, 10.f);
    for (auto& v : f.rgb) {
        Vec3 x = v;
        if (invG != 1.f) for (int ch = 0; ch < 3; ++ch) x[ch] = 255.f * std::pow(clamp255(x[ch]) / 255.f, invG);
        for (int ch = 0; ch < 3; ++ch) x[ch] = (x[ch] - 128.f) * c + 128.f + b;
        float l = luma(x);
        for (int ch = 0; ch < 3; ++ch) x[ch] = l + (x[ch] - l) * s;
        v = clamp255(x);
    }
}

} // namespace

ConvertResult convert(const Image& src, const ConvertOptions& opt, const std::atomic<bool>* cancel) {
    auto t0 = std::chrono::steady_clock::now();
    ConvertResult R;
    if (src.empty()) { R.error = "入力画像が空です"; return R; }
    ModeSpec mode = effectiveMode(opt);

    // ---- サイズ決定と縮小
    int W = mode.width, H = mode.height;
    if (opt.scale == ScaleMode::Custom) { W = std::clamp(opt.customWidth, 1, 16384); H = std::clamp(opt.customHeight, 1, 16384); }
    ImageF canvas;
    double outPar = mode.pixelAspect;
    if (opt.scale == ScaleMode::Original) {
        canvas = toFloat(src);
        outPar = src.pixelAspect;
    } else {
        Layout L = computeLayout(src.width, src.height, src.pixelAspect, W, H, mode.pixelAspect, opt.aspect,
                                 opt.cropX, opt.cropY, opt.cropZoom);
        ImageF s = toFloat(src);
        ImageF scaled = resample(s, L.srcX, L.srcY, L.srcW, L.srcH, L.imgW, L.imgH, opt.filter);
        canvas = ImageF(L.canvasW, L.canvasH);
        bool transparentPad = opt.alpha != AlphaMode::Flatten;
        for (size_t i = 0; i < canvas.rgb.size(); ++i) {
            canvas.rgb[i] = Vec3(opt.background);
            canvas.alpha[i] = transparentPad ? 0.f : 1.f;
        }
        for (int y = 0; y < L.imgH; ++y)
            for (int x = 0; x < L.imgW; ++x) {
                size_t d = (size_t)(y + L.imgY) * canvas.width + (x + L.imgX);
                size_t si = (size_t)y * L.imgW + x;
                canvas.rgb[d] = scaled.rgb[si];
                canvas.alpha[d] = scaled.alpha[si];
            }
    }
    if (cancel && cancel->load()) { R.error = "cancel"; return R; }
    const int w = canvas.width, h = canvas.height;

    adjust(canvas, opt);

    // ---- アルファ
    std::vector<uint8_t> opaque((size_t)w * h, 1);
    std::vector<uint8_t> alphaOut((size_t)w * h, 255);
    std::vector<float> weights((size_t)w * h, 1.f);
    Vec3 bg(opt.background);
    for (size_t i = 0; i < canvas.rgb.size(); ++i) {
        float a = canvas.alpha[i];
        switch (opt.alpha) {
        case AlphaMode::Flatten:
            canvas.rgb[i] = canvas.rgb[i] * a + bg * (1.f - a);
            break;
        case AlphaMode::Keep:
            alphaOut[i] = toU8(a * 255.f);
            weights[i] = a;
            if (alphaOut[i] == 0) { opaque[i] = 0; }
            break;
        case AlphaMode::Binary:
            if (a * 255.f >= (float)opt.alphaThreshold) { alphaOut[i] = 255; }
            else { alphaOut[i] = 0; opaque[i] = 0; weights[i] = 0; }
            break;
        }
    }

    QuantizeParams qp;
    qp.dither = opt.dither;
    qp.strength = std::clamp(opt.ditherStrength, 0.f, 1.f);
    qp.serpentine = opt.serpentine;
    qp.tiling = opt.tiling;
    qp.style = opt.tileStyle;
    qp.metric = opt.metric;
    qp.digitalPalette = mode.digital;
    qp.linearLight = opt.linearLight;
    qp.cancel = cancel;

    Image out(w, h);
    out.pixelAspect = outPar;

    if (mode.isPalette()) {
        std::vector<RGBA8> palette;
        TileAssign tiles;
        bool mixAware = opt.dither != DitherMethod::None || opt.tiling != TilingMode::Off;
        if (mode.tile.enabled()) {
            solveTileConstraint(canvas.rgb, opaque, w, h, mode, opt.includeBlack, mixAware, opt.linearLight, palette, tiles);
        } else {
            const std::vector<RGBA8>* fixed = mode.fixedPalette.empty() ? nullptr : &mode.fixedPalette;
            int n = mode.maxColors();
            if (fixed && (int)fixed->size() <= n) palette = *fixed;
            else {
                std::vector<Vec3> px;
                std::vector<float> wv;
                px.reserve(canvas.rgb.size());
                for (size_t i = 0; i < canvas.rgb.size(); ++i) if (opaque[i]) { px.push_back(canvas.rgb[i]); wv.push_back(weights[i]); }
                auto hist = buildHistogram(px, &wv, mode.space);
                PaletteGenOptions go;
                go.fixedSet = fixed;
                go.includeBlack = opt.includeBlack;
                palette = generatePalette(hist, n, mode.space, go);
            }
        }
        if (cancel && cancel->load()) { R.error = "cancel"; return R; }
        std::vector<uint8_t> idx;
        quantizePalette(canvas.rgb, opaque, w, h, palette, tiles.enabled() ? &tiles : nullptr, qp, idx);
        if (cancel && cancel->load()) { R.error = "cancel"; return R; }
        R.indexed = true;
        R.indexedImage.width = w; R.indexedImage.height = h;
        R.indexedImage.pixelAspect = outPar;
        R.indexedImage.indices = idx;
        R.indexedImage.palette = palette;
        bool anyAlpha = false;
        for (auto a : alphaOut) if (a != 255) { anyAlpha = true; break; }
        if (anyAlpha) {
            R.indexedImage.alpha = alphaOut;
            if (opt.alpha == AlphaMode::Keep) for (auto& a : R.indexedImage.alpha) a = a >= 128 ? 255 : 0;
        }
        for (size_t i = 0; i < idx.size(); ++i) {
            RGBA8 c = palette[idx[i]];
            c.a = alphaOut[i];
            out.pixels[i] = c;
        }
    } else {
        std::vector<RGBA8> px;
        if (mode.space.kind == SpaceKind::YJK) quantizeYJK(canvas.rgb, opaque, w, h, qp, px);
        else quantizeDirect(canvas.rgb, opaque, w, h, mode.space, qp, px);
        if (cancel && cancel->load()) { R.error = "cancel"; return R; }
        for (size_t i = 0; i < px.size(); ++i) {
            RGBA8 c = px[i];
            if (!opaque[i]) c = toRGBA8(canvas.rgb[i]);
            c.a = alphaOut[i];
            out.pixels[i] = c;
        }
    }

    R.image = std::move(out);
    R.mode = mode;
    R.usedColors = countColors(R.image);
    R.ok = true;
    R.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    return R;
}

} // namespace ric
