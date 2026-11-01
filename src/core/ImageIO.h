// RetroImageConv - file input / output
// SPDX-License-Identifier: MIT
#pragma once

#include "Image.h"
#include "MagFormat.h"
#include <filesystem>
#include <string>
#include <vector>

namespace ric {

enum class FileFormat { Unknown, PNG, JPG, GIF, BMP, WEBP, MAG, TIFF };

FileFormat formatFromPath(const std::filesystem::path& p);
FileFormat formatFromName(const std::string& name); // "png" など
const char* formatExtension(FileFormat f);           // ".png" など
const char* formatName(FileFormat f);
std::vector<FileFormat> outputFormats();             // 出力可能な形式 (PNG が先頭)

struct LoadedImage {
    Image image;
    bool isMag = false;
    IndexedImage indexed;   // MAG の場合の番号画像
    MagInfo magInfo;
    std::string warnings;
};

bool readFileBytes(const std::filesystem::path& p, std::vector<uint8_t>& out);
bool writeFileBytes(const std::filesystem::path& p, const std::vector<uint8_t>& data);

bool loadImageFile(const std::filesystem::path& path, LoadedImage& out, std::string& error);

struct SaveParams {
    FileFormat format = FileFormat::PNG;
    int jpegQuality = 90;
    bool parCorrect = false;  // 表示比補正 (例: 640x200 → 640x400)
    int zoom = 1;             // 整数倍拡大
    MagSaveOptions mag;
};

// indexed はパレット方式の変換結果 (GIF/MAG で使用。nullptr 可)
bool saveImageFile(const std::filesystem::path& path, const Image& img, const IndexedImage* indexed,
                   const SaveParams& params, std::string& error);

// ---- プラットフォーム依存 (Windows は WIC) ----
bool platformDecode(const std::vector<uint8_t>& data, Image& out, std::string& error);
bool platformEncode(FileFormat f, const Image& img, int quality, std::vector<uint8_t>& out, std::string& error);
bool platformCanEncode(FileFormat f);

// 文字コード変換 (MAG コメント用)
std::string utf8ToSjis(const std::string& s);
std::string sjisToUtf8(const std::string& s);

// UTF-8 文字列 <-> パス
inline std::filesystem::path pathFromUtf8(const std::string& s) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size()));
}
inline std::string pathToUtf8(const std::filesystem::path& p) {
    auto u = p.u8string();
    return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}

IndexedImage scaleIndexedNearest(const IndexedImage& src, int dw, int dh);

} // namespace ric
