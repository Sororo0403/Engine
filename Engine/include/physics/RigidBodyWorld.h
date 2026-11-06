#pragma once

#include "physics/AxisAlignedBox.h"
#include "physics/CollisionContact.h"
#include "physics/CollisionSweepResult.h"
#include "physics/RigidBodyDescription.h"
#include "physics/RigidBodyState.h"
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

/// <summary>
/// 箱・球・カプセルの重力・回転・衝突を計算する。外部ライブラリの型は内部に隠す
/// </summary>
/// <remarks>コピーは状態を共有し、変更時だけ独立する。失敗した操作では元の状態を保持する。同じインスタンスを複数スレッドから変更しない</remarks>
class RigidBodyWorld {
  public:
	static constexpr std::size_t kMaximumBodies = 1024;
	static constexpr float kMaximumFrameSeconds = 0.1f;
	static constexpr float kMaximumStepSeconds = 1.0f / 120.0f;

	/// <summary>
	/// 固有IDを持つ形状から物理ワールドを作る。Triggerは登録しない
	/// </summary>
	/// <param name="bodies">有効なワールド姿勢と非TriggerのCollider。球・カプセルには正の一様拡縮を使う。空も指定できる</param>
	/// <param name="error">成功時は空、失敗時は理由</param>
	/// <param name="invalidObject">指定時は失敗したID。全体の失敗と成功時は0</param>
	/// <returns>true:作成成功 false:設定や形状が無効。元の状態を保持する</returns>
	bool Rebuild(std::span<const RigidBodyDescription> bodies,
	             std::string &error, SceneObjectId *invalidObject = nullptr);

	/// <summary>
	/// 1フレームの物理演算を、最大1/120秒の間隔へ分割して実行する
	/// </summary>
	/// <param name="seconds">有限な0以上の秒数。0.1秒を超えた分は進めない。0と非正規化数は何も変更しない</param>
	/// <param name="error">成功時は空、失敗時は理由</param>
	/// <returns>true:成功 false:未作成、無効な時間、物理演算の上限超過。失敗時は全状態を保持する</returns>
	bool Update(float seconds, std::string &error);

	/// <summary>
	/// 物体の最新の姿勢と速度を取得する
	/// </summary>
	/// <param name="id">登録した物体ID</param>
	/// <returns>最新の状態。未作成または不在の場合はnullopt</returns>
	std::optional<RigidBodyState> GetState(SceneObjectId id) const;

	/// <summary>
	/// 拡大率を変えずに物体を瞬間移動する。現在の速度は保持する
	/// </summary>
	/// <param name="id">登録した物体ID。静的な障害物も指定できる</param>
	/// <param name="transform">登録時と同じ拡大率を持つ有効なワールド姿勢</param>
	/// <param name="error">成功時は空、失敗時は理由</param>
	/// <returns>true:成功 false:対象・姿勢が無効</returns>
	bool Teleport(SceneObjectId id, const Transform &transform,
	              std::string &error);

	/// <summary>
	/// Kinematicの物体を指定した姿勢まで動かす速度を設定する
	/// </summary>
	/// <param name="id">Kinematicの物体ID</param>
	/// <param name="transform">同じ拡大率を持つ移動先のワールド姿勢</param>
	/// <param name="seconds">移動に使用する有限な0.000001以上0.1以下の秒数</param>
	/// <param name="error">成功時は空、失敗時は理由</param>
	/// <returns>true:成功 false:対象・姿勢・時間・必要速度が無効</returns>
	bool MoveKinematic(SceneObjectId id, const Transform &transform,
	                   float seconds, std::string &error);

	/// <summary>
	/// 動的な剛体の重心へ瞬間的な力積を加え、休止中の物体を起こす
	/// </summary>
	/// <param name="id">動的な物体ID</param>
	/// <param name="impulse">ワールド座標の力積。各成分の絶対値は1000000以下</param>
	/// <param name="error">成功時は空、失敗時は理由</param>
	/// <returns>true:成功 false:対象や値が無効</returns>
	bool AddImpulse(SceneObjectId id, const DirectX::XMFLOAT3 &impulse,
	                std::string &error);

	/// <summary>
	/// 動的・Kinematicな物体の速度を設定する
	/// </summary>
	/// <param name="id">静的でない物体ID</param>
	/// <param name="linear">長さ500m/s以下のワールド並進速度</param>
	/// <param name="angular">長さ100rad/s以下のワールド角速度</param>
	/// <param name="error">成功時は空、失敗時は理由</param>
	/// <returns>true:成功 false:対象や速度が無効</returns>
	bool SetVelocity(SceneObjectId id, const DirectX::XMFLOAT3 &linear,
	                 const DirectX::XMFLOAT3 &angular, std::string &error);

	/// <summary>
	/// 他方の速度を保持し、並進速度だけを設定する
	/// </summary>
	/// <param name="id">静的でない物体ID</param>
	/// <param name="velocity">長さ500m/s以下のワールド並進速度</param>
	/// <param name="error">成功時は空、失敗時は理由</param>
	/// <returns>true:成功 false:対象や値が無効</returns>
	bool SetLinearVelocity(SceneObjectId id, const DirectX::XMFLOAT3 &velocity,
	                       std::string &error);

	/// <summary>
	/// 並進速度を保持し、角速度だけを設定する。回転固定は維持する
	/// </summary>
	/// <param name="id">静的でない物体ID</param>
	/// <param name="velocity">長さ100rad/s以下のワールド角速度</param>
	/// <param name="error">成功時は空、失敗時は理由</param>
	/// <returns>true:成功 false:対象や値が無効</returns>
	bool SetAngularVelocity(SceneObjectId id, const DirectX::XMFLOAT3 &velocity,
	                        std::string &error);

	/// <summary>
	/// 箱を現在の剛体形状と重ならずに配置できるか確認する
	/// </summary>
	/// <param name="box">ワールド軸に沿った問い合わせ用の箱</param>
	/// <param name="ignored">判定から除く物体ID</param>
	/// <param name="filter">指定時は相互の衝突マスクも確認する</param>
	/// <returns>true:配置可能 false:重なり、無効な値、または未作成</returns>
	bool
	CanPlace(const AxisAlignedBox &box,
	         std::span<const SceneObjectId> ignored = {},
	         const std::optional<CollisionFilter> &filter = std::nullopt) const;

	/// <summary>
	/// 箱を直線移動したときの最初の接触位置と、剛体の実際の面の法線を調べる
	/// </summary>
	/// <param name="box">移動前の問い合わせ用の箱</param>
	/// <param name="displacement">ワールド座標の移動量</param>
	/// <param name="result">成功時に置き換える結果。同時の接触では小さいIDを優先する</param>
	/// <param name="ignored">判定から除く物体ID</param>
	/// <param name="filter">指定時は相互の衝突マスクも確認する</param>
	/// <returns>true:検索成功 false:未作成または値が無効。失敗時は出力を保持する</returns>
	bool
	Sweep(const AxisAlignedBox &box, const DirectX::XMFLOAT3 &displacement,
	      CollisionSweepResult &result,
	      std::span<const SceneObjectId> ignored = {},
	      const std::optional<CollisionFilter> &filter = std::nullopt) const;

	/// <summary>
	/// 休止状態に依存せず、現在の形状同士が接触している組を求める
	/// </summary>
	/// <param name="contacts">成功時だけ設定するID順の結果。Triggerは登録されていないため含まない</param>
	/// <returns>true:検索成功 false:未作成・形状・件数が無効</returns>
	bool FindContacts(std::vector<CollisionContact> &contacts) const;

	/// <summary>
	/// 最後の正の時間の物理更新中に接触した組を取得する
	/// </summary>
	/// <returns>次の変更操作まで有効。休止中の継続接触はFindContactsで調べる</returns>
	std::span<const CollisionContact> GetFrameContacts() const;

  private:
	/// <summary>
	/// 片方の速度だけを検証して変更し、もう片方の実行値を保持する
	/// </summary>
	bool SetVelocityComponent(SceneObjectId id,
	                          const DirectX::XMFLOAT3 &velocity, bool angular,
	                          std::string &error);
	struct Impl;
	// コピー後の変更でJoltの接触・休止状態も含めて複製し、他のワールドを変えない
	std::shared_ptr<Impl> impl_;
};
