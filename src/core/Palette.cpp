// SPDX-License-Identifier: MIT
#include "Palette.h"
#include <unordered_map>
#include <algorithm>
#include <numeric>
#include <cmath>

namespace ric {

std::vector<ColorSample> buildHistogram(const std::vector<Vec3>& pixels, const std::vector<float>* weights, const ColorSpace& space) {
    // 色空間が小さい場合は丸めた色、それ以外は RGB666 のバケツで平均
    bool useSnap = space.kind == SpaceKind::Bits && (space.rBits + space.gBits + space.bBits) <= 18;
    struct Acc { double r = 0, g = 0, b = 0, w = 0; };
    std::unordered_map<uint32_t, Acc> map;
    map.reserve(4096);
    for (size_t i = 0; i < pixels.size(); ++i) {
        float w = weights ? (*weights)[i] : 1.f;
        if (w <= 0.f) continue;
        Vec3 c = clamp255(pixels[i]);
        uint32_t key;
        if (useSnap) {
            RGBA8 s = space.snap(c);
            key = ((uint32_t)s.r << 16) | ((uint32_t)s.g << 8) | s.b;
        } else {
            key = ((uint32_t)((int)c.r >> 2) << 12) | ((uint32_t)((int)c.g >> 2) << 6) | (uint32_t)((int)c.b >> 2);
        }
        auto& a = map[key];
        a.r += c.r * w; a.g += c.g * w; a.b += c.b * w; a.w += w;
    }
    std::vector<ColorSample> out;
    out.reserve(map.size());
    for (auto& kv : map) {
        const Acc& a = kv.second;
        if (a.w <= 0) continue;
        out.push_back({ Vec3((float)(a.r / a.w), (float)(a.g / a.w), (float)(a.b / a.w)), (float)a.w });
    }
    // 決定的な結果にするためソート
    std::sort(out.begin(), out.end(), [](const ColorSample& x, const ColorSample& y) {
        if (x.c.r != y.c.r) return x.c.r < y.c.r;
        if (x.c.g != y.c.g) return x.c.g < y.c.g;
        return x.c.b < y.c.b;
    });
    return out;
}

namespace {

const float kChW[3] = { 1.0f, 1.2f, 0.8f }; // 分割時のチャンネル重み

struct Box {
    size_t begin, end;
    double score;
    int axis;
};

void evalBox(const std::vector<ColorSample>& s, Box& b) {
    double sw = 0, m[3] = { 0, 0, 0 }, m2[3] = { 0, 0, 0 };
    for (size_t i = b.begin; i < b.end; ++i) {
        double w = s[i].w;
        sw += w;
        for (int ch = 0; ch < 3; ++ch) { double v = s[i].c[ch]; m[ch] += v * w; m2[ch] += v * v * w; }
    }
    b.score = 0; b.axis = 0;
    if (b.end - b.begin < 2 || sw <= 0) { b.score = -1; return; }
    double best = -1;
    for (int ch = 0; ch < 3; ++ch) {
        double mean = m[ch] / sw;
        double var = std::max(0.0, m2[ch] / sw - mean * mean) * kChW[ch];
        if (var > best) { best = var; b.axis = ch; }
    }
    b.score = best * sw;
}

std::vector<Vec3> medianCut(std::vector<ColorSample> s, int n) {
    std::vector<Box> boxes;
    Box root{ 0, s.size(), 0, 0 };
    evalBox(s, root);
    boxes.push_back(root);
    while ((int)boxes.size() < n) {
        int bi = -1;
        double bs = 0;
        for (size_t i = 0; i < boxes.size(); ++i) if (boxes[i].score > bs) { bs = boxes[i].score; bi = (int)i; }
        if (bi < 0) break;
        Box b = boxes[bi];
        int ax = b.axis;
        std::sort(s.begin() + b.begin, s.begin() + b.end, [ax](const ColorSample& x, const ColorSample& y) { return x.c[ax] < y.c[ax]; });
        double total = 0;
        for (size_t i = b.begin; i < b.end; ++i) total += s[i].w;
        double acc = 0;
        size_t mid = b.begin + 1;
        for (size_t i = b.begin; i < b.end - 1; ++i) {
            acc += s[i].w;
            if (acc >= total * 0.5) { mid = i + 1; break; }
            mid = i + 1;
        }
        mid = std::clamp(mid, b.begin + 1, b.end - 1);
        Box a{ b.begin, mid, 0, 0 }, c{ mid, b.end, 0, 0 };
        evalBox(s, a); evalBox(s, c);
        boxes[bi] = a;
        boxes.push_back(c);
    }
    std::vector<Vec3> centers;
    for (auto& b : boxes) {
        double sw = 0; Vec3 m(0, 0, 0);
        for (size_t i = b.begin; i < b.end; ++i) { m += s[i].c * s[i].w; sw += s[i].w; }
        if (sw > 0) centers.push_back(m * (float)(1.0 / sw));
    }
    return centers;
}

void kmeans(const std::vector<ColorSample>& s, std::vector<Vec3>& centers, int iters) {
    if (centers.empty() || s.empty()) return;
    // コスト上限: サンプル数 x 色数
    size_t stride = 1;
    const double budget = 2.0e7;
    while ((double)(s.size() / stride) * centers.size() > budget) stride *= 2;
    for (int it = 0; it < iters; ++it) {
        std::vector<Vec3> sum(centers.size(), Vec3(0, 0, 0));
        std::vector<double> wsum(centers.size(), 0.0);
        for (size_t i = 0; i < s.size(); i += stride) {
            int best = 0; float bd = 1e30f;
            for (size_t k = 0; k < centers.size(); ++k) {
                float d = distWeighted(s[i].c, centers[k]);
                if (d < bd) { bd = d; best = (int)k; }
            }
            sum[best] += s[i].c * s[i].w;
            wsum[best] += s[i].w;
        }
        bool moved = false;
        for (size_t k = 0; k < centers.size(); ++k) {
            if (wsum[k] > 0) {
                Vec3 nc = sum[k] * (float)(1.0 / wsum[k]);
                if (distWeighted(nc, centers[k]) > 0.25f) moved = true;
                centers[k] = nc;
            }
        }
        if (!moved) break;
    }
}

int nearestIn(const std::vector<RGBA8>& set, const Vec3& c) {
    int best = 0; float bd = 1e30f;
    for (size_t i = 0; i < set.size(); ++i) {
        float d = distWeighted(c, Vec3(set[i]));
        if (d < bd) { bd = d; best = (int)i; }
    }
    return best;
}

} // namespace

std::vector<RGBA8> generatePalette(const std::vector<ColorSample>& hist, int n, const ColorSpace& space, const PaletteGenOptions& opt) {
    std::vector<RGBA8> pal;
    if (n <= 0) return pal;
    const auto* fixed = opt.fixedSet;
    if (fixed && (int)fixed->size() <= n) return *fixed;

    std::vector<Vec3> centers;
    if (!hist.empty()) {
        centers = medianCut(hist, n);
        kmeans(hist, centers, opt.kmeansIterations);
    }

    if (fixed) {
        // 固定色集合から選ぶ
        std::vector<int> chosen;
        std::vector<bool> used(fixed->size(), false);
        for (auto& c : centers) {
            int i = nearestIn(*fixed, c);
            if (!used[i]) { used[i] = true; chosen.push_back(i); }
        }
        // 足りなければ誤差が最も減る色を追加
        if ((int)chosen.size() < n && !hist.empty()) {
            size_t stride = std::max<size_t>(1, hist.size() / 20000);
            std::vector<float> cur;
            for (size_t i = 0; i < hist.size(); i += stride) {
                float bd = 1e30f;
                for (int k : chosen) bd = std::min(bd, distWeighted(hist[i].c, Vec3((*fixed)[k])));
                cur.push_back(bd);
            }
            while ((int)chosen.size() < n) {
                int bestK = -1; double bestGain = 0;
                for (size_t k = 0; k < fixed->size(); ++k) {
                    if (used[k]) continue;
                    double gain = 0; size_t j = 0;
                    for (size_t i = 0; i < hist.size(); i += stride, ++j) {
                        float d = distWeighted(hist[i].c, Vec3((*fixed)[k]));
                        if (d < cur[j]) gain += (cur[j] - d) * hist[i].w;
                    }
                    if (gain > bestGain) { bestGain = gain; bestK = (int)k; }
                }
                if (bestK < 0) break;
                used[bestK] = true; chosen.push_back(bestK);
                size_t j = 0;
                for (size_t i = 0; i < hist.size(); i += stride, ++j)
                    cur[j] = std::min(cur[j], distWeighted(hist[i].c, Vec3((*fixed)[bestK])));
            }
        }
        if (opt.includeBlack) {
            int bk = nearestIn(*fixed, Vec3(0, 0, 0));
            if (!used[bk]) {
                if ((int)chosen.size() >= n && !chosen.empty()) {
                    // 最も暗い色と置き換え
                    size_t di = 0;
                    for (size_t i = 1; i < chosen.size(); ++i)
                        if (luma(Vec3((*fixed)[chosen[i]])) < luma(Vec3((*fixed)[chosen[di]]))) di = i;
                    chosen[di] = bk;
                } else chosen.push_back(bk);
            }
        }
        std::sort(chosen.begin(), chosen.end()); // 固定パレットの順序を保つ
        for (int i : chosen) pal.push_back((*fixed)[i]);
        if (pal.empty()) pal.push_back((*fixed)[0]);
        return pal;
    }

    for (auto& c : centers) {
        RGBA8 s = space.snap(c);
        if (std::find(pal.begin(), pal.end(), s) == pal.end()) pal.push_back(s);
    }
    if (opt.includeBlack) {
        RGBA8 black = rgb(0, 0, 0);
        if (std::find(pal.begin(), pal.end(), black) == pal.end()) {
            if ((int)pal.size() >= n && !pal.empty()) {
                size_t di = 0;
                for (size_t i = 1; i < pal.size(); ++i) if (luma(Vec3(pal[i])) < luma(Vec3(pal[di]))) di = i;
                pal[di] = black;
            } else pal.push_back(black);
        }
    }
    if (pal.empty()) pal.push_back(rgb(0, 0, 0));
    if (opt.sortByLuma) {
        std::stable_sort(pal.begin(), pal.end(), [](const RGBA8& a, const RGBA8& b) { return luma(Vec3(a)) < luma(Vec3(b)); });
    }
    return pal;
}

} // namespace ric
