#pragma once

#include "scene/SceneObject.h"
#include "scene/SceneObjectId.h"

#include <optional>
#include <span>
#include <vector>

class Scene;

/// <summary>
/// コピーした物体とTransformのデータを、元のシーンと独立して保持する
/// </summary>
class EditorClipboard {
  public:
	/// <summary>
	/// 指定した全物体のデータをコピーする。失敗時は以前の内容を保持する
	/// </summary>
	/// <param name="scene">コピー元のシーン</param>
	/// <param name="objectIds">重複のないコピー対象ID一覧</param>
	/// <returns>true:全件コピー成功 false:対象なし、重複、またはデータが無効</returns>
	bool Copy(const Scene &scene, std::span<const SceneObjectId> objectIds);

	/// <summary>
	/// コピーした物体データを取得する
	/// </summary>
	/// <returns>次のCopy成功またはClearまで有効なデータ一覧</returns>
	std::span<const SceneObject> GetObjects() const;

	/// <summary>
	/// 貼り付け可能な物体を保持しているか確認する
	/// </summary>
	/// <returns>true:データあり false:空</returns>
	bool HasObjects() const;

	/// <summary>
	/// Transformをコピーする。物体のコピー内容は変更しない
	/// </summary>
	/// <param name="transform">コピーする有効な変形情報</param>
	/// <returns>true:成功 false:値が無効。以前の内容は保持する</returns>
	bool CopyTransform(const Transform &transform);

	/// <summary>
	/// コピーしたTransformを取得する
	/// </summary>
	/// <returns>次のCopyTransform成功またはClearまで有効な値。未設定はnullptr</returns>
	const Transform *GetTransform() const;

	/// <summary>
	/// コピーしたデータを破棄する
	/// </summary>
	void Clear();

  private:
	std::vector<SceneObject> objects_;
	std::optional<Transform> transform_;
};
