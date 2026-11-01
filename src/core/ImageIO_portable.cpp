// SPDX-License-Identifier: MIT
// Windows 以外 (テスト用) の実装。PNG/JPEG 等は非対応
#ifndef _WIN32
#include "ImageIO.h"

namespace ric {

bool platformDecode(const std::vector<uint8_t>&, Image&, std::string& error) {
    error = "この環境では BMP / MAG 以外の読み込みに対応していません";
    return false;
}

bool platformEncode(FileFormat f, const Image&, int, std::vector<uint8_t>&, std::string& error) {
    error = std::string(formatName(f)) + " の書き出しはこの環境では未対応です";
    return false;
}

bool platformCanEncode(FileFormat) { return false; }

std::string utf8ToSjis(const std::string& s) { return s; }
std::string sjisToUtf8(const std::string& s) { return s; }

} // namespace ric
#endif
