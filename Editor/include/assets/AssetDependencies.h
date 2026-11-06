#pragma once

#include <filesystem>
#include <vector>

namespace AssetDependencies {
/// <summary>
/// モデルをCPUで読み込み、本体・外部画像・バッファ・OBJ材質の参照先を収集する
/// </summary>
/// <param name="path">絶対パスまたは実行ファイル基準のモデルパス</param>
/// <returns>絶対パスの依存一覧。重複する場合がある。モデルの読み込みや参照形式が無効なら例外</returns>
/// <remarks>依存ファイルの存在は呼び出し側で検証する。画像の復号やGPUリソースの作成は行わない</remarks>
std::vector<std::filesystem::path>
ModelFiles(const std::filesystem::path &path);

/// <summary>
/// 画像の拡張子とパスを検証し、直接参照する画像ファイルを取得する
/// </summary>
/// <param name="path">絶対パスまたは実行ファイル基準の画像パス</param>
/// <returns>絶対パスの依存一覧。拡張子やパスが無効なら例外。画像の存在は呼び出し側で検証する</returns>
std::vector<std::filesystem::path>
TextureFiles(const std::filesystem::path &path);
/// <summary>
/// WAVの拡張子とパスを検証し、直接参照する音声ファイルを取得する
/// </summary>
/// <param name="path">絶対パスまたは実行ファイル基準のWAV</param>
/// <returns>絶対パスの依存一覧。参照が無効なら例外。内容は読み込み側で検証する</returns>
std::vector<std::filesystem::path>
AudioFiles(const std::filesystem::path &path);
} // namespace AssetDependencies
