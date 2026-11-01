// SPDX-License-Identifier: MIT
#include "MagFormat.h"
#include <cstring>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace ric {

namespace {

const char kMagic[] = "MAKI02  ";

// フラグ 0..15 の相対位置 (dx: ピクセル単位, dy: ライン)
const int kDX[16] = { 0, -1, -2, -4, 0, -1, 0, -1, -2, 0, -1, -2, 0, -1, -2, 0 };
const int kDY[16] = { 0, 0, 0, 0, -1, -1, -2, -2, -2, -4, -4, -4, -8, -8, -8, -16 };
// 圧縮時の検索順
const int kSearchOrder[15] = { 1, 4, 5, 6, 7, 9, 10, 2, 8, 11, 12, 13, 14, 3, 15 };

uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
uint32_t rd32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
void wr16(std::vector<uint8_t>& v, size_t at, uint16_t x) { v[at] = (uint8_t)x; v[at + 1] = (uint8_t)(x >> 8); }
void wr32(std::vector<uint8_t>& v, size_t at, uint32_t x) { for (int i = 0; i < 4; ++i) v[at + i] = (uint8_t)(x >> (8 * i)); }

std::string trimRight(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\0')) s.pop_back();
    return s;
}

} // namespace

std::string MagInfo::describeScreenMode() const {
    std::string s;
    s += is200Line() ? "200ライン" : "400ライン";
    if (screenMode & 0x80) s += " 256色";
    else {
        s += (screenMode & 0x04) ? " デジタル" : " アナログ";
        s += (screenMode & 0x02) ? "8色" : "16色";
    }
    char buf[16];
    std::snprintf(buf, sizeof(buf), " ($%02X)", screenMode);
    return s + buf;
}

bool isMagData(const std::vector<uint8_t>& data) {
    return data.size() >= 8 && std::memcmp(data.data(), kMagic, 8) == 0;
}

bool loadMag(const std::vector<uint8_t>& data, IndexedImage& out, MagInfo* infoOut, std::string* error, std::string* warnings) {
    auto fail = [&](const char* m) { if (error) *error = m; return false; };
    auto warn = [&](const char* m) { if (warnings) { if (!warnings->empty()) *warnings += "\n"; *warnings += m; } };
    if (!isMagData(data)) return fail("MAG ファイルではありません (MAKI02 ヘッダなし)");

    MagInfo info;
    size_t eof = 8;
    while (eof < data.size() && data[eof] != 0x1A) ++eof;
    if (eof >= data.size()) return fail("コメント終端 (0x1A) が見つかりません");
    info.comment.assign((const char*)data.data() + 8, eof - 8);
    if (eof >= 12) info.machineName.assign((const char*)data.data() + 8, 4);
    if (eof > 13) info.user = trimRight(std::string((const char*)data.data() + 13, std::min<size_t>(18, eof - 13)));
    if (eof > 32) info.memo = trimRight(std::string((const char*)data.data() + 32, eof - 32));

    size_t hp = eof + 1;
    // ヘッダ先頭は 0。そうでなければ 0 まで読み捨てる
    while (hp < data.size() && data[hp] != 0) ++hp;
    if (hp + 32 > data.size()) return fail("ヘッダが不完全です");
    const uint8_t* H = data.data() + hp;
    info.machineCode = H[1];
    info.machineFlags = H[2];
    info.screenMode = H[3];
    info.x0 = rd16(H + 4); info.y0 = rd16(H + 6);
    info.x1 = rd16(H + 8); info.y1 = rd16(H + 10);
    uint32_t offA = rd32(H + 12), offB = rd32(H + 16), sizeB = rd32(H + 20), offP = rd32(H + 24), sizeP = rd32(H + 28);
    const bool c256 = (info.screenMode & 0x80) != 0;
    info.colors = c256 ? 256 : 16;
    if (info.x1 < info.x0 || info.y1 < info.y0) return fail("表示範囲が不正です");

    // パレット (GRB)
    size_t palPos = hp + 32;
    size_t palBytes = (size_t)info.colors * 3;
    if (palPos + palBytes > data.size()) return fail("パレットが不完全です");
    out.palette.assign(info.colors, RGBA8{});
    bool nibbleStyle = true;
    for (size_t i = 0; i < palBytes; ++i) {
        uint8_t v = data[palPos + i];
        if (v != 0 && (v & 0x0F) != 0x0F) nibbleStyle = false;
    }
    for (int i = 0; i < info.colors; ++i) {
        uint8_t g = data[palPos + i * 3], r = data[palPos + i * 3 + 1], b = data[palPos + i * 3 + 2];
        if (nibbleStyle) { g = (uint8_t)((g >> 4) * 17); r = (uint8_t)((r >> 4) * 17); b = (uint8_t)((b >> 4) * 17); }
        out.palette[i] = rgb(r, g, b);
    }

    // 範囲 (横は 2 ピクセル単位に揃える)
    const int dotsPerUnit = c256 ? 2 : 4;
    const int align = dotsPerUnit * 2;
    const int xa0 = info.x0 & ~(align - 1);
    const int xa1 = info.x1 | (align - 1);
    const int width = xa1 - xa0 + 1;
    const int height = info.y1 - info.y0 + 1;
    const int units = width / dotsPerUnit;
    const int flagBytes = units / 2;
    if (width <= 0 || height <= 0 || width > 32768 || height > 32768) return fail("画像サイズが不正です");

    auto byteAt = [&](uint32_t off, size_t i, bool& oob) -> uint8_t {
        size_t p = hp + (size_t)off + i;
        if (p >= data.size()) { oob = true; return 0; }
        return data[p];
    };
    bool oob = false;
    size_t sizeA = (offB > offA) ? (size_t)(offB - offA) : 0;

    std::vector<uint8_t> flags(flagBytes, 0);
    std::vector<uint16_t> unitImg((size_t)units * height, 0);
    size_t aBit = 0, bPos = 0, pPos = 0;
    bool pixelShort = false;
    for (int y = 0; y < height; ++y) {
        for (int i = 0; i < flagBytes; ++i) {
            bool bit = false;
            if ((aBit >> 3) < sizeA || sizeA == 0) {
                uint8_t a = byteAt(offA, aBit >> 3, oob);
                bit = (a >> (7 - (aBit & 7))) & 1;
            }
            ++aBit;
            if (bit) {
                uint8_t b = (bPos < sizeB || sizeB == 0) ? byteAt(offB, bPos, oob) : 0;
                ++bPos;
                flags[i] ^= b;
            }
            for (int half = 0; half < 2; ++half) {
                int f = half == 0 ? (flags[i] >> 4) : (flags[i] & 0x0F);
                int ux = i * 2 + half;
                uint16_t v = 0;
                if (f == 0) {
                    if (pPos + 1 < sizeP || sizeP == 0) {
                        v = (uint16_t)((byteAt(offP, pPos, oob) << 8) | byteAt(offP, pPos + 1, oob));
                    } else pixelShort = true;
                    pPos += 2;
                } else {
                    int sx = ux + kDX[f], sy = y + kDY[f];
                    if (sx >= 0 && sy >= 0) v = unitImg[(size_t)sy * units + sx];
                }
                unitImg[(size_t)y * units + ux] = v;
            }
        }
    }
    if (oob) warn("データがファイル終端を越えています (一部欠損)");
    if (pixelShort) warn("ピクセルデータが不足しています");

    // 表示範囲に切り出し
    const int cropX = info.x0 - xa0;
    const int outW = info.x1 - info.x0 + 1;
    out.width = outW;
    out.height = height;
    out.indices.assign((size_t)outW * height, 0);
    out.alpha.clear();
    out.pixelAspect = info.is200Line() ? 0.5 : 1.0;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < outW; ++x) {
            int dx = x + cropX;
            uint16_t u = unitImg[(size_t)y * units + dx / dotsPerUnit];
            int k = dx % dotsPerUnit;
            uint8_t idx;
            if (c256) idx = (uint8_t)(k == 0 ? (u >> 8) : (u & 0xFF));
            else idx = (uint8_t)((u >> (12 - 4 * k)) & 0x0F);
            out.indices[(size_t)y * outW + x] = idx;
        }
    }
    if (infoOut) *infoOut = info;
    return true;
}

bool saveMag(const IndexedImage& img, const MagSaveOptions& opt, std::vector<uint8_t>& out, std::string* error) {
    auto fail = [&](const char* m) { if (error) *error = m; return false; };
    if (img.empty()) return fail("画像が空です");
    if (img.palette.empty() || img.palette.size() > 256) return fail("MAG は 256 色以下のパレット画像のみ保存できます");
    int maxIdx = 0;
    for (auto v : img.indices) maxIdx = std::max<int>(maxIdx, v);
    const bool c256 = opt.force256 || img.palette.size() > 16 || maxIdx > 15;
    const int colors = c256 ? 256 : 16;
    const int dotsPerUnit = c256 ? 2 : 4;
    const int align = dotsPerUnit * 2;
    const int W = (img.width + align - 1) / align * align;
    const int H = img.height;
    if (img.width > 65536 || H > 65536) return fail("画像が大きすぎます");
    const int units = W / dotsPerUnit;
    const int flagBytes = units / 2;

    // ピクセル単位
    std::vector<uint16_t> u((size_t)units * H, 0);
    for (int y = 0; y < H; ++y)
        for (int ux = 0; ux < units; ++ux) {
            uint16_t v = 0;
            for (int k = 0; k < dotsPerUnit; ++k) {
                int x = ux * dotsPerUnit + k;
                int idx = x < img.width ? img.indices[(size_t)y * img.width + x] : 0;
                if (c256) v = (uint16_t)((v << 8) | (idx & 0xFF));
                else v = (uint16_t)((v << 4) | (idx & 0x0F));
            }
            u[(size_t)y * units + ux] = v;
        }

    // フラグ作成
    std::vector<uint8_t> flag((size_t)units * H, 0);
    std::vector<uint8_t> pixelData;
    auto matches = [&](int ux, int y, int f) {
        int sx = ux + kDX[f], sy = y + kDY[f];
        if (sx < 0 || sy < 0) return false;
        return u[(size_t)sy * units + sx] == u[(size_t)y * units + ux];
    };
    for (int y = 0; y < H; ++y) {
        for (int ux = 0; ux < units; ++ux) {
            int f = 0;
            if (y > 0) {
                int up = flag[(size_t)(y - 1) * units + ux];
                if (up != 0 && matches(ux, y, up)) f = up;
            }
            if (f == 0) for (int c : kSearchOrder) if (matches(ux, y, c)) { f = c; break; }
            flag[(size_t)y * units + ux] = (uint8_t)f;
            if (f == 0) {
                uint16_t v = u[(size_t)y * units + ux];
                pixelData.push_back((uint8_t)(v >> 8));
                pixelData.push_back((uint8_t)(v & 0xFF));
            }
        }
    }

    // フラグ圧縮 (縦 XOR → A/B)
    std::vector<uint8_t> flagA, flagB;
    std::vector<uint8_t> prev(flagBytes, 0), cur(flagBytes, 0);
    uint8_t aByte = 0; int aBits = 0;
    for (int y = 0; y < H; ++y) {
        for (int i = 0; i < flagBytes; ++i) {
            cur[i] = (uint8_t)((flag[(size_t)y * units + i * 2] << 4) | flag[(size_t)y * units + i * 2 + 1]);
            uint8_t x = cur[i] ^ prev[i];
            aByte = (uint8_t)(aByte << 1);
            if (x) { aByte |= 1; flagB.push_back(x); }
            if (++aBits == 8) { flagA.push_back(aByte); aByte = 0; aBits = 0; }
        }
        std::swap(prev, cur);
    }
    if (aBits) flagA.push_back((uint8_t)(aByte << (8 - aBits)));
    if (flagA.size() & 1) flagA.push_back(0);
    if (flagB.size() & 1) flagB.push_back(0);

    // コメント
    out.clear();
    out.insert(out.end(), kMagic, kMagic + 8);
    std::string mn = opt.machineName;
    mn.resize(4, ' ');
    out.insert(out.end(), mn.begin(), mn.end());
    out.push_back(' ');
    std::string user = opt.user.empty() ? std::string("Unknown") : opt.user;
    if (user.size() > 18) user.resize(18);
    user.resize(18, ' ');
    out.insert(out.end(), user.begin(), user.end());
    out.push_back(' ');
    for (char c : opt.memo) {
        unsigned char uc = (unsigned char)c;
        if (uc == 0x1A || uc == 0x00) continue;
        if (uc < 0x20 && uc != '\t') continue; // 制御コードは除く
        out.push_back(uc);
    }
    out.push_back(0x1A);

    // ヘッダ
    size_t hp = out.size();
    out.resize(hp + 32, 0);
    int sm = opt.screenMode;
    if (sm < 0) {
        bool line200 = std::fabs(img.pixelAspect - 0.5) < 0.05;
        sm = c256 ? (0x80 | (line200 ? 1 : 0)) : ((line200 ? 1 : 0) | (img.palette.size() <= 8 ? 0x02 : 0));
    }
    if (c256) sm |= 0x80; else sm &= 0x7F;
    out[hp + 0] = 0;
    out[hp + 1] = opt.machineCode;
    out[hp + 2] = opt.machineFlags;
    out[hp + 3] = (uint8_t)sm;
    wr16(out, hp + 4, 0);
    wr16(out, hp + 6, 0);
    wr16(out, hp + 8, (uint16_t)(img.width - 1));
    wr16(out, hp + 10, (uint16_t)(H - 1));
    const uint32_t palBytes = (uint32_t)colors * 3;
    const uint32_t offA = 32 + palBytes;
    const uint32_t offB = offA + (uint32_t)flagA.size();
    const uint32_t offP = offB + (uint32_t)flagB.size();
    wr32(out, hp + 12, offA);
    wr32(out, hp + 16, offB);
    wr32(out, hp + 20, (uint32_t)flagB.size());
    wr32(out, hp + 24, offP);
    wr32(out, hp + 28, (uint32_t)pixelData.size());

    // パレット (GRB)。4bit 値なら MAG 流に下位ビットを 1 で埋める
    bool fourBit = true;
    for (auto& c : img.palette) if (c.r % 17 || c.g % 17 || c.b % 17) { fourBit = false; break; }
    auto enc = [&](uint8_t v) -> uint8_t {
        if (!fourBit) return v;
        int l = v / 17;
        return l == 0 ? 0 : (uint8_t)((l << 4) | 0x0F);
    };
    for (int i = 0; i < colors; ++i) {
        RGBA8 c;
        if (i < (int)img.palette.size()) c = img.palette[i];
        else if (!c256 && img.palette.size() <= 8 && i >= 8) c = img.palette[(i - 8) % img.palette.size()]; // 8色は 0=8, 1=9...
        else c = rgb(0, 0, 0);
        out.push_back(enc(c.g)); out.push_back(enc(c.r)); out.push_back(enc(c.b));
    }
    out.insert(out.end(), flagA.begin(), flagA.end());
    out.insert(out.end(), flagB.begin(), flagB.end());
    out.insert(out.end(), pixelData.begin(), pixelData.end());
    return true;
}

} // namespace ric
