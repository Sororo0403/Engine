#pragma once
#include "physics/RigidBodyWorld.h"
#include <map>
#include <optional>
#include <set>
class Scene;

/// <summary>
/// シーンのBox Colliderと剛体設定を、編集データから独立した物理状態へ接続する
/// </summary>
class ScenePhysics {
  public:
	/// <summary>
	/// 剛体の形状・親子姿勢・組み合わせを、物理ワールドを作らずに確認する
	/// </summary>
	/// <param name="scene">変更しないシーン</param>
	/// <param name="error">成功時は空、失敗時は理由</param>
	/// <param name="invalidObject">指定時は問題のID。成功時は0</param>
	/// <returns>true:有効 false:形状や構成が無効</returns>
	static bool Validate(const Scene &scene, std::string &error,
	                     SceneObjectId *invalidObject = nullptr);

	/// <summary>
	/// 現在のシーンへ物理状態を同期し、継続する剛体の速度と休止状態を保持する
	/// </summary>
	/// <param name="scene">作成または外部変更後のシーン</param>
	/// <param name="preserve">true:現在の実行状態を保持 false:開始時の設定で初期化</param>
	/// <param name="error">成功時は空、失敗時は理由</param>
	/// <param name="invalidObject">指定時は問題のID。成功時は0</param>
	/// <returns>true:同期成功 false:構成が無効。失敗時は現在の状態を保持する</returns>
	bool Configure(const Scene &scene, bool preserve, std::string &error,
	               SceneObjectId *invalidObject = nullptr);

	/// <summary>
	/// 移動床・主人公の姿勢を受け取り、物理演算の結果を親から順にローカル姿勢へ戻す
	/// </summary>
	/// <param name="scene">作業中のシーン。呼び出し側のトランザクション内で使用する</param>
	/// <param name="seconds">有限な0以上の秒数。0.1秒を上限とする</param>
	/// <param name="error">成功時は空、失敗時は理由</param>
	/// <returns>true:更新成功 false:設定や形状が無効</returns>
	bool Advance(Scene &scene, float seconds, std::string &error);

	/// <summary>
	/// 主人公の衝突問い合わせに使う、現在の剛体形状を取得する
	/// </summary>
	/// <returns>有効な剛体がある場合のワールド。剛体なしはnullptr</returns>
	const RigidBodyWorld *GetWorld() const {
		return world_ ? &*world_ : nullptr;
	}

	/// <summary>
	/// 経路から操作されない剛体の並進速度を、角速度を保って変更する
	/// </summary>
	/// <param name="id">有効な剛体の物体ID</param>
	/// <param name="velocity">ワールド並進速度</param>
	/// <param name="error">成功時は空、失敗時は理由</param>
	/// <returns>true:変更成功 false:対象・操作元・値が無効</returns>
	bool SetLinearVelocity(SceneObjectId id, const DirectX::XMFLOAT3 &velocity,
	                       std::string &error);

	/// <summary>
	/// 経路から操作されない剛体の角速度を、並進速度を保って変更する
	/// </summary>
	/// <param name="id">有効な剛体の物体ID</param>
	/// <param name="velocity">ワールド角速度。単位はrad/s</param>
	/// <param name="error">成功時は空、失敗時は理由</param>
	/// <returns>true:変更成功 false:対象・操作元・値が無効</returns>
	bool SetAngularVelocity(SceneObjectId id, const DirectX::XMFLOAT3 &velocity,
	                        std::string &error);

	/// <summary>
	/// 動的な剛体の重心へ、ワールド座標の力積を加える
	/// </summary>
	/// <param name="id">有効な動的剛体の物体ID</param>
	/// <param name="impulse">ワールド座標の力積</param>
	/// <param name="error">成功時は空、失敗時は理由</param>
	/// <returns>true:変更成功 false:対象や値が無効</returns>
	bool AddImpulse(SceneObjectId id, const DirectX::XMFLOAT3 &impulse,
	                std::string &error);

  private:
	std::optional<RigidBodyWorld> world_;
	std::map<SceneObjectId, RigidBodyDescription> definitions_;
	std::set<SceneObjectId> driven_;
};
