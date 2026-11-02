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
/// 材質を読み込み、本体と基本色画像の参照先を収集する
/// </summary>
/// <param name="path">絶対パスまたは実行ファイル基準の材質パス</param>
/// <returns>絶対パスの依存一覧。材質の形式や設定が無効なら例外。画像の存在は呼び出し側で検証する</returns>
std::vector<std::filesystem::path>
MaterialFiles(const std::filesystem::path &path);
} // namespace AssetDependencies
