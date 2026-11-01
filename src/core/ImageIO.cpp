// SPDX-License-Identifier: MIT
#include "ImageIO.h"
#include "Codecs.h"
#include "Resize.h"
#include <fstream>
#include <cctype>
#include <cmath>

namespace ric {

FileFormat formatFromName(const std::string& name) {
    std::string e;
    for (char c : name) if (c != '.') e += (char)std::tolower((unsigned char)c);
    if (e == "png") return FileFormat::PNG;
    if (e == "jpg" || e == "jpeg" || e == "jpe" || e == "jfif") return FileFormat::JPG;
    if (e == "gif") return FileFormat::GIF;
    if (e == "bmp" || e == "dib") return FileFormat::BMP;
    if (e == "webp") return FileFormat::WEBP;
    if (e == "mag" || e == "mki") return FileFormat::MAG;
    if (e == "tif" || e == "tiff") return FileFormat::TIFF;
    return FileFormat::Unknown;
}

FileFormat formatFromPath(const std::filesystem::path& p) { return formatFromName(p.extension().string()); }

const char* formatExtension(FileFormat f) {
    switch (f) {
    case FileFormat::PNG: return ".png";
    case FileFormat::JPG: return ".jpg";
    case FileFormat::GIF: return ".gif";
    case FileFormat::BMP: return ".bmp";
    case FileFormat::WEBP: return ".webp";
    case FileFormat::MAG: return ".mag";
    case FileFormat::TIFF: return ".tif";
    default: return "";
    }
}

const char* formatName(FileFormat f) {
    switch (f) {
    case FileFormat::PNG: return "PNG";
    case FileFormat::JPG: return "JPEG";
    case FileFormat::GIF: return "GIF";
    case FileFormat::BMP: return "BMP";
    case FileFormat::WEBP: return "WebP";
    case FileFormat::MAG: return "MAG";
    case FileFormat::TIFF: return "TIFF";
    default: return "?";
    }
}

std::vector<FileFormat> outputFormats() {
    return { FileFormat::PNG, FileFormat::JPG, FileFormat::GIF, FileFormat::BMP, FileFormat::WEBP, FileFormat::MAG };
}

bool readFileBytes(const std::filesystem::path& p, std::vector<uint8_t>& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    std::streamoff sz = f.tellg();
    if (sz < 0) return false;
    f.seekg(0, std::ios::beg);
    out.resize((size_t)sz);
    if (sz > 0) f.read((char*)out.data(), sz);
    return (bool)f || f.eof();
}

bool writeFileBytes(const std::filesystem::path& p, const std::vector<uint8_t>& data) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write((const char*)data.data(), (std::streamsize)data.size());
    return (bool)f;
}

bool loadImageFile(const std::filesystem::path& path, LoadedImage& out, std::string& error) {
    std::vector<uint8_t> data;
    if (!readFileBytes(path, data)) { error = "ファイルを読み込めません"; return false; }
    out = LoadedImage{};
    if (isMagData(data)) {
        if (!loadMag(data, out.indexed, &out.magInfo, &error, &out.warnings)) return false;
        out.isMag = true;
        out.image = out.indexed.toRGBA();
        return true;
    }
    if (data.size() >= 2 && data[0] == 'B' && data[1] == 'M') {
        std::string e;
        if (decodeBMP(data, out.image, &e)) return true;
        // 対応外 BMP は OS のデコーダに任せる
    }
    if (platformDecode(data, out.image, error)) return true;
    if (error.empty()) error = "未対応の画像形式です";
    return false;
}

IndexedImage scaleIndexedNearest(const IndexedImage& src, int dw, int dh) {
    IndexedImage o;
    o.width = dw; o.height = dh; o.palette = src.palette;
    o.pixelAspect = src.pixelAspect * ((double)src.width / dw) / ((double)src.height / dh);
    o.indices.resize((size_t)dw * dh);
    if (!src.alpha.empty()) o.alpha.resize((size_t)dw * dh);
    for (int y = 0; y < dh; ++y) {
        int sy = std::min(src.height - 1, (int)((int64_t)y * src.height / dh));
        for (int x = 0; x < dw; ++x) {
            int sx = std::min(src.width - 1, (int)((int64_t)x * src.width / dw));
            size_t si = (size_t)sy * src.width + sx, di = (size_t)y * dw + x;
            o.indices[di] = src.indices[si];
            if (!src.alpha.empty()) o.alpha[di] = src.alpha[si];
        }
    }
    return o;
}

bool saveImageFile(const std::filesystem::path& path, const Image& srcImg, const IndexedImage* srcIdx,
                   const SaveParams& P, std::string& error) {
    Image img = applyOutputScale(srcImg, P.parCorrect, P.zoom);
    bool scaled = img.width != srcImg.width || img.height != srcImg.height;
    IndexedImage idxScaled;
    const IndexedImage* idx = srcIdx;
    if (srcIdx && !srcIdx->empty() && scaled) {
        idxScaled = scaleIndexedNearest(*srcIdx, img.width, img.height);
        idxScaled.pixelAspect = img.pixelAspect;
        idx = &idxScaled;
    }
    std::vector<uint8_t> data;
    switch (P.format) {
    case FileFormat::BMP:
        encodeBMP(img, img.hasAlpha(), data);
        break;
    case FileFormat::GIF: {
        IndexedImage tmp;
        if (idx && !idx->empty() && idx->palette.size() <= 256) tmp = *idx;
        else tmp = makeIndexed256(img);
        encodeGIF(tmp, data);
        break;
    }
    case FileFormat::WEBP:
        if (img.width > 16384 || img.height > 16384) { error = "WebP は 16384 ドットまでです"; return false; }
        encodeWebPLossless(img, data);
        break;
    case FileFormat::MAG: {
        IndexedImage tmp;
        if (idx && !idx->empty() && idx->palette.size() <= 256) tmp = *idx;
        else tmp = makeIndexed256(img); // 257 色以上は 256 色に減色して保存
        MagSaveOptions mo = P.mag;
        if (scaled && mo.screenMode >= 0) {
            bool line200 = std::fabs(tmp.pixelAspect - 0.5) < 0.05;
            mo.screenMode = (mo.screenMode & ~1) | (line200 ? 1 : 0);
        }
        if (!saveMag(tmp, mo, data, &error)) return false;
        break;
    }
    case FileFormat::PNG:
    case FileFormat::JPG:
    case FileFormat::TIFF:
        if (!platformEncode(P.format, img, P.jpegQuality, data, error)) return false;
        break;
    default:
        error = "未対応の出力形式です";
        return false;
    }
    if (!writeFileBytes(path, data)) { error = "ファイルを書き込めません"; return false; }
    return true;
}

} // namespace ric
