#pragma once

#include <filesystem>

class Scene;

/// <summary>
/// シーンの物体データをJSON形式で保存・読み込みする
/// </summary>
class SceneSerializer {
  public:
	/// <summary>
	/// 色・描画・ゲーム用コンポーネントを含むversion 7形式で保存する。相対パスは実行ファイル基準
	/// </summary>
	/// <param name="scene">保存するシーン</param>
	/// <param name="filePath">保存先のファイルパス</param>
	/// <returns>true:成功 false:保存失敗</returns>
	static bool Save(const Scene &scene, const std::filesystem::path &filePath);

	/// <summary>
	/// version 1から7のシーンを読み込む。失敗時は現在のシーンを保持する
	/// </summary>
	/// <param name="scene">読み込み結果を反映するシーン</param>
	/// <param name="filePath">読み込むファイルのパス。相対パスは実行ファイル基準</param>
	/// <returns>true:成功 false:読み込み失敗</returns>
	static bool Load(Scene &scene, const std::filesystem::path &filePath);
};
