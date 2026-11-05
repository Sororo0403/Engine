#pragma once

#include "physics/AxisAlignedBox.h"
#include "physics/CollisionFilter.h"
#include "physics/TriggerEvent.h"
#include <map>
#include <span>
#include <vector>

class Scene;

/// <summary>
/// 親子合成したColliderのAABBと相互マスクからTriggerの接触イベントを生成する
/// </summary>
class TriggerWorld {
  public:
	/// <summary>
	/// 検知状態を初期化し、開始時に重なる組のEnterを生成する
	/// </summary>
	/// <param name="scene">変更しない検知対象のシーン</param>
	/// <returns>true:成功 false:親子関係・Collider・ワールド形状が無効。失敗時は現在の状態を保持する</returns>
	bool Reset(const Scene &scene);

	/// <summary>
	/// 前回の形状と比較し、組のID順にEnter・Stay・Exitを生成する。両端で離れていても経路で重なる場合はEnterとExitを同時に生成する
	/// </summary>
	/// <param name="scene">更新後のシーン。少なくとも一方がTriggerで、両者のマスクが許可した組を検知する</param>
	/// <param name="teleported">途中経路を検知しない物体ID。開始・終点の接触変化は通常どおり生成する</param>
	/// <returns>true:成功 false:形状や親子関係が無効。失敗時は形状・接触・イベントをすべて保持する</returns>
	/// <remarks>経路は前後のワールドAABBの各境界を直線補間する。回転の途中形状や曲線経路は再現しない。面・辺・頂点だけの接触は検知しない。新規追加や設定変更時は終点だけを調べる</remarks>
	bool Update(const Scene &scene,
	            std::span<const SceneObjectId> teleported = {});

	/// <summary>
	/// 形状・接触・イベントを破棄する。退出イベントは生成しない
	/// </summary>
	void Clear();

	/// <summary>
	/// 最後に成功したResetまたはUpdateで生成したイベントを取得する
	/// </summary>
	/// <returns>次の更新成功またはClearまで有効なイベント。同じ組の通過はEnter、Exitの順</returns>
	std::span<const TriggerEvent> GetEvents() const {
		return events_;
	}

	/// <summary>
	/// 最後の更新の終点で重なっている組を取得する
	/// </summary>
	/// <returns>ID順の接触一覧。瞬間的な通過の組は含まない</returns>
	std::span<const TriggerContact> GetContacts() const {
		return contacts_;
	}

  private:
	/// <summary>
	/// 前後の経路検知に使用するワールド形状とColliderの設定
	/// </summary>
	struct Shape {
		AxisAlignedBox bounds;
		CollisionFilter filter;
		bool trigger = false;
	};

	std::map<SceneObjectId, Shape> shapes_;
	std::vector<TriggerContact> contacts_;
	std::vector<TriggerEvent> events_;
};
