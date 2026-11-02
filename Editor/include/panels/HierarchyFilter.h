#pragma once

#include "scene/SceneObjectId.h"
#include <string>

struct SceneObject;

/// <summary>
/// Hierarchyの名前またはID・表示状態・コンポーネントによる絞り込み条件を保持する
/// </summary>
class HierarchyFilter {
  public:
	/// <summary>
	/// 一覧に含めるコンポーネントまたはモデルの条件
	/// </summary>
	enum class Component {
		ALL,
		BOX_COLLIDER,
		MODELS_WITHOUT_COLLIDER,
		CHARACTER,
		CAMERA,
		FOLLOW_CAMERA,
		RESPAWN,
		COLOR_TINT,
	};

	std::string text;
	bool hiddenOnly = false;
	Component component = Component::ALL;

	/// <summary>
	/// 名前またはID・表示状態・コンポーネントの全条件に一致するか確認する。
	/// id:数値はIDの完全一致、その他は名前の部分一致とする。
	/// 未完成の設定も探せるよう、コンポーネントの有無だけを判定する
	/// </summary>
	/// <param name="id">確認する物体ID。0は無効</param>
	/// <param name="object">確認する物体。空の名前は表示と同じObjectとして扱う</param>
	/// <returns>true:全条件に一致 false:不一致、またはコンポーネント条件が無効</returns>
	bool Matches(SceneObjectId id, const SceneObject &object) const;

	/// <summary>
	/// 全条件を解除する。シーンと選択状態は変更しない
	/// </summary>
	void Clear() noexcept;
};
