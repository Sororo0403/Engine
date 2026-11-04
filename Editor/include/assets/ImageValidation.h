#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

namespace ImageValidation {
// 画像取り込みとシーン検証で共通に使用する上限
inline constexpr std::size_t kMaximumSourceBytes = 256u * 1024u * 1024u;
inline constexpr unsigned kMaximumDimension = 16384;

/// <summary>
/// 画像ファイルを読み取り、先頭フレームをRGBAへ復号して全行を検証する。失敗時は例外
/// </summary>
/// <param name="path">画像の絶対パス、または実行ファイル基準の相対パス</param>
/// <returns>検証した元のバイト列。文字コードや改行は変更しない</returns>
/// <remarks>COM初期化後に呼び出す。画像は256MiBまで、各辺は16384ピクセルまで。ファイルとGPUリソースは変更しない</remarks>
std::string Read(const std::filesystem::path &path);
} // namespace ImageValidation
