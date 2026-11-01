#pragma once

#include "history/EditorHistory.h"
#include "scene/Scene.h"

#include <filesystem>
#include <functional>

/// <summary>
/// 編集中のシーン・保存先・未保存の変更を管理する
/// </summary>
class SceneDocument {
  public:
	SceneDocument() = default;

	SceneDocument(const SceneDocument &) = delete;
	SceneDocument &operator=(const SceneDocument &) = delete;

	/// <summary>
	/// 編集対象のシーンを取得する
	/// </summary>
	/// <returns>シーンへの参照。編集後はMarkModifiedを呼び出す</returns>
	Scene &GetScene();

	/// <summary>
	/// 読み取り専用のシーンを取得する
	/// </summary>
	/// <returns>シーンへの読み取り専用参照</returns>
	const Scene &GetScene() const;

	/// <summary>
	/// 現在の保存先を取得する
	/// </summary>
	/// <returns>保存先の絶対パス。未保存の場合は空のパス</returns>
	const std::filesystem::path &GetFilePath() const;

	/// <summary>
	/// 未保存の変更があるか取得する
	/// </summary>
	/// <returns>true:変更あり false:変更なし</returns>
	bool IsModified() const;

	/// <summary>
	/// 履歴対象外の変更を記録し、それ以前の編集履歴をクリアする
	/// </summary>
	void MarkModified();

	/// <summary>
	/// 空のシーンを作成し、保存先と変更状態をクリアする
	/// </summary>
	void New();

	/// <summary>
	/// 候補シーンを読み込み、事前確認の成功後に反映する。失敗時は現在の状態を保持する
	/// </summary>
	/// <param name="filePath">読み込むファイルのパス</param>
	/// <param name="validate">候補の事前確認（省略可）。現在のドキュメントは変更しない</param>
	/// <returns>true:成功 false:読み込み・事前確認の失敗。編集履歴と保存状態は保持する</returns>
	bool Open(const std::filesystem::path &filePath,
	          const std::function<bool(const Scene &)> &validate = {});

	/// <summary>
	/// 現在の保存先へシーンを保存する
	/// </summary>
	/// <returns>true:成功 false:保存先が未指定、または保存失敗</returns>
	bool Save();

	/// <summary>
	/// 指定先へシーンを保存する。成功した場合だけ保存先を変更する
	/// </summary>
	/// <param name="filePath">保存先のファイルパス</param>
	/// <returns>true:成功 false:保存失敗</returns>
	bool SaveAs(const std::filesystem::path &filePath);

	/// <summary>
	/// 編集中のシーンに対応する履歴を取得する
	/// </summary>
	/// <returns>編集履歴への参照</returns>
	EditorHistory &GetHistory();

	/// <summary>
	/// 読み取り専用の編集履歴を取得する
	/// </summary>
	/// <returns>編集履歴への読み取り専用参照</returns>
	const EditorHistory &GetHistory() const;

  private:
	Scene scene_;
	EditorHistory history_{scene_};

	std::filesystem::path filePath_;
};
