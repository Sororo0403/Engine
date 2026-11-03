#pragma once

#include "history/EditorHistory.h"
#include "scene/Scene.h"

#include <filesystem>
#include <functional>
#include <string>

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
	/// 最後の読み込み・保存の失敗理由を取得する
	/// </summary>
	/// <returns>エラー文。Newまたは操作成功後は空</returns>
	const std::string &GetError() const;

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
	/// 現在のシーンを保持し、保存先と編集履歴を切り離して未保存の新規シーンとして扱う
	/// </summary>
	/// <remarks>取り込みなどでシーンを置き換えた後に呼ぶ。空のシーンも保存確認の対象にする</remarks>
	void SetUntitled();

	/// <summary>
	/// 候補シーンを読み込み、事前確認の成功後に反映する。失敗時は現在の状態を保持する
	/// </summary>
	/// <param name="filePath">読み込むファイルのパス</param>
	/// <param name="validate">候補の事前確認（省略可）。現在のドキュメントは変更しない</param>
	/// <returns>true:成功 false:読み込み・事前確認の失敗。編集履歴と保存状態は保持する</returns>
	bool Open(const std::filesystem::path &filePath,
	          const std::function<bool(const Scene &)> &validate = {});

	/// <summary>
	/// 現在の保存先が前回の読み込み・保存時と同じ内容の場合だけ保存する
	/// </summary>
	/// <returns>true:成功 false:保存先が未指定、外部変更・削除・読み取り失敗、または保存失敗</returns>
	bool Save();

	/// <summary>
	/// 指定先へ保存する。現在の保存先と同じファイルは外部変更を確認し、成功時だけ保存先を変更する
	/// </summary>
	/// <param name="filePath">保存先のファイルパス</param>
	/// <returns>true:成功 false:保存失敗</returns>
	bool SaveAs(const std::filesystem::path &filePath);

	/// <summary>
	/// 編集を確定し、現在の保存先と保存地点を保持したまま別ファイルへ保存する
	/// </summary>
	/// <param name="filePath">コピー先のファイルパス。現在の保存先の別名は使用できない</param>
	/// <returns>true:成功 false:コピー先が無効、または保存失敗</returns>
	bool SaveCopy(const std::filesystem::path &filePath);

	/// <summary>
	/// 編集を確定し、指定スナップショットを別ファイルへ保存する。編集中のシーンと保存地点は保持する
	/// </summary>
	/// <param name="filePath">コピー先。現在の保存先や、その一時ファイルの別名は使用できない</param>
	/// <param name="snapshot">選択物体などを準備した独立したシーン。素材ファイルはコピーしない</param>
	/// <returns>true:成功 false:コピー先・データが無効、または書き込み失敗</returns>
	bool SaveCopy(const std::filesystem::path &filePath, const Scene &snapshot);

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
	std::string savedText_;
	std::string error_;
};
