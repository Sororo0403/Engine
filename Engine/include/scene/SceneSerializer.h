#pragma once

#include <filesystem>
#include <string>
#include <string_view>

class Scene;

/// <summary>
/// シーンの物体データをJSON形式で保存・読み込みする
/// </summary>
class SceneSerializer {
  public:
	/// <summary>
	/// 保存する形式のバージョン。読み込みはversion 1からこの値まで対応する
	/// </summary>
	static constexpr int kCurrentVersion = 44;

	/// <summary>
	/// シーンを検証し、ファイルへ書き込まずJSON文字列に変換する
	/// </summary>
	/// <param name="scene">変換するシーン</param>
	/// <param name="text">成功時に置き換えるJSON文字列。失敗時は保持する</param>
	/// <returns>true:成功 false:データが無効</returns>
	static bool Serialize(const Scene &scene, std::string &text);

	/// <summary>
	/// version 1から44のJSON文字列を読み込む。失敗時は現在のシーンを保持する
	/// </summary>
	/// <param name="scene">成功時に置き換えるシーン</param>
	/// <param name="text">ファイルから取得したJSON。参照する間だけ保持すればよい</param>
	/// <returns>true:成功 false:形式や設定が無効</returns>
	static bool Deserialize(Scene &scene, std::string_view text);

	/// <summary>
	/// Audio Source・Audio Listener・Point Light・Spot Light・Trigger Actions・有効状態・親子関係・衝突レイヤー・Trigger・画像割り当て・ゲーム用コンポーネントを保存する。相対パスは実行ファイル基準
	/// </summary>
	/// <param name="scene">保存するシーン</param>
	/// <param name="filePath">保存先のファイルパス</param>
	/// <returns>true:成功 false:保存失敗</returns>
	static bool Save(const Scene &scene, const std::filesystem::path &filePath);

	/// <summary>
	/// version 1から44のシーンを読み込む。失敗時は現在のシーンを保持する
	/// </summary>
	/// <param name="scene">読み込み結果を反映するシーン</param>
	/// <param name="filePath">読み込むファイルのパス。相対パスは実行ファイル基準</param>
	/// <returns>true:成功 false:読み込み失敗</returns>
	static bool Load(Scene &scene, const std::filesystem::path &filePath);
};
