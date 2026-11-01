// RetroImageConv - MAG (MAKI02) format loader / saver
// SPDX-License-Identifier: MIT
//
// MAG フォーマットは まきちゃんネット で策定された画像形式です。
// 仕様書の「当フォーマットは使用においていかなる制限もなく全く自由です」に基づき実装しています。
#pragma once

#include "Image.h"
#include <cstdint>
#include <string>
#include <vector>

namespace ric {

struct MagInfo {
    std::string machineName;   // コメント先頭の機種名 4 文字
    std::string user;          // ユーザー名 (Shift_JIS のまま)
    std::string memo;          // メモ (Shift_JIS のまま)
    std::string comment;       // チェックデータ以降 EOF までのコメント全体 (Shift_JIS)
    uint8_t machineCode = 0;   // ヘッダの機種コード
    uint8_t machineFlags = 0;  // 機種依存フラグ
    uint8_t screenMode = 0;    // スクリーンモード
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    int colors = 16;
    bool is200Line() const { return (screenMode & 0x01) != 0; }
    std::string describeScreenMode() const;
};

bool isMagData(const std::vector<uint8_t>& data);

// 読み込み。warnings にはデータ不足などの警告を追記
bool loadMag(const std::vector<uint8_t>& data, IndexedImage& out, MagInfo* info, std::string* error, std::string* warnings = nullptr);

struct MagSaveOptions {
    std::string machineName = "WIN ";  // 4 文字 (コメント領域)
    std::string user;                  // 18 バイトまで (Shift_JIS 推奨)。空なら ">謎<" 相当の "Unknown"
    std::string memo;                  // メモ (Shift_JIS 推奨)
    uint8_t machineCode = 0x00;        // $00 : 98/68 その他
    uint8_t machineFlags = 0x00;
    int screenMode = -1;               // -1 = 自動
    bool force256 = false;             // 16 色以下でも 256 色モードで保存
};

// 保存。パレットは 256 色以下であること
bool saveMag(const IndexedImage& img, const MagSaveOptions& opt, std::vector<uint8_t>& out, std::string* error);

} // namespace ric
