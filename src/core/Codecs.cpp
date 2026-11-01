// SPDX-License-Identifier: MIT
#include "Codecs.h"
#include "Palette.h"
#include "Color.h"
#include <algorithm>
#include <cstring>
#include <queue>
#include <cmath>
#include <unordered_map>

namespace ric {

namespace {
uint16_t le16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
uint32_t le32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
void put16(std::vector<uint8_t>& v, uint16_t x) { v.push_back((uint8_t)x); v.push_back((uint8_t)(x >> 8)); }
void put32(std::vector<uint8_t>& v, uint32_t x) { for (int i = 0; i < 4; ++i) v.push_back((uint8_t)(x >> (8 * i))); }
int ctz32(uint32_t m) { int n = 0; if (!m) return 0; while (!(m & 1)) { m >>= 1; ++n; } return n; }
int popc32(uint32_t m) { int n = 0; while (m) { n += m & 1; m >>= 1; } return n; }
} // namespace

// ================================================================ BMP
bool decodeBMP(const std::vector<uint8_t>& d, Image& out, std::string* error) {
    auto fail = [&](const char* m) { if (error) *error = m; return false; };
    if (d.size() < 26 || d[0] != 'B' || d[1] != 'M') return fail("BMP ではありません");
    uint32_t dataOff = le32(&d[10]);
    uint32_t hs = le32(&d[14]);
    int32_t w, h; int bpp; uint32_t comp = 0, used = 0;
    size_t palEntry = 4;
    if (hs == 12) {
        w = le16(&d[18]); h = (int16_t)le16(&d[20]); bpp = le16(&d[24]); palEntry = 3;
    } else {
        if (d.size() < 54) return fail("BMP ヘッダが不完全です");
        w = (int32_t)le32(&d[18]); h = (int32_t)le32(&d[22]); bpp = le16(&d[28]); comp = le32(&d[30]); used = le32(&d[46]);
    }
    bool topDown = h < 0;
    if (h < 0) h = -h;
    if (w <= 0 || h <= 0 || w > 32768 || h > 32768) return fail("BMP のサイズが不正です");
    if (comp != 0 && comp != 3 && comp != 6) return fail("圧縮 BMP には対応していません");
    uint32_t mR = 0, mG = 0, mB = 0, mA = 0;
    if (bpp == 16 || bpp == 32) {
        if (comp == 3 || comp == 6) {
            size_t mo = 14 + 40;
            if (hs >= 52) mo = 14 + 40; // V4/V5 はヘッダ内
            if (d.size() < mo + 12) return fail("BMP ビットフィールドが不完全です");
            mR = le32(&d[mo]); mG = le32(&d[mo + 4]); mB = le32(&d[mo + 8]);
            if (hs >= 56 && d.size() >= mo + 16) mA = le32(&d[mo + 12]);
            else if (comp == 6 && d.size() >= mo + 16) mA = le32(&d[mo + 12]);
        } else if (bpp == 16) { mR = 0x7C00; mG = 0x03E0; mB = 0x001F; }
        else { mR = 0x00FF0000; mG = 0x0000FF00; mB = 0x000000FF; mA = 0xFF000000; }
    }
    std::vector<RGBA8> pal;
    if (bpp <= 8) {
        size_t n = used ? used : (1u << bpp);
        size_t po = 14 + hs;
        for (size_t i = 0; i < n && po + i * palEntry + 2 < d.size(); ++i) {
            const uint8_t* p = &d[po + i * palEntry];
            pal.push_back(rgb(p[2], p[1], p[0]));
        }
        if (pal.empty()) return fail("BMP パレットがありません");
    }
    size_t stride = ((size_t)w * bpp + 31) / 32 * 4;
    if (dataOff + stride * h > d.size()) return fail("BMP データが不足しています");
    out = Image(w, h);
    bool anyAlpha = false;
    auto ext = [](uint32_t v, uint32_t m) -> uint8_t {
        if (!m) return 0;
        int s = ctz32(m), b = popc32(m);
        uint32_t x = (v & m) >> s;
        uint32_t mx = (1u << b) - 1;
        return (uint8_t)((x * 255 + mx / 2) / mx);
    };
    for (int y = 0; y < h; ++y) {
        const uint8_t* row = &d[dataOff + stride * (topDown ? y : (h - 1 - y))];
        for (int x = 0; x < w; ++x) {
            RGBA8 c{};
            switch (bpp) {
            case 1: c = pal[std::min<size_t>(pal.size() - 1, (row[x >> 3] >> (7 - (x & 7))) & 1)]; break;
            case 4: c = pal[std::min<size_t>(pal.size() - 1, (row[x >> 1] >> ((x & 1) ? 0 : 4)) & 15)]; break;
            case 8: c = pal[std::min<size_t>(pal.size() - 1, row[x])]; break;
            case 24: c = rgb(row[x * 3 + 2], row[x * 3 + 1], row[x * 3]); break;
            case 16: { uint32_t v = le16(row + x * 2); c = RGBA8{ ext(v, mR), ext(v, mG), ext(v, mB), (uint8_t)(mA ? ext(v, mA) : 255) }; break; }
            case 32: { uint32_t v = le32(row + x * 4); c = RGBA8{ ext(v, mR), ext(v, mG), ext(v, mB), (uint8_t)(mA ? ext(v, mA) : 255) }; break; }
            default: return fail("未対応の BMP ビット深度です");
            }
            if (c.a != 0) anyAlpha = true;
            out.at(x, y) = c;
        }
    }
    if (bpp == 32 && mA && !anyAlpha) for (auto& p : out.pixels) p.a = 255; // アルファ全 0 は不透明扱い
    return true;
}

void encodeBMP(const Image& img, bool withAlpha, std::vector<uint8_t>& out) {
    int bpp = withAlpha ? 32 : 24;
    size_t stride = ((size_t)img.width * bpp + 31) / 32 * 4;
    uint32_t hdr = withAlpha ? 108 : 40;
    uint32_t off = 14 + hdr;
    out.clear();
    out.push_back('B'); out.push_back('M');
    put32(out, (uint32_t)(off + stride * img.height));
    put32(out, 0);
    put32(out, off);
    put32(out, hdr);
    put32(out, (uint32_t)img.width);
    put32(out, (uint32_t)img.height);
    put16(out, 1);
    put16(out, (uint16_t)bpp);
    put32(out, withAlpha ? 3 : 0);
    put32(out, (uint32_t)(stride * img.height));
    put32(out, 2835); put32(out, 2835); put32(out, 0); put32(out, 0);
    if (withAlpha) {
        put32(out, 0x00FF0000); put32(out, 0x0000FF00); put32(out, 0x000000FF); put32(out, 0xFF000000);
        put32(out, 0x73524742); // 'sRGB'
        for (int i = 0; i < 12; ++i) put32(out, 0);
    }
    std::vector<uint8_t> row(stride, 0);
    for (int y = img.height - 1; y >= 0; --y) {
        std::fill(row.begin(), row.end(), 0);
        for (int x = 0; x < img.width; ++x) {
            const RGBA8& c = img.at(x, y);
            if (withAlpha) { row[x * 4] = c.b; row[x * 4 + 1] = c.g; row[x * 4 + 2] = c.r; row[x * 4 + 3] = c.a; }
            else { row[x * 3] = c.b; row[x * 3 + 1] = c.g; row[x * 3 + 2] = c.r; }
        }
        out.insert(out.end(), row.begin(), row.end());
    }
}

// ================================================================ GIF
IndexedImage makeIndexed256(const Image& img) {
    IndexedImage r;
    r.width = img.width; r.height = img.height; r.pixelAspect = img.pixelAspect;
    r.indices.assign(img.pixels.size(), 0);
    bool anyTrans = false;
    std::unordered_map<uint32_t, int> map;
    for (auto& p : img.pixels) {
        if (p.a < 128) { anyTrans = true; continue; }
        uint32_t k = ((uint32_t)p.r << 16) | ((uint32_t)p.g << 8) | p.b;
        if (!map.count(k)) {
            int id = (int)map.size();
            map[k] = id;
            if (map.size() > 256) break;
        }
    }
    if (map.size() <= 256) {
        r.palette.resize(map.size());
        for (auto& kv : map) r.palette[kv.second] = rgb((kv.first >> 16) & 255, (kv.first >> 8) & 255, kv.first & 255);
        for (size_t i = 0; i < img.pixels.size(); ++i) {
            auto& p = img.pixels[i];
            if (p.a < 128) continue;
            r.indices[i] = (uint8_t)map[((uint32_t)p.r << 16) | ((uint32_t)p.g << 8) | p.b];
        }
    } else {
        std::vector<Vec3> px;
        for (auto& p : img.pixels) if (p.a >= 128) px.push_back(Vec3(p));
        auto hist = buildHistogram(px, nullptr, ColorSpace::full());
        PaletteGenOptions go;
        r.palette = generatePalette(hist, anyTrans ? 255 : 256, ColorSpace::full(), go);
        std::unordered_map<uint32_t, uint8_t> cache;
        for (size_t i = 0; i < img.pixels.size(); ++i) {
            auto& p = img.pixels[i];
            if (p.a < 128) continue;
            uint32_t k = ((uint32_t)p.r << 16) | ((uint32_t)p.g << 8) | p.b;
            auto it = cache.find(k);
            if (it != cache.end()) { r.indices[i] = it->second; continue; }
            int best = 0; float bd = 1e30f;
            for (size_t j = 0; j < r.palette.size(); ++j) {
                float dd = distWeighted(Vec3(p), Vec3(r.palette[j]));
                if (dd < bd) { bd = dd; best = (int)j; }
            }
            cache[k] = (uint8_t)best;
            r.indices[i] = (uint8_t)best;
        }
    }
    if (r.palette.empty()) r.palette.push_back(rgb(0, 0, 0));
    if (anyTrans) {
        r.alpha.assign(img.pixels.size(), 255);
        for (size_t i = 0; i < img.pixels.size(); ++i) if (img.pixels[i].a < 128) r.alpha[i] = 0;
    }
    return r;
}

namespace {
struct LSBWriter {
    std::vector<uint8_t>& buf;
    uint64_t acc = 0;
    int n = 0;
    explicit LSBWriter(std::vector<uint8_t>& b) : buf(b) {}
    void put(uint32_t v, int bits) {
        acc |= (uint64_t)v << n;
        n += bits;
        while (n >= 8) { buf.push_back((uint8_t)acc); acc >>= 8; n -= 8; }
    }
    void flush() { if (n > 0) { buf.push_back((uint8_t)acc); acc = 0; n = 0; } }
};
} // namespace

void encodeGIF(const IndexedImage& src, std::vector<uint8_t>& out) {
    IndexedImage img = src;
    int trans = -1;
    bool anyTrans = false;
    for (auto a : img.alpha) if (a < 128) { anyTrans = true; break; }
    if (anyTrans) {
        if (img.palette.size() < 256) { trans = (int)img.palette.size(); img.palette.push_back(rgb(0, 0, 0)); }
        else {
            std::vector<int> cnt(256, 0);
            for (size_t i = 0; i < img.indices.size(); ++i) if (img.alpha[i] >= 128) cnt[img.indices[i]]++;
            trans = (int)(std::min_element(cnt.begin(), cnt.end()) - cnt.begin());
        }
        for (size_t i = 0; i < img.indices.size(); ++i) if (img.alpha[i] < 128) img.indices[i] = (uint8_t)trans;
    }
    int bits = 1;
    while ((1u << bits) < img.palette.size()) ++bits;
    out.clear();
    const char* sig = "GIF89a";
    out.insert(out.end(), sig, sig + 6);
    put16(out, (uint16_t)img.width);
    put16(out, (uint16_t)img.height);
    out.push_back((uint8_t)(0x80 | ((bits - 1) << 4) | (bits - 1)));
    out.push_back(0); // 背景色
    // ドット比 (縦長ドットの場合)
    uint8_t aspect = 0;
    if (std::abs(img.pixelAspect - 1.0) > 0.01) {
        int v = (int)std::lround(img.pixelAspect * 64.0 - 15.0);
        if (v >= 1 && v <= 255) aspect = (uint8_t)v;
    }
    out.push_back(aspect);
    for (int i = 0; i < (1 << bits); ++i) {
        RGBA8 c = i < (int)img.palette.size() ? img.palette[i] : rgb(0, 0, 0);
        out.push_back(c.r); out.push_back(c.g); out.push_back(c.b);
    }
    if (trans >= 0) {
        const uint8_t gce[] = { 0x21, 0xF9, 0x04, 0x01, 0x00, 0x00, (uint8_t)trans, 0x00 };
        out.insert(out.end(), gce, gce + 8);
    }
    out.push_back(0x2C);
    put16(out, 0); put16(out, 0);
    put16(out, (uint16_t)img.width); put16(out, (uint16_t)img.height);
    out.push_back(0);
    const int minCode = std::max(2, bits);
    out.push_back((uint8_t)minCode);

    std::vector<uint8_t> lzw;
    LSBWriter bw(lzw);
    const int clearCode = 1 << minCode;
    int codeSize = minCode + 1;
    int maxCode = clearCode + 1;
    std::unordered_map<uint32_t, int> dict;
    dict.reserve(8192);
    bw.put(clearCode, codeSize);
    const size_t n = img.indices.size();
    if (n > 0) {
        int cur = img.indices[0];
        for (size_t i = 1; i < n; ++i) {
            int k = img.indices[i];
            uint32_t key = ((uint32_t)cur << 8) | (uint32_t)k;
            auto it = dict.find(key);
            if (it != dict.end()) { cur = it->second; continue; }
            bw.put(cur, codeSize);
            dict[key] = ++maxCode;
            if (maxCode >= (1 << codeSize)) ++codeSize;
            if (maxCode == 4095) {
                bw.put(clearCode, codeSize);
                dict.clear();
                codeSize = minCode + 1;
                maxCode = clearCode + 1;
            }
            cur = k;
        }
        bw.put(cur, codeSize);
    }
    bw.put(clearCode + 1, codeSize);
    bw.flush();
    for (size_t p = 0; p < lzw.size(); p += 255) {
        size_t len = std::min<size_t>(255, lzw.size() - p);
        out.push_back((uint8_t)len);
        out.insert(out.end(), lzw.begin() + p, lzw.begin() + p + len);
    }
    out.push_back(0);
    out.push_back(0x3B);
}

// ================================================================ WebP (VP8L lossless)
namespace {

// 長さ制限付きハフマン符号長
std::vector<int> huffLengths(std::vector<uint32_t> freq, int maxLen) {
    const int n = (int)freq.size();
    std::vector<int> len(n, 0);
    // 2 シンボル以上を保証
    int used = 0;
    for (auto f : freq) if (f) ++used;
    for (int i = 0; used < 2 && i < n; ++i) if (!freq[i]) { freq[i] = 1; ++used; }
    for (;;) {
        struct Node { uint64_t f; int l, r; int sym; };
        std::vector<Node> nodes;
        using QE = std::pair<uint64_t, int>;
        std::priority_queue<QE, std::vector<QE>, std::greater<QE>> pq;
        for (int i = 0; i < n; ++i) if (freq[i]) { nodes.push_back({ freq[i], -1, -1, i }); pq.push({ freq[i], (int)nodes.size() - 1 }); }
        while (pq.size() > 1) {
            auto a = pq.top(); pq.pop();
            auto b = pq.top(); pq.pop();
            nodes.push_back({ a.first + b.first, a.second, b.second, -1 });
            pq.push({ a.first + b.first, (int)nodes.size() - 1 });
        }
        std::fill(len.begin(), len.end(), 0);
        int maxD = 0;
        std::vector<std::pair<int, int>> st{ { pq.top().second, 0 } };
        while (!st.empty()) {
            auto [ni, dpt] = st.back(); st.pop_back();
            const Node& nd = nodes[ni];
            if (nd.sym >= 0) { len[nd.sym] = std::max(1, dpt); maxD = std::max(maxD, len[nd.sym]); }
            else { st.push_back({ nd.l, dpt + 1 }); st.push_back({ nd.r, dpt + 1 }); }
        }
        if (maxD <= maxLen) break;
        for (auto& f : freq) if (f) f = (f + 1) / 2;
    }
    return len;
}

// 正準符号 (LSB 先頭で書くためビット反転済み)
std::vector<uint32_t> canonicalCodes(const std::vector<int>& len) {
    int maxL = 0;
    for (int l : len) maxL = std::max(maxL, l);
    std::vector<int> count(maxL + 1, 0);
    for (int l : len) if (l) count[l]++;
    std::vector<uint32_t> next(maxL + 2, 0);
    uint32_t code = 0;
    for (int b = 1; b <= maxL; ++b) { code = (code + count[b - 1]) << 1; next[b] = code; }
    std::vector<uint32_t> codes(len.size(), 0);
    for (size_t s = 0; s < len.size(); ++s) {
        int l = len[s];
        if (!l) continue;
        uint32_t c = next[l]++;
        uint32_t r = 0;
        for (int i = 0; i < l; ++i) if (c & (1u << i)) r |= 1u << (l - 1 - i);
        codes[s] = r;
    }
    return codes;
}

struct HuffCode {
    std::vector<int> len;
    std::vector<uint32_t> code;
    void build(const std::vector<uint32_t>& freq, int maxLen) { len = huffLengths(freq, maxLen); code = canonicalCodes(len); }
    void write(LSBWriter& bw, int sym) const { bw.put(code[sym], len[sym]); }
};

void writeHuffman(LSBWriter& bw, const HuffCode& hc) {
    static const int kOrder[19] = { 17, 18, 0, 1, 2, 3, 4, 5, 16, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
    // 符号長列をトークン化 (17/18 でゼロ連続)
    struct Tok { int sym, extra, nbits; };
    std::vector<Tok> toks;
    const auto& L = hc.len;
    for (size_t i = 0; i < L.size();) {
        if (L[i] == 0) {
            size_t j = i;
            while (j < L.size() && L[j] == 0) ++j;
            size_t run = j - i;
            while (run >= 11) { size_t r = std::min<size_t>(run, 138); toks.push_back({ 18, (int)(r - 11), 7 }); run -= r; }
            while (run >= 3) { size_t r = std::min<size_t>(run, 10); toks.push_back({ 17, (int)(r - 3), 3 }); run -= r; }
            while (run > 0) { toks.push_back({ 0, 0, 0 }); --run; }
            i = j;
        } else { toks.push_back({ L[i], 0, 0 }); ++i; }
    }
    std::vector<uint32_t> f(19, 0);
    for (auto& t : toks) f[t.sym]++;
    HuffCode clc;
    clc.build(f, 7);
    int num = 4;
    for (int i = 0; i < 19; ++i) if (clc.len[kOrder[i]]) num = std::max(num, i + 1);
    bw.put(0, 1);            // 通常符号
    bw.put(num - 4, 4);
    for (int i = 0; i < num; ++i) bw.put(clc.len[kOrder[i]], 3);
    bw.put(0, 1);            // max_symbol なし
    for (auto& t : toks) {
        clc.write(bw, t.sym);
        if (t.nbits) bw.put(t.extra, t.nbits);
    }
}

void prefixEncode(uint32_t v, int& code, int& nbits, uint32_t& extra) {
    // v >= 1
    uint32_t d = v - 1;
    if (d < 4) { code = (int)d; nbits = 0; extra = 0; return; }
    int hb = 31;
    while (!(d >> hb)) --hb;
    int second = (d >> (hb - 1)) & 1;
    nbits = hb - 1;
    extra = d & ((1u << nbits) - 1);
    code = 2 * hb + second;
}

} // namespace

void encodeWebPLossless(const Image& img, std::vector<uint8_t>& out) {
    const int w = std::clamp(img.width, 1, 16384), h = std::clamp(img.height, 1, 16384);
    const size_t n = (size_t)w * h;
    std::vector<uint32_t> argb(n);
    bool alpha = false;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            RGBA8 c = (x < img.width && y < img.height) ? img.at(x, y) : RGBA8{};
            if (c.a != 255) alpha = true;
            uint32_t r = (uint8_t)(c.r - c.g), b = (uint8_t)(c.b - c.g); // subtract green
            argb[(size_t)y * w + x] = ((uint32_t)c.a << 24) | (r << 16) | ((uint32_t)c.g << 8) | b;
        }

    // LZ77 (左 / 上 / ハッシュ候補)
    struct Tok { uint32_t argb; uint32_t len; uint32_t dist; };
    std::vector<Tok> toks;
    const uint32_t kMaxLen = 4096;
    const size_t kWindow = (1u << 20) - 120;
    const int kHashBits = 16;
    std::vector<int64_t> head((size_t)1 << kHashBits, -1);
    auto hashAt = [&](size_t i) -> uint32_t {
        uint64_t v = (uint64_t)argb[i] * 0x9E3779B1u ^ ((i + 1 < n ? (uint64_t)argb[i + 1] : 0) * 0x85EBCA77u);
        return (uint32_t)(v >> 7) & ((1u << kHashBits) - 1);
    };
    auto matchLen = [&](size_t i, size_t d) -> uint32_t {
        if (d == 0 || d > i) return 0;
        uint32_t l = 0;
        size_t maxl = std::min<size_t>(kMaxLen, n - i);
        while (l < maxl && argb[i + l] == argb[i + l - d]) ++l;
        return l;
    };
    for (size_t i = 0; i < n;) {
        uint32_t bestL = 0; size_t bestD = 0;
        size_t cands[3] = { 1, (size_t)w, 0 };
        if (i + 1 < n) { int64_t hpos = head[hashAt(i)]; if (hpos >= 0) cands[2] = i - (size_t)hpos; }
        for (size_t d : cands) {
            if (d == 0 || d > kWindow) continue;
            uint32_t l = matchLen(i, d);
            if (l > bestL) { bestL = l; bestD = d; }
        }
        if (bestL >= 3) {
            toks.push_back({ 0, bestL, (uint32_t)bestD });
            for (uint32_t k = 0; k < bestL; ++k) if (i + k + 1 < n) head[hashAt(i + k)] = (int64_t)(i + k);
            i += bestL;
        } else {
            toks.push_back({ argb[i], 0, 0 });
            if (i + 1 < n) head[hashAt(i)] = (int64_t)i;
            ++i;
        }
    }

    // 頻度
    std::vector<uint32_t> fG(256 + 24, 0), fR(256, 0), fB(256, 0), fA(256, 0), fD(40, 0);
    struct Enc { int lc, ln; uint32_t le; int dc, dn; uint32_t de; };
    std::vector<Enc> enc(toks.size());
    for (size_t t = 0; t < toks.size(); ++t) {
        auto& k = toks[t];
        if (k.len == 0) {
            fG[(k.argb >> 8) & 255]++; fR[(k.argb >> 16) & 255]++; fB[k.argb & 255]++; fA[k.argb >> 24]++;
        } else {
            Enc& e = enc[t];
            prefixEncode(k.len, e.lc, e.ln, e.le);
            uint32_t dcode = (k.dist == (uint32_t)w) ? 1u : (k.dist == 1 ? 2u : k.dist + 120);
            prefixEncode(dcode, e.dc, e.dn, e.de);
            fG[256 + e.lc]++; fD[e.dc]++;
        }
    }
    HuffCode hG, hR, hB, hA, hD;
    hG.build(fG, 15); hR.build(fR, 15); hB.build(fB, 15); hA.build(fA, 15); hD.build(fD, 15);

    std::vector<uint8_t> bits;
    LSBWriter bw(bits);
    bw.put(0x2f, 8);
    bw.put(w - 1, 14);
    bw.put(h - 1, 14);
    bw.put(alpha ? 1 : 0, 1);
    bw.put(0, 3);
    bw.put(1, 1); bw.put(2, 2);   // SUBTRACT_GREEN
    bw.put(0, 1);                 // 変換終わり
    bw.put(0, 1);                 // カラーキャッシュなし
    bw.put(0, 1);                 // メタハフマンなし
    writeHuffman(bw, hG); writeHuffman(bw, hR); writeHuffman(bw, hB); writeHuffman(bw, hA); writeHuffman(bw, hD);
    for (size_t t = 0; t < toks.size(); ++t) {
        auto& k = toks[t];
        if (k.len == 0) {
            hG.write(bw, (k.argb >> 8) & 255);
            hR.write(bw, (k.argb >> 16) & 255);
            hB.write(bw, k.argb & 255);
            hA.write(bw, k.argb >> 24);
        } else {
            const Enc& e = enc[t];
            hG.write(bw, 256 + e.lc);
            if (e.ln) bw.put(e.le, e.ln);
            hD.write(bw, e.dc);
            if (e.dn) bw.put(e.de, e.dn);
        }
    }
    bw.flush();

    out.clear();
    uint32_t chunk = (uint32_t)bits.size();
    uint32_t riff = 4 + 8 + chunk + (chunk & 1);
    out.insert(out.end(), { 'R', 'I', 'F', 'F' });
    put32(out, riff);
    out.insert(out.end(), { 'W', 'E', 'B', 'P', 'V', 'P', '8', 'L' });
    put32(out, chunk);
    out.insert(out.end(), bits.begin(), bits.end());
    if (chunk & 1) out.push_back(0);
}

} // namespace ric
