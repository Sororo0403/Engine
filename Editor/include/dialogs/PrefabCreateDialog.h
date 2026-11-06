#pragma once

#include "scene/Scene.h"
#include <filesystem>
#include <string>
class ImGuiSystem;

enum class PrefabCreateResult { NONE, CANCELLED, ACCEPTED };

/// <summary>
/// 選択した階層の固定コピーから、新しいPrefabアセットを作成する
/// </summary>
class PrefabCreateDialog {
  public:
	/// <summary>
	/// 保存する階層を検証して保持し、作成画面を準備する
	/// </summary>
	/// <param name="scene">変更しない編集シーン</param>
	/// <param name="root">単独で選択した根</param>
	/// <param name="assetRoot">Assetsの検索先</param>
	/// <param name="parent">検索先基準の作成先フォルダ</param>
	/// <param name="error">成功時は空、失敗時は理由</param>
	/// <returns>true:準備成功 false:階層または参照が無効</returns>
	bool Show(const Scene &scene, SceneObjectId root,
	          const std::filesystem::path &assetRoot,
	          const std::filesystem::path &parent, std::string &error);

	/// <summary>
	/// 名前の入力と新規保存を表示する。既存ファイルがある場合は上書きせず画面を保持する
	/// </summary>
	/// <param name="ui">フレーム開始済みのUI</param>
	/// <returns>編集中・取消・作成完了</returns>
	PrefabCreateResult Draw(const ImGuiSystem &ui);

	/// <summary>
	/// 作成したPrefabを取得する
	/// </summary>
	/// <returns>ACCEPTED後に使用できるAssets参照</returns>
	const std::filesystem::path &GetCreatedPath() const;

  private:
	Scene snapshot_;
	SceneObjectId root_ = 0;
	std::filesystem::path assets_, folder_, created_;
	std::string name_, error_;
	bool opening_ = false;
};
