#pragma once

#include "graphics/material/MaterialData.h"
#include <filesystem>
#include <string>

/// <summary>
/// モデルとは独立して保存・共有する材質の設定
/// </summary>
struct MaterialAsset {
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
	bool operator==(const MaterialAsset &other) const = default;
};

namespace MaterialSerializer {
/// <summary>
/// 材質をJSON文字列に変換する。失敗時は出力文字列を保持する
/// </summary>
/// <param name="asset">変換する有効な材質</param>
/// <param name="text">成功時に置き換えるJSON</param>
/// <param name="error">失敗理由。成功時は空</param>
/// <returns>true:成功 false:無効な設定または変換失敗</returns>
bool Serialize(const MaterialAsset &asset, std::string &text,
               std::string &error);

/// <summary>
/// 独立した材質ファイルを読み込む。失敗時は現在値を保持する
/// </summary>
/// <param name="path">絶対パスまたは実行ファイル基準の相対パス</param>
/// <param name="asset">成功時に置き換える材質</param>
/// <param name="error">失敗理由。成功時は空</param>
/// <returns>true:成功 false:形式や設定が無効、または読み込み失敗</returns>
bool Load(const std::filesystem::path &path, MaterialAsset &asset,
          std::string &error);

/// <summary>
/// 材質を保存する。既存ファイルの更新は書き込み完了後に置き換える
/// </summary>
/// <param name="path">保存先。親フォルダは事前に作成しておく</param>
/// <param name="asset">保存する材質</param>
/// <param name="error">失敗理由。成功時は空</param>
/// <param name="overwrite">falseでは既存ファイルを変更しない</param>
/// <returns>true:成功 false:設定が無効、またはファイル操作失敗</returns>
bool Save(const std::filesystem::path &path, const MaterialAsset &asset,
          std::string &error, bool overwrite = true);
} // namespace MaterialSerializer
