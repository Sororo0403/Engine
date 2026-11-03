#pragma once

#include "graphics/material/MaterialData.h"
#include <filesystem>
#include <string>

/// <summary>
/// 旧シーンの画像参照を移行するためだけに読み込む材質の設定
/// </summary>
struct LegacyMaterial {
	MaterialData data;
	// 空は白い画像。指定時はマテリアルファイルを基準とした相対パス
	std::filesystem::path baseColorTexture;

	/// <summary>
	/// パラメーターとテクスチャの相対パスを検証する
	/// </summary>
	/// <returns>true:有効 false:値やパスが無効</returns>
	bool IsValid() const;

	/// <summary>
	/// 材質設定が一致するか確認する
	/// </summary>
	/// <param name="other">比較対象</param>
	/// <returns>true:一致 false:相違あり</returns>
	bool operator==(const LegacyMaterial &other) const = default;
};

namespace LegacyMaterialReader {

/// <summary>
/// version 1・2の材質を読み込む。version 1はLitとする。失敗時は現在値を保持する
/// </summary>
/// <param name="path">絶対パスまたは実行ファイル基準の相対パス</param>
/// <param name="asset">成功時に置き換える材質</param>
/// <param name="error">失敗理由。成功時は空</param>
/// <returns>true:成功 false:形式や設定が無効、または読み込み失敗</returns>
bool Load(const std::filesystem::path &path, LegacyMaterial &asset,
          std::string &error);

} // namespace LegacyMaterialReader
