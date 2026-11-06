#pragma once
#include "physics/CollisionEvent.h"
#include <span>
#include <vector>
class Scene;
class RigidBodyWorld;

/// <summary>
/// 移動を止めるColliderの接触を比較し、1ゲームフレーム分の通知を保持する
/// </summary>
class CollisionEvents {
  public:
	static constexpr std::size_t kMaximumContacts = 8192;
	// Character Controllerの接触面に残す隙間も、接触の継続として扱う
	static constexpr float kContactTolerance = 0.0002f;

	/// <summary>
	/// 現在の固体形状と前回の接触を比較し、組のID順で通知を作る
	/// </summary>
	/// <param name="scene">有効状態とColliderの参照元。内容は変更しない</param>
	/// <param name="world">指定時は最新の剛体形状。未指定時は移動判定と同じワールドAABBを使用する</param>
	/// <param name="impacts">今回の物理更新の途中で触れた組。終点で離れていてもEnter・Exitを通知する</param>
	/// <returns>true:成功 false:形状・親子関係・接触数が無効。失敗時は前回の状態を保持する</returns>
	/// <remarks>Triggerと主人公の子のColliderを除外し、相互レイヤーマスクを確認する。休止中の剛体も検知する。AABBのみの経路上の接触は推定しない</remarks>
	bool Update(const Scene &scene, const RigidBodyWorld *world = nullptr,
	            std::span<const CollisionContact> impacts = {});

	/// <summary>
	/// 最後に成功した更新の通知を取得する
	/// </summary>
	/// <returns>次の更新成功または破棄まで有効。途中の衝突はEnter、Exitの順</returns>
	std::span<const CollisionEvent> GetEvents() const {
		return events_;
	}

	/// <summary>
	/// 最後の更新の終点で接触している組を取得する
	/// </summary>
	/// <returns>物体ID順の重複しない一覧</returns>
	std::span<const CollisionContact> GetContacts() const {
		return contacts_;
	}

  private:
	std::vector<CollisionContact> contacts_;
	std::vector<CollisionEvent> events_;
};
