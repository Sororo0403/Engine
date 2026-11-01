#pragma once

#include "scene/SceneObject.h"
#include "scene/SceneObjectId.h"

#include <span>
#include <vector>

class Scene;

/// <summary>
/// コピーした物体のデータを、元のシーンと独立して保持する
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
	/// コピーしたデータを破棄する
	/// </summary>
	void Clear();

  private:
	std::vector<SceneObject> objects_;
};
