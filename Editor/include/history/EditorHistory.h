#pragma once

#include "history/ObjectCommand.h"
#include "history/TransformCommand.h"
#include "scene/SceneObject.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <variant>
#include <vector>

class Scene;

/// <summary>
/// シーンの編集履歴と保存地点を管理する
/// </summary>
class EditorHistory {
  public:
	/// <summary>
	/// 履歴を適用するシーンへの参照を保持する
	/// </summary>
	/// <param name="scene">この履歴より長く保持するシーン</param>
	explicit EditorHistory(Scene &scene) noexcept;

	EditorHistory(const EditorHistory &) = delete;
	EditorHistory &operator=(const EditorHistory &) = delete;

	/// <summary>
	/// 変更前の値を保持し、連続した変形編集を開始する
	/// </summary>
	/// <param name="objectId">編集する物体のID</param>
	/// <param name="before">編集を開始する前の変形情報</param>
	/// <returns>true:開始成功 false:物体がない、または値が無効</returns>
	bool BeginTransform(SceneObjectId objectId, const Transform &before);

	/// <summary>
	/// 編集中の変形情報を1操作として記録する。
	/// 変更がなければ履歴には追加しない
	/// </summary>
	void CommitTransform();

	/// <summary>
	/// 連続した変形編集の途中か確認する
	/// </summary>
	/// <returns>true:編集中 false:編集なし</returns>
	bool IsEditing() const;

	/// <summary>
	/// 元に戻せる操作があるか確認する
	/// </summary>
	/// <returns>true:実行可能 false:編集中、または履歴なし</returns>
	bool CanUndo() const;

	/// <summary>
	/// やり直せる操作があるか確認する
	/// </summary>
	/// <returns>true:実行可能 false:編集中、または履歴なし</returns>
	bool CanRedo() const;

	/// <summary>
	/// 直前の操作を元に戻す
	/// </summary>
	/// <returns>true:成功 false:実行不可、または履歴とシーンが一致しない</returns>
	bool Undo();

	/// <summary>
	/// 元に戻した操作をやり直す
	/// </summary>
	/// <returns>true:成功 false:実行不可、または履歴とシーンが一致しない</returns>
	bool Redo();

	/// <summary>
	/// 保存地点から変更されているか確認する
	/// </summary>
	/// <returns>true:変更あり false:保存地点と一致</returns>
	bool IsModified() const;

	/// <summary>
	/// 編集を確定し、現在の状態を保存地点として記録する
	/// </summary>
	void MarkSaved();

	/// <summary>
	/// 履歴対象外の変更を記録し、それ以前の履歴を無効にする
	/// </summary>
	void Invalidate();

	/// <summary>
	/// 履歴と編集状態をクリアし、変更なしの状態へ戻す
	/// </summary>
	void Reset();

	/// <summary>
	/// 物体を作成し、その追加を履歴へ記録する。
	/// 編集中の変形情報は先に確定する
	/// </summary>
	/// <param name="name">物体の名前</param>
	/// <param name="modelPath">モデルの参照先。空の場合はモデルなし</param>
	/// <returns>作成した物体のID</returns>
	SceneObjectId CreateObject(std::string name = "Object",
	                           std::filesystem::path modelPath = {});

	/// <summary>
	/// 指定した物体を削除し、その削除を履歴へ記録する。
	/// 編集中の変形情報は先に確定する
	/// </summary>
	/// <param name="objectId">削除する物体のID</param>
	/// <returns>true:削除成功 false:物体が存在しない</returns>
	bool DeleteObject(SceneObjectId objectId);

	/// <summary>
	/// 物体の全データを複製し、その追加を履歴へ記録する。
	/// 編集中の変形情報は先に確定する
	/// </summary>
	/// <param name="objectId">複製元の物体ID</param>
	/// <returns>成功:複製した物体のID 対象なし:0</returns>
	SceneObjectId DuplicateObject(SceneObjectId objectId);

  private:
	// Transformの変更、または物体の追加・削除を保持する
	using Command = std::variant<TransformCommand, ObjectCommand>;

	/// <summary>
	/// 操作と、その前後の状態を識別する情報
	/// </summary>
	struct Entry {
		Command command;

		std::uint64_t beforeState = 0;
		std::uint64_t afterState = 0;
	};

	/// <summary>
	/// 連続した編集を開始した時点の情報
	/// </summary>
	struct PendingTransform {
		SceneObjectId objectId = 0;
		Transform before;
	};

  private:
	/// <summary>
	/// 新しい状態の識別番号を発行する
	/// </summary>
	/// <returns>過去の状態と重複しない番号</returns>
	std::uint64_t AllocateState();

	/// <summary>
	/// 次の操作を記録する領域と状態番号を確保する。
	/// シーンを変更する前に呼び出す
	/// </summary>
	/// <returns>次の操作に割り当てる状態番号</returns>
	std::uint64_t PrepareAppend();

	/// <summary>
	/// シーンへ反映済みの操作を、確保済みの領域へ記録する
	/// </summary>
	/// <param name="command">反映済みの操作</param>
	/// <param name="nextState">PrepareAppendで取得した状態番号</param>
	void AppendPrepared(Command command, std::uint64_t nextState) noexcept;

	/// <summary>
	/// 物体の全データを使って作成し、追加操作を履歴へ記録する
	/// </summary>
	/// <param name="object">作成する物体のデータ</param>
	/// <returns>作成した物体のID</returns>
	/// <remarks>呼び出し前にCommitTransformを呼び出す</remarks>
	SceneObjectId CreateObjectWithHistory(SceneObject object);

  private:
	Scene &scene_;

	std::vector<Entry> entries_;
	std::size_t cursor_ = 0;

	std::optional<PendingTransform> pending_;

	std::uint64_t currentState_ = 0;
	std::uint64_t savedState_ = 0;
	std::uint64_t nextState_ = 1;
};
