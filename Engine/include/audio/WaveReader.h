#pragma once

#include "audio/WaveData.h"
#include <filesystem>
#include <span>
#include <string>

namespace WaveReader {
/// <summary>
/// RIFF/WAVEから非圧縮のPCMまたはIEEE float32を読み込む。失敗時は既存データを保持する
/// </summary>
/// <param name="bytes">RIFFヘッダーとチャンクを含むバイト列</param>
/// <param name="data">成功時に置き換える音声</param>
/// <param name="error">成功時は空、失敗時は理由</param>
/// <returns>true:成功 false:未対応の形式、範囲外、重複チャンク、または破損</returns>
bool Decode(std::span<const std::uint8_t> bytes, WaveData &data,
            std::string &error);

/// <summary>
/// 実行ファイルを基準にWAVを読み込む。ファイルのサイズを確認してから確保する
/// </summary>
/// <param name="path">絶対パスまたは実行ファイル基準の相対パス</param>
/// <param name="data">成功時に置き換える音声</param>
/// <param name="error">成功時は空、失敗時は理由</param>
/// <returns>true:成功 false:参照・読み込み・デコード失敗</returns>
bool Load(const std::filesystem::path &path, WaveData &data,
          std::string &error);
} // namespace WaveReader
