// SPDX-License-Identifier: MIT
#include "Quantizer.h"
#include "Palette.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>

namespace ric {

namespace {

// ---------------------------------------------------------------- 色検索
class Matcher {
public:
    Matcher(const std::vector<Vec3>& colors, DistanceMetric m) : metric_(m), cols_(colors) {
        if (metric_ == DistanceMetric::Lab) {
            space_.reserve(cols_.size());
            for (auto& c : cols_) space_.push_back(rgbToLab(c));
        }
    }
    // subset == nullptr なら全色
    int nearest(const Vec3& c, const uint8_t* subset, size_t n) const {
        int best = 0; float bd = 1e30f;
        if (metric_ == DistanceMetric::Lab) {
            Vec3 q = rgbToLab(c);
            if (subset) { for (size_t i = 0; i < n; ++i) { float d = distLab(q, space_[subset[i]]); if (d < bd) { bd = d; best = subset[i]; } } }
            else { for (size_t i = 0; i < space_.size(); ++i) { float d = distLab(q, space_[i]); if (d < bd) { bd = d; best = (int)i; } } }
        } else {
            if (subset) { for (size_t i = 0; i < n; ++i) { float d = distWeighted(c, cols_[subset[i]]); if (d < bd) { bd = d; best = subset[i]; } } }
            else { for (size_t i = 0; i < cols_.size(); ++i) { float d = distWeighted(c, cols_[i]); if (d < bd) { bd = d; best = (int)i; } } }
        }
        return best;
    }
    // 番号リスト (int) 版
    int nearestList(const Vec3& c, const std::vector<int>& list) const {
        int best = list.empty() ? 0 : list[0]; float bd = 1e30f;
        Vec3 q = metric_ == DistanceMetric::Lab ? rgbToLab(c) : c;
        for (int i : list) {
            float d = metric_ == DistanceMetric::Lab ? distLab(q, space_[i]) : distWeighted(c, cols_[i]);
            if (d < bd) { bd = d; best = i; }
        }
        return best;
    }
private:
    DistanceMetric metric_;
    std::vector<Vec3> cols_;
    std::vector<Vec3> space_;
};

// ---------------------------------------------------------------- ディザ行列
float bayer(int n, int x, int y) {
    // 標準ベイヤー行列 (n = 2,4,8)。細かいビットほど上位桁
    int v = 0;
    for (int bit = 1; bit < n; bit <<= 1) {
        int bx = (x & bit) ? 1 : 0, by = (y & bit) ? 1 : 0;
        int m = (!bx && !by) ? 0 : (bx && !by) ? 2 : (!bx && by) ? 3 : 1; // [[0,2],[3,1]]
        v = v * 4 + m;
    }
    return ((float)v + 0.5f) / (float)(n * n);
}

float orderedThreshold(DitherMethod d, int x, int y) {
    switch (d) {
    case DitherMethod::Bayer2: return bayer(2, x & 1, y & 1);
    case DitherMethod::Bayer4: return bayer(4, x & 3, y & 3);
    case DitherMethod::Bayer8: return bayer(8, x & 7, y & 7);
    default: return 0.5f;
    }
}

bool isOrdered(DitherMethod d) { return d == DitherMethod::Bayer2 || d == DitherMethod::Bayer4 || d == DitherMethod::Bayer8; }

struct KTap { int dx, dy; float w; };
std::vector<KTap> kernel(DitherMethod d) {
    auto mk = [](std::initializer_list<KTap> l, float div) {
        std::vector<KTap> v(l);
        for (auto& t : v) t.w /= div;
        return v;
    };
    switch (d) {
    case DitherMethod::FloydSteinberg: return mk({ { 1, 0, 7 }, { -1, 1, 3 }, { 0, 1, 5 }, { 1, 1, 1 } }, 16);
    case DitherMethod::JarvisJudiceNinke: return mk({ { 1, 0, 7 }, { 2, 0, 5 }, { -2, 1, 3 }, { -1, 1, 5 }, { 0, 1, 7 }, { 1, 1, 5 }, { 2, 1, 3 },
                                                      { -2, 2, 1 }, { -1, 2, 3 }, { 0, 2, 5 }, { 1, 2, 3 }, { 2, 2, 1 } }, 48);
    case DitherMethod::Stucki: return mk({ { 1, 0, 8 }, { 2, 0, 4 }, { -2, 1, 2 }, { -1, 1, 4 }, { 0, 1, 8 }, { 1, 1, 4 }, { 2, 1, 2 },
                                           { -2, 2, 1 }, { -1, 2, 2 }, { 0, 2, 4 }, { 1, 2, 2 }, { 2, 2, 1 } }, 42);
    case DitherMethod::Atkinson: return mk({ { 1, 0, 1 }, { 2, 0, 1 }, { -1, 1, 1 }, { 0, 1, 1 }, { 1, 1, 1 }, { 0, 2, 1 } }, 8);
    case DitherMethod::Sierra3: return mk({ { 1, 0, 5 }, { 2, 0, 3 }, { -2, 1, 2 }, { -1, 1, 4 }, { 0, 1, 5 }, { 1, 1, 4 }, { 2, 1, 2 },
                                            { -1, 2, 2 }, { 0, 2, 3 }, { 1, 2, 2 } }, 32);
    case DitherMethod::SierraLite: return mk({ { 1, 0, 2 }, { -1, 1, 1 }, { 0, 1, 1 } }, 4);
    default: return {};
    }
}

// 誤差バッファ
class ErrorBuffer {
public:
    ErrorBuffer(int w, int h, bool enabled) : w_(w), h_(h) { if (enabled) e_.assign((size_t)w * h, Vec3(0, 0, 0)); }
    bool enabled() const { return !e_.empty(); }
    Vec3 get(int x, int y) const { return e_.empty() ? Vec3(0, 0, 0) : e_[(size_t)y * w_ + x]; }
    void spread(int x, int y, const Vec3& err, const std::vector<KTap>& k, bool mirror, const std::vector<uint8_t>& opaque) {
        for (auto& t : k) {
            int nx = x + (mirror ? -t.dx : t.dx), ny = y + t.dy;
            if (nx < 0 || nx >= w_ || ny >= h_) continue;
            size_t i = (size_t)ny * w_ + nx;
            if (!opaque[i]) continue;
            e_[i] += err * t.w;
        }
    }
private:
    int w_, h_;
    std::vector<Vec3> e_;
};

// 誤差の計算空間 (sRGB そのまま / 線形光)
struct WorkSpace {
    bool linear;
    Vec3 to(const Vec3& c) const {
        if (!linear) return c;
        return Vec3(srgbToLinear(c.r) * 255.f, srgbToLinear(c.g) * 255.f, srgbToLinear(c.b) * 255.f);
    }
    Vec3 from(const Vec3& c) const {
        if (!linear) return c;
        return Vec3(linearToSrgb(c.r / 255.f), linearToSrgb(c.g / 255.f), linearToSrgb(c.b / 255.f));
    }
};

// ---------------------------------------------------------------- タイリング
float tileThreshold(TilingMode m, int x, int y) {
    switch (m) {
    case TilingMode::Checker: return ((x + y) & 1) ? 0.75f : 0.25f;
    case TilingMode::VStripe: return (x & 1) ? 0.75f : 0.25f;
    case TilingMode::HStripe: return (y & 1) ? 0.75f : 0.25f;
    case TilingMode::Quad: return bayer(2, x & 1, y & 1);
    case TilingMode::Fine4x4: return bayer(4, x & 3, y & 3);
    default: return 0.f;
    }
}

std::vector<float> tileRatios(TilingMode m) {
    switch (m) {
    case TilingMode::Quad: return { 0.25f, 0.5f, 0.75f };
    case TilingMode::Fine4x4: { std::vector<float> r; for (int i = 1; i < 16; ++i) r.push_back(i / 16.f); return r; }
    case TilingMode::Off: return {};
    default: return { 0.5f };
    }
}

struct VirtualPalette {
    std::vector<uint8_t> c1, c2;
    std::vector<float> ratio;
    std::vector<Vec3> mean;
    Matcher* matcher = nullptr;
    std::vector<Matcher> holder;
};

int digitalCode(const RGBA8& c) {
    return (c.b >= 128 ? 1 : 0) | (c.r >= 128 ? 2 : 0) | (c.g >= 128 ? 4 : 0);
}
bool isPureDigital(const RGBA8& c) {
    auto p = [](uint8_t v) { return v == 0 || v == 255; };
    return p(c.r) && p(c.g) && p(c.b);
}

// 2 色を t の割合で混ぜた見た目の色。linear = true なら線形光 (CRT 実機に近い) で計算
Vec3 mixColors(const Vec3& a, const Vec3& b, float t, bool linear) {
    if (!linear) return a + (b - a) * t;
    Vec3 o;
    for (int ch = 0; ch < 3; ++ch) {
        float la = srgbToLinear(a[ch]), lb = srgbToLinear(b[ch]);
        o[ch] = linearToSrgb(la + (lb - la) * t);
    }
    return o;
}

std::vector<std::vector<int>> knn(const std::vector<Vec3>& cols, int k) {
    std::vector<std::vector<int>> res(cols.size());
    for (size_t i = 0; i < cols.size(); ++i) {
        std::vector<std::pair<float, int>> d;
        for (size_t j = 0; j < cols.size(); ++j) if (j != i) d.push_back({ distWeighted(cols[i], cols[j]), (int)j });
        std::sort(d.begin(), d.end());
        for (int n = 0; n < k && n < (int)d.size(); ++n) res[i].push_back(d[n].second);
    }
    return res;
}

VirtualPalette buildVirtual(const std::vector<RGBA8>& pal, const std::vector<uint8_t>& subset, TilingMode mode, TileStyle style,
                            bool digital, DistanceMetric metric, bool linear) {
    VirtualPalette vp;
    std::vector<Vec3> cols;
    for (auto i : subset) cols.push_back(Vec3(pal[i]));
    const int n = (int)cols.size();
    for (int i = 0; i < n; ++i) { vp.c1.push_back(subset[i]); vp.c2.push_back(subset[i]); vp.ratio.push_back(0.f); vp.mean.push_back(cols[i]); }

    // 明暗の端
    int darkest = 0, brightest = 0;
    for (int i = 1; i < n; ++i) {
        if (luma(cols[i]) < luma(cols[darkest])) darkest = i;
        if (luma(cols[i]) > luma(cols[brightest])) brightest = i;
    }
    bool allDigital = digital;
    if (!allDigital) {
        allDigital = n <= 8;
        for (auto& c : cols) if (!isPureDigital(toRGBA8(c))) { allDigital = false; break; }
    }
    int kFree = n > 24 ? 8 : n;
    int kStyle = style == TileStyle::Style98 ? 4 : 3;
    auto nnFree = knn(cols, kFree);
    auto nnStyle = knn(cols, kStyle);
    auto inList = [](const std::vector<int>& l, int v) { return std::find(l.begin(), l.end(), v) != l.end(); };

    auto allowed = [&](int i, int j) -> bool {
        switch (style) {
        case TileStyle::Free:
            return n <= 24 || inList(nnFree[i], j) || inList(nnFree[j], i);
        case TileStyle::Style88:
            if (allDigital) {
                RGBA8 a = toRGBA8(cols[i]), b = toRGBA8(cols[j]);
                int diff = digitalCode(a) ^ digitalCode(b);
                int bits = (diff & 1) + ((diff >> 1) & 1) + ((diff >> 2) & 1);
                bool bw = digitalCode(a) == 0 || digitalCode(a) == 7 || digitalCode(b) == 0 || digitalCode(b) == 7;
                return bits == 1 || bw;
            }
            return inList(nnStyle[i], j) || inList(nnStyle[j], i) || i == darkest || j == darkest || i == brightest || j == brightest;
        case TileStyle::Style98:
            return (inList(nnStyle[i], j) || inList(nnStyle[j], i)) && distWeighted(cols[i], cols[j]) < 9.f * 255.f * 255.f * 0.35f * 0.35f;
        }
        return true;
    };

    auto ratios = tileRatios(mode);
    for (int i = 0; i < n; ++i) {
        for (int j = i + 1; j < n; ++j) {
            if (!allowed(i, j)) continue;
            for (float r : ratios) {
                vp.c1.push_back(subset[i]); vp.c2.push_back(subset[j]); vp.ratio.push_back(r);
                vp.mean.push_back(mixColors(cols[i], cols[j], r, linear));
            }
        }
    }
    vp.holder.emplace_back(vp.mean, metric);
    return vp;
}

} // namespace

// ================================================================ パレット方式
void quantizePalette(const std::vector<Vec3>& rgb, const std::vector<uint8_t>& opaque, int w, int h,
                     const std::vector<RGBA8>& palette, const TileAssign* tiles, const QuantizeParams& p,
                     std::vector<uint8_t>& outIdx) {
    outIdx.assign((size_t)w * h, 0);
    if (palette.empty()) return;
    std::vector<Vec3> palF;
    for (auto& c : palette) palF.push_back(Vec3(c));
    Matcher matcher(palF, p.metric);

    bool useTiles = tiles && tiles->enabled();
    bool tiling = p.tiling != TilingMode::Off;
    auto k = kernel(p.dither);
    bool diffusion = !k.empty() && p.strength > 0;
    bool ordered = isOrdered(p.dither) && p.strength > 0 && !tiling;
    ErrorBuffer err(w, h, diffusion);
    WorkSpace ws{ p.linearLight && diffusion };

    // 順序ディザの振幅: パレットの平均最近傍距離
    float spread = 0;
    if (ordered) {
        double sum = 0;
        for (size_t i = 0; i < palF.size(); ++i) {
            float bd = 1e30f;
            for (size_t j = 0; j < palF.size(); ++j) if (i != j) {
                Vec3 d = palF[i] - palF[j];
                bd = std::min(bd, std::sqrt(d.r * d.r + d.g * d.g + d.b * d.b));
            }
            sum += (palF.size() > 1 ? bd : 64.f);
        }
        spread = (float)(sum / palF.size()) * p.strength;
    }

    // タイリング用の仮想パレット (サブセット毎にキャッシュ)
    std::vector<uint8_t> all(palette.size());
    std::iota(all.begin(), all.end(), 0);
    std::vector<VirtualPalette> vpCache;
    if (tiling) {
        if (useTiles) for (auto& s : tiles->subsets) vpCache.push_back(buildVirtual(palette, s, p.tiling, p.style, p.digitalPalette, p.metric, p.linearLight));
        else vpCache.push_back(buildVirtual(palette, all, p.tiling, p.style, p.digitalPalette, p.metric, p.linearLight));
    }

    for (int y = 0; y < h; ++y) {
        if (p.cancel && p.cancel->load()) return;
        bool rev = p.serpentine && diffusion && (y & 1);
        for (int xi = 0; xi < w; ++xi) {
            int x = rev ? (w - 1 - xi) : xi;
            size_t i = (size_t)y * w + x;
            if (!opaque[i]) continue;
            Vec3 work = clamp255(ws.to(rgb[i]) + err.get(x, y));
            Vec3 target = ws.from(work);
            Vec3 q;
            int sid = useTiles ? tiles->subsetAt(x, y) : -1;
            if (tiling) {
                const VirtualPalette& vp = vpCache[useTiles ? sid : 0];
                int e = vp.holder[0].nearest(target, nullptr, 0);
                float thr = tileThreshold(p.tiling, x, y);
                outIdx[i] = (vp.ratio[e] > 0.f && thr < vp.ratio[e]) ? vp.c2[e] : vp.c1[e];
                q = vp.mean[e];
            } else {
                Vec3 t2 = target;
                if (ordered) {
                    float o = (orderedThreshold(p.dither, x, y) - 0.5f) * spread;
                    t2 = clamp255(Vec3(t2.r + o, t2.g + o, t2.b + o));
                }
                int idx = useTiles ? matcher.nearest(t2, tiles->subsets[sid].data(), tiles->subsets[sid].size())
                                   : matcher.nearest(t2, nullptr, 0);
                outIdx[i] = (uint8_t)idx;
                q = palF[idx];
            }
            if (diffusion) err.spread(x, y, (work - ws.to(q)) * p.strength, k, rev, opaque);
        }
    }
}

// ================================================================ ダイレクトカラー
void quantizeDirect(const std::vector<Vec3>& rgb, const std::vector<uint8_t>& opaque, int w, int h,
                    const ColorSpace& space, const QuantizeParams& p, std::vector<RGBA8>& out) {
    out.assign((size_t)w * h, RGBA8{});
    auto k = kernel(p.dither);
    bool diffusion = !k.empty() && p.strength > 0 && space.kind != SpaceKind::Full;
    bool ordered = isOrdered(p.dither) && p.strength > 0 && space.kind != SpaceKind::Full;
    ErrorBuffer err(w, h, diffusion);
    WorkSpace ws{ p.linearLight && diffusion };
    float st[3] = { space.step(0), space.step(1), space.step(2) };
    for (int y = 0; y < h; ++y) {
        if (p.cancel && p.cancel->load()) return;
        bool rev = p.serpentine && diffusion && (y & 1);
        for (int xi = 0; xi < w; ++xi) {
            int x = rev ? (w - 1 - xi) : xi;
            size_t i = (size_t)y * w + x;
            if (!opaque[i]) continue;
            Vec3 work = clamp255(ws.to(rgb[i]) + err.get(x, y));
            Vec3 target = ws.from(work);
            Vec3 t2 = target;
            if (ordered) {
                float o = (orderedThreshold(p.dither, x, y) - 0.5f) * p.strength;
                for (int ch = 0; ch < 3; ++ch) t2[ch] = clamp255(t2[ch] + o * st[ch]);
            }
            RGBA8 s = space.snap(t2);
            out[i] = s;
            if (diffusion) err.spread(x, y, (work - ws.to(Vec3(s))) * p.strength, k, rev, opaque);
        }
    }
}

// ================================================================ YJK
void quantizeYJK(const std::vector<Vec3>& rgb, const std::vector<uint8_t>& opaque, int w, int h,
                 const QuantizeParams& p, std::vector<RGBA8>& out) {
    out.assign((size_t)w * h, RGBA8{});
    auto k = kernel(p.dither);
    bool diffusion = !k.empty() && p.strength > 0;
    bool ordered = isOrdered(p.dither) && p.strength > 0;
    ErrorBuffer err(w, h, diffusion);
    WorkSpace ws{ p.linearLight && diffusion };
    auto toRGB5 = [](int Y, int J, int K, int& r, int& g, int& b) {
        r = std::clamp(Y + J, 0, 31);
        g = std::clamp(Y + K, 0, 31);
        b = std::clamp((5 * Y - 2 * J - K) >> 2, 0, 31);
    };
    for (int y = 0; y < h; ++y) {
        if (p.cancel && p.cancel->load()) return;
        for (int bx = 0; bx < w; bx += 4) {
            int n = std::min(4, w - bx);
            Vec3 t[4], t5[4];
            double sj = 0, sk = 0; int cnt = 0;
            for (int i = 0; i < n; ++i) {
                size_t idx = (size_t)y * w + bx + i;
                t[i] = clamp255(ws.to(rgb[idx]) + err.get(bx + i, y));
                Vec3 tt = ws.from(t[i]);
                if (ordered) {
                    float o = (orderedThreshold(p.dither, bx + i, y) - 0.5f) * p.strength * (255.f / 31.f);
                    tt = clamp255(Vec3(tt.r + o, tt.g + o, tt.b + o));
                }
                t5[i] = tt * (31.f / 255.f);
                if (!opaque[idx]) continue;
                float Y = t5[i].b / 2.f + t5[i].r / 4.f + t5[i].g / 8.f;
                sj += t5[i].r - Y; sk += t5[i].g - Y; ++cnt;
            }
            if (cnt == 0) continue;
            int J0 = (int)std::lround(sj / cnt), K0 = (int)std::lround(sk / cnt);
            int bestJ = 0, bestK = 0, bestY[4] = { 0, 0, 0, 0 };
            float bestErr = 1e30f;
            for (int J = J0 - 2; J <= J0 + 2; ++J) {
                if (J < -32 || J > 31) continue;
                for (int K = K0 - 2; K <= K0 + 2; ++K) {
                    if (K < -32 || K > 31) continue;
                    float total = 0; int ys[4] = { 0, 0, 0, 0 };
                    for (int i = 0; i < n; ++i) {
                        if (!opaque[(size_t)y * w + bx + i]) continue;
                        float be = 1e30f;
                        for (int Y = 0; Y < 32; ++Y) {
                            int r, g, b; toRGB5(Y, J, K, r, g, b);
                            Vec3 c((float)r, (float)g, (float)b);
                            float d = distWeighted(c * (255.f / 31.f), t5[i] * (255.f / 31.f));
                            if (d < be) { be = d; ys[i] = Y; }
                        }
                        total += be;
                    }
                    if (total < bestErr) { bestErr = total; bestJ = J; bestK = K; std::copy(ys, ys + 4, bestY); }
                }
            }
            for (int i = 0; i < n; ++i) {
                size_t idx = (size_t)y * w + bx + i;
                if (!opaque[idx]) continue;
                int r, g, b; toRGB5(bestY[i], bestJ, bestK, r, g, b);
                RGBA8 c{ expandBits(r, 5), expandBits(g, 5), expandBits(b, 5), 255 };
                out[idx] = c;
                if (diffusion) err.spread(bx + i, y, (t[i] - ws.to(Vec3(c))) * p.strength, k, false, opaque);
            }
        }
    }
}

// ================================================================ タイル制約
namespace {

struct TilePixels { std::vector<Vec3> px; };

float tileErrorFor(const std::vector<Vec3>& px, const std::vector<Vec3>& pal, bool mixAware) {
    float total = 0;
    size_t stride = std::max<size_t>(1, px.size() / 64);
    for (size_t i = 0; i < px.size(); i += stride) {
        float bd = 1e30f;
        for (size_t a = 0; a < pal.size(); ++a) {
            bd = std::min(bd, distWeighted(px[i], pal[a]));
            if (mixAware) for (size_t b = a + 1; b < pal.size(); ++b) bd = std::min(bd, distWeighted(px[i], (pal[a] + pal[b]) * 0.5f) * 1.15f);
        }
        total += bd;
    }
    return total * (float)stride;
}

} // namespace

void solveTileConstraint(const std::vector<Vec3>& rgb, const std::vector<uint8_t>& opaque, int w, int h,
                         const ModeSpec& mode, bool includeBlack, bool mixAware, bool linearMix,
                         std::vector<RGBA8>& outPalette, TileAssign& T) {
    const TileConstraint& tc = mode.tile;
    T = TileAssign{};
    T.tileW = tc.tileW; T.tileH = tc.tileH;
    T.tilesX = (w + tc.tileW - 1) / tc.tileW;
    T.tilesY = (h + tc.tileH - 1) / tc.tileH;
    const int nTiles = T.tilesX * T.tilesY;
    T.subsetOfTile.assign(nTiles, 0);

    std::vector<TilePixels> tiles(nTiles);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            size_t i = (size_t)y * w + x;
            if (opaque[i]) tiles[(size_t)(y / tc.tileH) * T.tilesX + x / tc.tileW].px.push_back(clamp255(rgb[i]));
        }

    const std::vector<RGBA8>* fixed = mode.fixedPalette.empty() ? nullptr : &mode.fixedPalette;
    const int C = std::max(1, tc.colorsPerTile);

    // 全体ヒストグラム
    std::vector<Vec3> allPx;
    for (auto& t : tiles) allPx.insert(allPx.end(), t.px.begin(), t.px.end());
    auto histAll = buildHistogram(allPx, nullptr, mode.space);

    if (tc.subPalettes <= 0) {
        // ---- 属性方式: タイル毎にマスターパレットから C 色を選ぶ
        if (fixed && (mode.paletteColors <= 0 || mode.paletteColors >= (int)fixed->size())) outPalette = *fixed;
        else {
            PaletteGenOptions go; go.fixedSet = fixed; go.includeBlack = includeBlack;
            outPalette = generatePalette(histAll, mode.paletteColors > 0 ? mode.paletteColors : 16, mode.space, go);
        }
        const int P = (int)outPalette.size();
        std::vector<Vec3> palF;
        for (auto& c : outPalette) palF.push_back(Vec3(c));
        // 2 色間の中間色 (ディザ想定)
        std::map<std::vector<uint8_t>, int> subsetIds;
        for (int ti = 0; ti < nTiles; ++ti) {
            const auto& px = tiles[ti].px;
            std::vector<uint8_t> best;
            if (px.empty()) best = { 0 };
            else if (C == 2) {
                // 画素 x 色 の距離表
                std::vector<float> d(px.size() * P);
                for (size_t i = 0; i < px.size(); ++i) for (int c = 0; c < P; ++c) d[i * P + c] = distWeighted(px[i], palF[c]);
                float be = 1e30f; int ba = 0, bb = 0;
                for (int a = 0; a < P; ++a)
                    for (int b = a; b < P; ++b) {
                        float e = 0;
                        Vec3 m1 = mixColors(palF[a], palF[b], 0.25f, linearMix), m2 = mixColors(palF[a], palF[b], 0.5f, linearMix), m3 = mixColors(palF[a], palF[b], 0.75f, linearMix);
                        for (size_t i = 0; i < px.size(); ++i) {
                            float v = std::min(d[i * P + a], d[i * P + b]);
                            if (mixAware && a != b) {
                                v = std::min(v, distWeighted(px[i], m1) * 1.1f);
                                v = std::min(v, distWeighted(px[i], m2) * 1.1f);
                                v = std::min(v, distWeighted(px[i], m3) * 1.1f);
                            }
                            e += v;
                        }
                        if (e < be) { be = e; ba = a; bb = b; }
                    }
                best = { (uint8_t)ba };
                if (bb != ba) best.push_back((uint8_t)bb);
            } else {
                // 貪欲法
                std::vector<float> cur(px.size(), 1e30f);
                std::vector<bool> used(P, false);
                for (int n = 0; n < C; ++n) {
                    int bc = -1; double bg = -1;
                    for (int c = 0; c < P; ++c) {
                        if (used[c]) continue;
                        double g = 0;
                        for (size_t i = 0; i < px.size(); ++i) {
                            float dd = distWeighted(px[i], palF[c]);
                            if (dd < cur[i]) g += (std::min(cur[i], 1e12f) - dd);
                        }
                        if (g > bg) { bg = g; bc = c; }
                    }
                    if (bc < 0 || bg <= 0) break;
                    used[bc] = true; best.push_back((uint8_t)bc);
                    for (size_t i = 0; i < px.size(); ++i) cur[i] = std::min(cur[i], distWeighted(px[i], palF[bc]));
                }
                if (best.empty()) best = { 0 };
            }
            std::sort(best.begin(), best.end());
            auto it = subsetIds.find(best);
            int id;
            if (it == subsetIds.end()) { id = (int)T.subsets.size(); subsetIds[best] = id; T.subsets.push_back(best); }
            else id = it->second;
            T.subsetOfTile[ti] = id;
        }
        return;
    }

    // ---- サブパレット方式
    const int S = tc.subPalettes;
    bool shared = tc.sharedColor0;
    RGBA8 sharedColor{};
    if (shared) {
        // 最頻色を共有背景色にする
        float bw = -1; Vec3 bc(0, 0, 0);
        for (auto& s : histAll) if (s.w > bw) { bw = s.w; bc = s.c; }
        if (fixed) {
            int bi = 0; float bd = 1e30f;
            for (size_t i = 0; i < fixed->size(); ++i) { float d = distWeighted(bc, Vec3((*fixed)[i])); if (d < bd) { bd = d; bi = (int)i; } }
            sharedColor = (*fixed)[bi];
        } else sharedColor = mode.space.snap(bc);
    }

    auto makeGroupPalette = [&](const std::vector<int>& members) {
        std::vector<Vec3> px;
        for (int t : members) px.insert(px.end(), tiles[t].px.begin(), tiles[t].px.end());
        auto hist = buildHistogram(px, nullptr, mode.space);
        PaletteGenOptions go; go.fixedSet = fixed; go.includeBlack = includeBlack && !shared; go.kmeansIterations = 4; go.sortByLuma = true;
        int want = shared ? C - 1 : C;
        std::vector<RGBA8> pal;
        if (shared) pal.push_back(sharedColor);
        if (want > 0 && !hist.empty()) {
            auto g = generatePalette(hist, want + (shared ? 1 : 0), mode.space, go); // 共有色と重なる分を見込む
            for (auto& c : g) {
                if ((int)pal.size() >= C) break;
                if (std::find(pal.begin(), pal.end(), c) == pal.end()) pal.push_back(c);
            }
        }
        if (pal.empty()) pal.push_back(ric::rgb(0, 0, 0));
        return pal;
    };

    // 初期割り当て: タイル平均色の最遠点法 + k-means
    std::vector<int> nonEmpty;
    std::vector<Vec3> means(nTiles, Vec3(0, 0, 0));
    for (int t = 0; t < nTiles; ++t) {
        if (tiles[t].px.empty()) continue;
        Vec3 m(0, 0, 0);
        for (auto& c : tiles[t].px) m += c;
        means[t] = m * (1.f / tiles[t].px.size());
        nonEmpty.push_back(t);
    }
    std::vector<int> assign(nTiles, 0);
    int groups = std::max(1, std::min<int>(S, (int)nonEmpty.size()));
    if (!nonEmpty.empty()) {
        std::vector<Vec3> cent;
        cent.push_back(means[nonEmpty[0]]);
        while ((int)cent.size() < groups) {
            int far = nonEmpty[0]; float fd = -1;
            for (int t : nonEmpty) {
                float bd = 1e30f;
                for (auto& c : cent) bd = std::min(bd, distWeighted(means[t], c));
                if (bd > fd) { fd = bd; far = t; }
            }
            cent.push_back(means[far]);
        }
        for (int it = 0; it < 5; ++it) {
            std::vector<Vec3> sum(groups, Vec3(0, 0, 0)); std::vector<int> cnt(groups, 0);
            for (int t : nonEmpty) {
                int bg = 0; float bd = 1e30f;
                for (int g = 0; g < groups; ++g) { float d = distWeighted(means[t], cent[g]); if (d < bd) { bd = d; bg = g; } }
                assign[t] = bg; sum[bg] += means[t]; cnt[bg]++;
            }
            for (int g = 0; g < groups; ++g) if (cnt[g]) cent[g] = sum[g] * (1.f / cnt[g]);
        }
    }

    std::vector<std::vector<RGBA8>> gp(groups);
    bool mixGroups = mixAware && C <= 4;
    for (int iter = 0; iter < 4; ++iter) {
        std::vector<std::vector<int>> members(groups);
        for (int t : nonEmpty) members[assign[t]].push_back(t);
        // 空グループには誤差最大のタイルを入れる
        for (int g = 0; g < groups; ++g) {
            if (!members[g].empty() || iter == 0) continue;
            int worst = -1; float we = -1;
            for (int t : nonEmpty) {
                if (members[assign[t]].size() <= 1) continue;
                std::vector<Vec3> pf; for (auto& c : gp[assign[t]]) pf.push_back(Vec3(c));
                float e = tileErrorFor(tiles[t].px, pf, mixGroups);
                if (e > we) { we = e; worst = t; }
            }
            if (worst >= 0) {
                auto& old = members[assign[worst]];
                old.erase(std::find(old.begin(), old.end(), worst));
                assign[worst] = g; members[g].push_back(worst);
            }
        }
        for (int g = 0; g < groups; ++g) gp[g] = members[g].empty() ? (gp[g].empty() ? makeGroupPalette({}) : gp[g]) : makeGroupPalette(members[g]);
        if (iter == 3) break;
        // 再割り当て
        std::vector<std::vector<Vec3>> gpf(groups);
        for (int g = 0; g < groups; ++g) for (auto& c : gp[g]) gpf[g].push_back(Vec3(c));
        bool changed = false;
        for (int t : nonEmpty) {
            int bg = assign[t]; float bd = 1e30f;
            for (int g = 0; g < groups; ++g) {
                float e = tileErrorFor(tiles[t].px, gpf[g], mixGroups);
                if (e < bd) { bd = e; bg = g; }
            }
            if (bg != assign[t]) { assign[t] = bg; changed = true; }
        }
        if (!changed) break;
    }

    // マスターパレット: グループ毎に C 枠
    outPalette.clear();
    T.subsets.clear();
    for (int g = 0; g < groups; ++g) {
        std::vector<uint8_t> sub;
        for (int k = 0; k < C; ++k) {
            if (k < (int)gp[g].size()) sub.push_back((uint8_t)outPalette.size());
            outPalette.push_back(k < (int)gp[g].size() ? gp[g][k] : gp[g][0]);
        }
        T.subsets.push_back(sub);
    }
    for (int t = 0; t < nTiles; ++t) T.subsetOfTile[t] = assign[t];
}

} // namespace ric
