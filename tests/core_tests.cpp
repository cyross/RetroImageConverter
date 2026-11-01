// RetroImageConv core tests (コマンドライン実行)
// SPDX-License-Identifier: MIT
#include "../src/core/Converter.h"
#include "../src/core/MagFormat.h"
#include "../src/core/Codecs.h"
#include "../src/core/ImageIO.h"
#include "../src/core/Resize.h"
#include <cstdio>
#include <cmath>
#include <set>
#include <map>
#include <string>
#include <filesystem>

using namespace ric;

static int g_fail = 0;
#define CHECK(c, ...) do { if (!(c)) { std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); ++g_fail; } } while (0)

static Image makeTestImage(int w, int h) {
    Image img(w, h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            float fx = (float)x / w, fy = (float)y / h;
            int r = (int)(255 * fx), g = (int)(255 * fy), b = (int)(255 * (0.5f + 0.5f * std::sin(fx * 9 + fy * 5)));
            if (((x / 37) + (y / 23)) % 5 == 0) { r = 250; g = 240; b = 230; }
            uint8_t a = 255;
            if (x < w / 8 && y < h / 8) a = (uint8_t)(255 * fx * 8);
            img.at(x, y) = RGBA8{ (uint8_t)r, (uint8_t)g, (uint8_t)b, a };
        }
    return img;
}

static IndexedImage randomIndexed(int w, int h, int colors, unsigned seed) {
    IndexedImage im;
    im.width = w; im.height = h;
    for (int i = 0; i < colors; ++i) im.palette.push_back(rgb((i * 37) & 255, (i * 91) & 255, (i * 53) & 255));
    im.indices.resize((size_t)w * h);
    unsigned s = seed;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            s = s * 1103515245u + 12345u;
            int v;
            int mode = (s >> 16) % 10;
            if (mode < 4 && x > 0) v = im.indices[(size_t)y * w + x - 1];
            else if (mode < 7 && y > 0) v = im.indices[(size_t)(y - 1) * w + x];
            else if (mode < 8) v = ((x / 2) + (y / 2)) % colors; // タイル
            else v = (s >> 8) % colors;
            im.indices[(size_t)y * w + x] = (uint8_t)v;
        }
    return im;
}

static void testMagRoundTrip() {
    const int sizes[][2] = { { 640, 400 }, { 13, 7 }, { 1, 1 }, { 255, 33 }, { 64, 64 } };
    for (int colors : { 8, 16, 256 }) {
        for (auto& sz : sizes) {
            IndexedImage im = randomIndexed(sz[0], sz[1], colors, (unsigned)(sz[0] * 31 + sz[1] + colors));
            if (colors == 16) for (auto& c : im.palette) c = rgb((c.r / 17) * 17, (c.g / 17) * 17, (c.b / 17) * 17);
            im.pixelAspect = (sz[1] == 7) ? 0.5 : 1.0;
            MagSaveOptions mo;
            mo.user = "tester";
            mo.memo = "round trip";
            std::vector<uint8_t> data;
            std::string err;
            CHECK(saveMag(im, mo, data, &err), "saveMag: %s", err.c_str());
            IndexedImage back;
            MagInfo info;
            std::string warn;
            CHECK(loadMag(data, back, &info, &err, &warn), "loadMag: %s", err.c_str());
            CHECK(warn.empty(), "warnings: %s", warn.c_str());
            CHECK(back.width == im.width && back.height == im.height, "size %dx%d vs %dx%d", back.width, back.height, im.width, im.height);
            CHECK(back.indices == im.indices, "indices differ (%d colors %dx%d)", colors, sz[0], sz[1]);
            for (size_t i = 0; i < im.palette.size(); ++i) CHECK(back.palette[i] == im.palette[i], "palette %zu differs", i);
            CHECK(info.user == "tester" && info.memo == "round trip", "comment '%s' '%s'", info.user.c_str(), info.memo.c_str());
            CHECK(std::fabs(back.pixelAspect - im.pixelAspect) < 1e-6, "pixelAspect");
            
        }
    }
    std::printf("MAG round trip done\n");
}

static void testPresets(const std::string& outDir) {
    Image src = makeTestImage(400, 300);
    int count = 0;
    for (auto& p : presets()) {
        for (size_t mi = 0; mi < p.modes.size(); ++mi) {
            ConvertOptions o;
            o.presetId = p.id;
            o.modeIndex = (int)mi;
            applyRecommended(p.modes[mi], o);
            if (p.id == "4K" || p.id == "FHD") o.scale = ScaleMode::Custom, o.customWidth = 320, o.customHeight = 180;
            ConvertResult r = convert(src, o);
            CHECK(r.ok, "%s/%zu: %s", p.id.c_str(), mi, r.error.c_str());
            if (!r.ok) continue;
            const ModeSpec& m = r.mode;
            if (o.scale == ScaleMode::Machine) CHECK(r.image.width == m.width && r.image.height == m.height, "%s size", p.id.c_str());
            if (m.isPalette()) {
                CHECK(r.indexed, "%s indexed", p.id.c_str());
                int maxc = m.tile.enabled() ? (m.tile.subPalettes > 0 ? m.tile.subPalettes * m.tile.colorsPerTile : 256) : m.maxColors();
                CHECK(r.usedColors <= maxc, "%s/%s used %d > %d", p.id.c_str(), m.id.c_str(), r.usedColors, maxc);
                // タイル制約の確認
                if (m.tile.enabled()) {
                    const auto& t = m.tile;
                    int tx = (r.image.width + t.tileW - 1) / t.tileW, ty = (r.image.height + t.tileH - 1) / t.tileH;
                    for (int j = 0; j < ty; ++j)
                        for (int i = 0; i < tx; ++i) {
                            std::set<uint32_t> cs;
                            std::set<int> idxs;
                            for (int y = j * t.tileH; y < std::min(r.image.height, (j + 1) * t.tileH); ++y)
                                for (int x = i * t.tileW; x < std::min(r.image.width, (i + 1) * t.tileW); ++x) {
                                    auto c = r.image.at(x, y);
                                    if (c.a == 0) continue;
                                    cs.insert((c.r << 16) | (c.g << 8) | c.b);
                                    idxs.insert(r.indexedImage.indices[(size_t)y * r.image.width + x]);
                                }
                            CHECK((int)cs.size() <= t.colorsPerTile, "%s tile (%d,%d) has %zu colors", p.id.c_str(), i, j, cs.size());
                            if (t.subPalettes > 0 && !idxs.empty()) {
                                int g = *idxs.begin() / t.colorsPerTile;
                                for (int v : idxs) CHECK(v / t.colorsPerTile == g, "%s tile (%d,%d) mixes subpalettes", p.id.c_str(), i, j);
                            }
                        }
                }
                if (!m.fixedPalette.empty()) {
                    for (auto& c : r.indexedImage.palette) {
                        bool found = false;
                        for (auto& f : m.fixedPalette) if (f.r == c.r && f.g == c.g && f.b == c.b) found = true;
                        CHECK(found, "%s palette color not in fixed set", p.id.c_str());
                    }
                }
            }
            // 色空間の確認
            if (m.space.kind == SpaceKind::Bits && m.fixedPalette.empty()) {
                for (auto& px : r.image.pixels) {
                    if (px.a == 0) continue;
                    RGBA8 s = m.space.snap(Vec3(px));
                    if (!(s.r == px.r && s.g == px.g && s.b == px.b)) { CHECK(false, "%s/%s color out of space", p.id.c_str(), m.id.c_str()); break; }
                }
            }
            if (m.space.kind == SpaceKind::YJK) {
                // 4 ドット毎に J,K が一致するか (5bit 値で検算)
                bool ok = true;
                for (int y = 0; y < r.image.height && ok; ++y)
                    for (int x = 0; x + 3 < r.image.width && ok; x += 4) {
                        // すべての画素が同じ (J,K) と何らかの Y で再現できること
                        bool anyJK = false;
                        for (int J = -32; J < 32 && !anyJK; ++J)
                            for (int K = -32; K < 32 && !anyJK; ++K) {
                                bool all = true;
                                for (int k = 0; k < 4 && all; ++k) {
                                    auto c = r.image.at(x + k, y);
                                    if (c.a == 0) continue;
                                    bool f = false;
                                    for (int Y = 0; Y < 32 && !f; ++Y) {
                                        int rr = std::clamp(Y + J, 0, 31), gg = std::clamp(Y + K, 0, 31), bb = std::clamp((5 * Y - 2 * J - K) >> 2, 0, 31);
                                        if (expandBits(rr, 5) == c.r && expandBits(gg, 5) == c.g && expandBits(bb, 5) == c.b) f = true;
                                    }
                                    all = f;
                                }
                                anyJK = all;
                            }
                        if (!anyJK) { ok = false; CHECK(false, "YJK block (%d,%d) inconsistent", x, y); }
                        if (y > 20) break;
                    }
            }
            if (!outDir.empty()) {
                std::string base = outDir + "/" + p.id + "_" + m.id;
                std::string err;
                SaveParams sp; sp.format = FileFormat::BMP; sp.parCorrect = true;
                CHECK(saveImageFile(pathFromUtf8(base + ".bmp"), r.image, nullptr, sp, err), "bmp %s", err.c_str());
                if (r.indexed && r.indexedImage.palette.size() <= 256) {
                    sp.format = FileFormat::MAG; sp.parCorrect = false;
                    sp.mag.screenMode = m.magScreenMode;
                    CHECK(saveImageFile(pathFromUtf8(base + ".mag"), r.image, &r.indexedImage, sp, err), "mag %s", err.c_str());
                }
            }
            ++count;
        }
    }
    std::printf("presets converted: %d\n", count);
}

static void testDitherVariants(const std::string& outDir) {
    Image src = makeTestImage(320, 200);
    for (auto& d : ditherNames()) {
        for (auto& t : tilingNames()) {
            for (auto& st : tileStyleNames()) {
                ConvertOptions o;
                o.presetId = "88"; o.modeIndex = 0;
                o.dither = (DitherMethod)d.value; o.tiling = (TilingMode)t.value; o.tileStyle = (TileStyle)st.value;
                ConvertResult r = convert(src, o);
                CHECK(r.ok && r.usedColors <= 8, "88 %s/%s/%s", d.key, t.key, st.key);
                if (!outDir.empty() && st.value == (int)TileStyle::Style88 && (d.value == 0 || t.value == 0)) {
                    std::string err; SaveParams sp; sp.format = FileFormat::BMP; sp.parCorrect = true;
                    saveImageFile(pathFromUtf8(outDir + "/v88_" + d.key + "_" + t.key + ".bmp"), r.image, nullptr, sp, err);
                }
            }
        }
    }
    // オプション類
    for (auto& a : aspectNames()) for (auto& f : filterNames()) for (auto& al : alphaNames()) {
        ConvertOptions o; o.presetId = "98"; o.aspect = (AspectMode)a.value; o.filter = (ResizeFilter)f.value; o.alpha = (AlphaMode)al.value;
        o.metric = DistanceMetric::Lab; o.brightness = 10; o.contrast = 20; o.saturation = 30; o.gamma = 1.2f; o.includeBlack = true;
        ConvertResult r = convert(src, o);
        CHECK(r.ok && r.usedColors <= 16, "98 aspect %s filter %s alpha %s", a.key, f.key, al.key);
        if (a.value == (int)AspectMode::KeepNoPad) CHECK(r.image.width == 533 || r.image.height == 400, "keep-nopad size %dx%d", r.image.width, r.image.height);
    }
    for (auto& c : colorsNames()) {
        ConvertOptions o; o.presetId = "X68K"; o.colorsOverride = c.value; o.dither = DitherMethod::FloydSteinberg;
        ConvertResult r = convert(src, o);
        CHECK(r.ok, "colors %s", c.key);
        if (c.value > 0 && c.value <= 256) CHECK(r.usedColors <= c.value, "colors %s used %d", c.key, r.usedColors);
    }
    {
        ConvertOptions o; o.presetId = "98"; o.scale = ScaleMode::Original;
        ConvertResult r = convert(src, o);
        CHECK(r.ok && r.image.width == 320 && r.image.height == 200, "original size");
    }
    std::printf("dither/option variants done\n");
}

static void writeCodecSamples(const std::string& outDir) {
    if (outDir.empty()) return;
    Image src = makeTestImage(203, 117);
    std::vector<uint8_t> d;
    encodeWebPLossless(src, d); writeFileBytes(pathFromUtf8(outDir + "/codec.webp"), d);
    encodeBMP(src, true, d); writeFileBytes(pathFromUtf8(outDir + "/codec_alpha.bmp"), d);
    encodeBMP(src, false, d); writeFileBytes(pathFromUtf8(outDir + "/codec.bmp"), d);
    IndexedImage ix = makeIndexed256(src);
    encodeGIF(ix, d); writeFileBytes(pathFromUtf8(outDir + "/codec.gif"), d);
    // 単色・小サイズの境界
    Image one(1, 1, rgb(10, 20, 30));
    encodeWebPLossless(one, d); writeFileBytes(pathFromUtf8(outDir + "/one.webp"), d);
    IndexedImage oi = makeIndexed256(one);
    encodeGIF(oi, d); writeFileBytes(pathFromUtf8(outDir + "/one.gif"), d);
    Image flat(300, 200, rgb(200, 100, 50));
    encodeWebPLossless(flat, d); writeFileBytes(pathFromUtf8(outDir + "/flat.webp"), d);
    // 参照用 RGBA 生データ
    std::vector<uint8_t> raw;
    for (auto& p : src.pixels) { raw.push_back(p.r); raw.push_back(p.g); raw.push_back(p.b); raw.push_back(p.a); }
    writeFileBytes(pathFromUtf8(outDir + "/codec_ref.rgba"), raw);
    // BMP 読み戻し
    Image back; std::string err;
    std::vector<uint8_t> bm; encodeBMP(src, true, bm);
    CHECK(decodeBMP(bm, back, &err) && back.pixels == src.pixels, "bmp32 round trip %s", err.c_str());
    encodeBMP(src, false, bm);
    CHECK(decodeBMP(bm, back, &err), "bmp24 %s", err.c_str());
    std::printf("codec samples written\n");
}

static void testCropLayout() {
    // 400x300 (4:3) → 640x400 (1.6:1) : 縦方向を切り抜く
    Layout L = computeLayout(400, 300, 1.0, 640, 400, 1.0, AspectMode::Crop);
    CHECK(std::fabs(L.srcW - 400) < 1e-6 && std::fabs(L.srcH - 250) < 1e-6 && std::fabs(L.srcY - 25) < 1e-6, "crop center %f %f %f", L.srcW, L.srcH, L.srcY);
    L = computeLayout(400, 300, 1.0, 640, 400, 1.0, AspectMode::Crop, 0.5, 0.0);
    CHECK(std::fabs(L.srcY) < 1e-6, "crop top %f", L.srcY);
    L = computeLayout(400, 300, 1.0, 640, 400, 1.0, AspectMode::Crop, 0.5, 1.0);
    CHECK(std::fabs(L.srcY - 50) < 1e-6, "crop bottom %f", L.srcY);
    L = computeLayout(400, 300, 1.0, 640, 400, 1.0, AspectMode::Crop, 1.0, 0.0, 2.0);
    CHECK(std::fabs(L.srcW - 200) < 1e-6 && std::fabs(L.srcH - 125) < 1e-6 && std::fabs(L.srcX - 200) < 1e-6 && std::fabs(L.srcY) < 1e-6,
          "crop zoom %f %f %f %f", L.srcX, L.srcY, L.srcW, L.srcH);
    // 640x200 (ドット比 0.5) の画面: 見た目 640x400 と同じ比率で切り抜く
    L = computeLayout(400, 300, 1.0, 640, 200, 0.5, AspectMode::Crop);
    CHECK(std::fabs(L.srcH - 250) < 1e-6, "crop par %f", L.srcH);
    ConvertOptions o; o.presetId = "98"; o.aspect = AspectMode::Crop; o.cropX = 0.f; o.cropY = 0.f; o.cropZoom = 2.f;
    double x, y, w, h;
    CHECK(cropRect(400, 300, 1.0, o, x, y, w, h) && std::fabs(x) < 1e-6 && std::fabs(y) < 1e-6 && std::fabs(w - 200) < 1e-6, "cropRect");
    ConvertResult r = convert(makeTestImage(400, 300), o);
    CHECK(r.ok && r.image.width == 640 && r.image.height == 400, "crop convert");
    CHECK(ConvertOptions{}.linearLight, "linear default");
    std::printf("crop layout done\n");
}

int main(int argc, char** argv) {
    std::string outDir = argc > 1 ? argv[1] : "";
    if (!outDir.empty()) std::filesystem::create_directories(outDir);
    testMagRoundTrip();
    testCropLayout();
    writeCodecSamples(outDir);
    testDitherVariants(outDir);
    testPresets(outDir);
    std::printf(g_fail ? "FAILED: %d\n" : "ALL OK\n", g_fail);
    return g_fail ? 1 : 0;
}
