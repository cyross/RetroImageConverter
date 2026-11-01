// RetroImageConv CLI
// SPDX-License-Identifier: MIT
#include "../core/Converter.h"
#include "../core/ImageIO.h"
#include "../core/Resize.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <filesystem>
#include <algorithm>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#endif

using namespace ric;
namespace fs = std::filesystem;

namespace {

void usage() {
    std::printf(
        "RetroImageConv CLI - 画像減色・縮小コンバーター\n"
        "\n"
        "使い方: RetroImageConvCLI <入力> [-o 出力] [オプション]\n"
        "\n"
        "  --list                     プリセットとモードの一覧\n"
        "  --info                     入力画像 (MAG) の情報を表示して終了\n"
        "  -p, --preset ID            機種プリセット (既定: 98)\n"
        "  -m, --mode N|ID            モード番号 (0〜) またはモード ID\n"
        "  --scale machine|original|WxH  サイズ (既定: machine)\n"
        "  --aspect stretch|keep|keep-nopad|crop  縦横比 (既定: keep)\n"
        "  --crop-pos X,Y             切り抜き位置 (0〜100%%、既定 50,50 = 中央)\n"
        "  --crop-zoom F              切り抜きの拡大率 (1.0〜、既定 1.0)\n"
        "  --filter lanczos|bilinear|area|nearest\n"
        "  --dither none|bayer2|bayer4|bayer8|fs|jjn|stucki|atkinson|sierra|sierralite\n"
        "  --strength 0-100           ディザ強度\n"
        "  --no-serpentine            誤差拡散を一方向で走査\n"
        "  --tiling off|checker|quad|fine|vstripe|hstripe\n"
        "  --style free|88|98         タイリングの配色傾向\n"
        "  --colors N|65536|full      色数の上書き (2〜256)\n"
        "  --metric rgb|lab           色差の計算方法\n"
        "  --no-linear                混色を sRGB で計算 (既定は線形光)\n"
        "  --no-black                 パレットに黒を必ず含める処理をしない (既定は含める)\n"
        "  --no-tile-constraint       機種のタイル/属性制約を無視\n"
        "  --brightness N --contrast N --saturation N (-100〜100)  --gamma F\n"
        "  --alpha flatten|keep|binary  --bg RRGGBB  --threshold 0-255\n"
        "  --format png|jpg|gif|bmp|webp|mag  (既定: 出力ファイルの拡張子、なければ png)\n"
        "  --quality 1-100            JPEG 品質\n"
        "  --par                      表示比補正して出力 (640x200 → 640x400 など)\n"
        "  --zoom N                   整数倍拡大して出力\n"
        "  --mag-user NAME --mag-memo TEXT --mag-machine XXXX\n"
        "\n"
        "推奨ディザ設定はプリセットごとに自動で選ばれます (--dither / --tiling で上書き)。\n");
}

void listPresets() {
    for (auto& p : presets()) {
        std::printf("%-6s %s\n", p.id.c_str(), p.description.c_str());
        for (size_t i = 0; i < p.modes.size(); ++i)
            std::printf("    %zu: %-12s %s\n         %s\n", i, p.modes[i].id.c_str(), p.modes[i].label.c_str(), p.modes[i].summary().c_str());
    }
}

bool parseInt(const std::string& s, int& v) {
    char* e = nullptr;
    long x = std::strtol(s.c_str(), &e, 10);
    if (!e || *e) return false;
    v = (int)x;
    return true;
}

template <class T> bool parseE(const std::vector<EnumName>& t, const std::string& s, T& out) {
    int v;
    if (!parseEnum(t, s, v)) return false;
    out = (T)v;
    return true;
}

int run(std::vector<std::string> args) {
    if (args.empty()) { usage(); return 1; }
    ConvertOptions opt;
    SaveParams sp;
    std::string input, output, modeArg, formatArg;
    bool info = false, ditherSet = false, tilingSet = false, styleSet = false, strengthSet = false;
    std::string magUser, magMemo, magMachine;

    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        auto next = [&](std::string& v) -> bool {
            if (i + 1 >= args.size()) { std::fprintf(stderr, "%s の値がありません\n", a.c_str()); return false; }
            v = args[++i];
            return true;
        };
        std::string v;
        if (a == "-h" || a == "--help") { usage(); return 0; }
        else if (a == "--list") { listPresets(); return 0; }
        else if (a == "--info") info = true;
        else if (a == "-o" || a == "--output") { if (!next(output)) return 1; }
        else if (a == "-p" || a == "--preset") { if (!next(opt.presetId)) return 1; }
        else if (a == "-m" || a == "--mode") { if (!next(modeArg)) return 1; }
        else if (a == "--scale") {
            if (!next(v)) return 1;
            if (v == "machine") opt.scale = ScaleMode::Machine;
            else if (v == "original") opt.scale = ScaleMode::Original;
            else {
                auto x = v.find('x');
                if (x == std::string::npos || !parseInt(v.substr(0, x), opt.customWidth) || !parseInt(v.substr(x + 1), opt.customHeight)) {
                    std::fprintf(stderr, "--scale の値が不正です: %s\n", v.c_str()); return 1;
                }
                opt.scale = ScaleMode::Custom;
            }
        }
        else if (a == "--aspect") { if (!next(v) || !parseE(aspectNames(), v, opt.aspect)) { std::fprintf(stderr, "--aspect が不正です\n"); return 1; } }
        else if (a == "--filter") { if (!next(v) || !parseE(filterNames(), v, opt.filter)) { std::fprintf(stderr, "--filter が不正です\n"); return 1; } }
        else if (a == "--dither") { if (!next(v) || !parseE(ditherNames(), v, opt.dither)) { std::fprintf(stderr, "--dither が不正です\n"); return 1; } ditherSet = true; }
        else if (a == "--strength") { int s; if (!next(v) || !parseInt(v, s)) return 1; opt.ditherStrength = s / 100.f; strengthSet = true; }
        else if (a == "--no-serpentine") opt.serpentine = false;
        else if (a == "--tiling") { if (!next(v) || !parseE(tilingNames(), v, opt.tiling)) { std::fprintf(stderr, "--tiling が不正です\n"); return 1; } tilingSet = true; }
        else if (a == "--style") { if (!next(v) || !parseE(tileStyleNames(), v, opt.tileStyle)) { std::fprintf(stderr, "--style が不正です\n"); return 1; } styleSet = true; }
        else if (a == "--colors") {
            if (!next(v)) return 1;
            if (v == "full") opt.colorsOverride = 16777216;
            else if (!parseInt(v, opt.colorsOverride)) { std::fprintf(stderr, "--colors が不正です\n"); return 1; }
        }
        else if (a == "--metric") { if (!next(v) || !parseE(metricNames(), v, opt.metric)) return 1; }
        else if (a == "--black") opt.includeBlack = true;
        else if (a == "--no-black") opt.includeBlack = false;
        else if (a == "--no-linear") opt.linearLight = false;
        else if (a == "--linear") opt.linearLight = true;
        else if (a == "--crop-pos") {
            if (!next(v)) return 1;
            auto c = v.find(',');
            int px, py;
            if (c == std::string::npos || !parseInt(v.substr(0, c), px) || !parseInt(v.substr(c + 1), py)) {
                std::fprintf(stderr, "--crop-pos の値が不正です: %s\n", v.c_str()); return 1;
            }
            opt.cropX = std::clamp(px, 0, 100) / 100.f; opt.cropY = std::clamp(py, 0, 100) / 100.f;
            opt.aspect = AspectMode::Crop;
        }
        else if (a == "--crop-zoom") {
            if (!next(v)) return 1;
            opt.cropZoom = std::max(1.0f, (float)std::atof(v.c_str()));
            opt.aspect = AspectMode::Crop;
        }
        else if (a == "--no-tile-constraint") opt.keepTileConstraint = false;
        else if (a == "--brightness") { int s; if (!next(v) || !parseInt(v, s)) return 1; opt.brightness = (float)s; }
        else if (a == "--contrast") { int s; if (!next(v) || !parseInt(v, s)) return 1; opt.contrast = (float)s; }
        else if (a == "--saturation") { int s; if (!next(v) || !parseInt(v, s)) return 1; opt.saturation = (float)s; }
        else if (a == "--gamma") { if (!next(v)) return 1; opt.gamma = (float)std::atof(v.c_str()); }
        else if (a == "--alpha") { if (!next(v) || !parseE(alphaNames(), v, opt.alpha)) return 1; }
        else if (a == "--bg") {
            if (!next(v)) return 1;
            unsigned long c = std::strtoul(v.c_str(), nullptr, 16);
            opt.background = rgb((c >> 16) & 255, (c >> 8) & 255, c & 255);
        }
        else if (a == "--threshold") { if (!next(v) || !parseInt(v, opt.alphaThreshold)) return 1; }
        else if (a == "--format") { if (!next(formatArg)) return 1; }
        else if (a == "--quality") { if (!next(v) || !parseInt(v, sp.jpegQuality)) return 1; }
        else if (a == "--par") sp.parCorrect = true;
        else if (a == "--zoom") { if (!next(v) || !parseInt(v, sp.zoom)) return 1; }
        else if (a == "--mag-user") { if (!next(magUser)) return 1; }
        else if (a == "--mag-memo") { if (!next(magMemo)) return 1; }
        else if (a == "--mag-machine") { if (!next(magMachine)) return 1; }
        else if (!a.empty() && a[0] == '-') { std::fprintf(stderr, "不明なオプション: %s\n", a.c_str()); return 1; }
        else if (input.empty()) input = a;
        else if (output.empty()) output = a;
        else { std::fprintf(stderr, "引数が多すぎます: %s\n", a.c_str()); return 1; }
    }
    if (input.empty()) { usage(); return 1; }

    fs::path inPath = pathFromUtf8(input);
    LoadedImage li;
    std::string err;
    if (!loadImageFile(inPath, li, err)) { std::fprintf(stderr, "読み込み失敗: %s\n", err.c_str()); return 2; }
    if (!li.warnings.empty()) std::fprintf(stderr, "警告: %s\n", li.warnings.c_str());
    std::printf("入力: %dx%d%s\n", li.image.width, li.image.height, li.isMag ? " (MAG)" : "");
    if (li.isMag) {
        const MagInfo& m = li.magInfo;
        std::printf("  機種名: %s  ユーザー: %s\n  メモ: %s\n  %d色  %s  範囲(%d,%d)-(%d,%d)\n",
                    sjisToUtf8(m.machineName).c_str(), sjisToUtf8(m.user).c_str(), sjisToUtf8(m.memo).c_str(),
                    m.colors, m.describeScreenMode().c_str(), m.x0, m.y0, m.x1, m.y1);
    }
    if (info) return 0;

    const Preset* preset = findPreset(opt.presetId);
    if (!preset) { std::fprintf(stderr, "不明なプリセット: %s (--list で一覧)\n", opt.presetId.c_str()); return 1; }
    opt.presetId = preset->id;
    if (!modeArg.empty()) {
        int mi;
        if (parseInt(modeArg, mi)) opt.modeIndex = mi;
        else {
            opt.modeIndex = -1;
            for (size_t k = 0; k < preset->modes.size(); ++k) if (preset->modes[k].id == modeArg) opt.modeIndex = (int)k;
            if (opt.modeIndex < 0) { std::fprintf(stderr, "不明なモード: %s\n", modeArg.c_str()); return 1; }
        }
        if (opt.modeIndex < 0 || opt.modeIndex >= (int)preset->modes.size()) { std::fprintf(stderr, "モード番号が範囲外です\n"); return 1; }
    }
    {
        // 推奨設定を反映し、明示指定で上書き
        ConvertOptions rec = opt;
        applyRecommended(preset->modes[opt.modeIndex], rec);
        if (!ditherSet) opt.dither = rec.dither;
        if (!strengthSet) opt.ditherStrength = ditherSet ? 1.f : rec.ditherStrength;
        if (!tilingSet) opt.tiling = rec.tiling;
        if (!styleSet) opt.tileStyle = rec.tileStyle;
    }

    if (output.empty()) {
        FileFormat f = formatArg.empty() ? FileFormat::PNG : formatFromName(formatArg);
        fs::path o = inPath;
        o.replace_filename(pathFromUtf8(pathToUtf8(inPath.stem()) + "_" + preset->id + formatExtension(f)));
        output = pathToUtf8(o);
    }
    fs::path outPath = pathFromUtf8(output);
    sp.format = formatArg.empty() ? formatFromPath(outPath) : formatFromName(formatArg);
    if (sp.format == FileFormat::Unknown || sp.format == FileFormat::TIFF) sp.format = FileFormat::PNG;

    ConvertResult r = convert(li.image, opt);
    if (!r.ok) { std::fprintf(stderr, "変換失敗: %s\n", r.error.c_str()); return 3; }
    std::printf("変換: %s %s → %dx%d  使用色数 %d  (%.2f秒)\n", preset->id.c_str(), r.mode.label.c_str(),
                r.image.width, r.image.height, r.usedColors, r.seconds);

    sp.mag.user = utf8ToSjis(magUser);
    sp.mag.memo = utf8ToSjis(magMemo);
    if (!magMachine.empty()) sp.mag.machineName = magMachine;
    sp.mag.screenMode = r.mode.magScreenMode;
    if (!saveImageFile(outPath, r.image, r.indexed ? &r.indexedImage : nullptr, sp, err)) {
        std::fprintf(stderr, "保存失敗: %s\n", err.c_str());
        return 4;
    }
    std::printf("出力: %s (%s)\n", output.c_str(), formatName(sp.format));
    return 0;
}

} // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        int n = WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, nullptr, 0, nullptr, nullptr);
        std::string s((size_t)(n > 0 ? n - 1 : 0), '\0');
        if (n > 1) WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, s.data(), n, nullptr, nullptr);
        args.push_back(s);
    }
    int rc = run(args);
    CoUninitialize();
    return rc;
}
#else
int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    return run(args);
}
#endif
