#pragma once

#include "math/Transform.h"

class Scene;
class EditorSelection;

/// <summary>
/// 複数選択した物体の変形に使用する支点を求める
/// </summary>
class SelectionPivot {
  public:
	/// <summary>
	/// 原点の平均位置、またはアクティブ物体の原点を使用する
	/// </summary>
	enum class Mode {
		CENTER,
		ACTIVE,
	};

	/// <summary>
	/// 全選択物体の変形情報を確認し、指定された支点を作成する
	/// </summary>
	/// <param name="scene">選択物体を保持するシーン</param>
	/// <param name="selection">2個以上の物体の選択状態</param>
	/// <param name="mode">支点に使用する位置の種類</param>
	/// <param name="transform">成功した場合に設定する、回転なし・拡大率1の支点</param>
	/// <returns>true:成功 false:選択数・物体・変形情報・モードが無効</returns>
	static bool Make(const Scene &scene, const EditorSelection &selection,
	                 Mode mode, Transform &transform);
};
