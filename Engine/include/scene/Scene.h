#pragma once

#include "scene/SceneObject.h"
#include "scene/SceneObjectId.h"

#include <filesystem>
#include <map>
#include <string>

/// <summary>
/// シーン上の物体データの作成・削除・取得を管理する
/// </summary>
class Scene {
  public:
	Scene() = default;

	Scene(const Scene &) = delete;
	Scene &operator=(const Scene &) = delete;

	/// <summary>
	/// 物体を作成する。描画リソースは作成しない
	/// </summary>
	/// <param name="name">物体の名前</param>
	/// <param name="modelPath">モデルの参照先。空の場合はモデルなし</param>
	/// <returns>このシーン内で有効な物体のID</returns>
	SceneObjectId CreateObject(std::string name = "Object",
	                           std::filesystem::path modelPath = {});

	/// <summary>
	/// 指定した物体を削除する
	/// </summary>
	/// <param name="id">削除する物体のID</param>
	/// <returns>true:削除成功 false:物体が存在しない</returns>
	bool DestroyObject(SceneObjectId id);

	/// <summary>
	/// 指定した物体が存在するか取得する
	/// </summary>
	/// <param name="id">確認する物体のID</param>
	/// <returns>true:物体あり false:物体なし</returns>
	bool Contains(SceneObjectId id) const;

	/// <summary>
	/// 編集用の物体データを取得する
	/// </summary>
	/// <param name="id">取得する物体のID</param>
	/// <returns>物体削除まで有効なポインター。存在しない場合はnullptr</returns>
	SceneObject *FindObject(SceneObjectId id);

	/// <summary>
	/// 読み取り専用の物体データを取得する
	/// </summary>
	/// <param name="id">取得する物体のID</param>
	/// <returns>物体削除まで有効なポインター。存在しない場合はnullptr</returns>
	const SceneObject *FindObject(SceneObjectId id) const;

	/// <summary>
	/// 全物体の一覧を取得する
	/// </summary>
	/// <returns>物体のIDとデータの対応</returns>
	const std::map<SceneObjectId, SceneObject> &GetObjects() const;

	/// <summary>
	/// 全物体を削除する。発行済みのIDは再利用しない
	/// </summary>
	void Clear();

  private:
	std::map<SceneObjectId, SceneObject> objects_;
	SceneObjectId nextObjectId_ = 1;
};
