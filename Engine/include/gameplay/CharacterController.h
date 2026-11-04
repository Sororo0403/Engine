#pragma once

#include "gameplay/CharacterInput.h"
#include "gameplay/CharacterSettings.h"
#include "math/Transform.h"
#include "physics/BoxCollider.h"
#include "scene/SceneObjectId.h"

class CollisionWorld;

/// <summary>
/// ボックス形状の主人公を固定刻みで移動させ、重力・着地・入力猶予付きのジャンプを処理する
/// </summary>
class CharacterController {
  public:
	/// <summary>
	/// 有効で障害物と重ならない開始状態を設定する
	/// </summary>
	/// <param name="transform">開始姿勢</param>
	/// <param name="collider">主人公のローカル衝突形状</param>
	/// <param name="settings">移動と重力の設定</param>
	/// <param name="world">主人公自身を除外した静的な衝突ワールド</param>
	/// <returns>true:成功 false:開始状態が無効。失敗時は現在の状態を保持する</returns>
	bool Initialize(const Transform &transform, const BoxCollider &collider,
	                const CharacterSettings &settings,
	                const CollisionWorld &world);

	/// <summary>
	/// 入力を反映して更新する。長いフレームは0.1秒までを処理し、内部では120Hzに分割する
	/// </summary>
	/// <param name="deltaTime">有限で0以上の経過時間（秒）</param>
	/// <param name="input">移動入力とジャンプの押下状態。斜め方向の入力は長さ1までに制限する</param>
	/// <param name="world">静的な衝突ワールド</param>
	/// <returns>true:成功 false:未初期化、無効な入力、または移動不能な状態。失敗時は現在の状態を保持する</returns>
	bool Update(float deltaTime, const CharacterInput &input,
	            const CollisionWorld &world);

	/// <summary>
	/// 移動速度と入力の蓄積をリセットし、指定位置へ移動する
	/// </summary>
	/// <param name="position">移動先のワールド座標</param>
	/// <param name="world">配置を確認する衝突ワールド</param>
	/// <returns>true:成功 false:未初期化、または配置不可。失敗時は現在の状態を保持する</returns>
	bool Teleport(const DirectX::XMFLOAT3 &position,
	              const CollisionWorld &world);

	/// <summary>
	/// 描画とシーンへ反映する現在の姿勢を取得する
	/// </summary>
	/// <returns>現在の姿勢</returns>
	const Transform &GetTransform() const {
		return transform_;
	}

	/// <summary>
	/// 地面に接しているか取得する
	/// </summary>
	/// <returns>true:接地 false:空中または未初期化</returns>
	bool IsGrounded() const {
		return grounded_;
	}

	/// <summary>
	/// 現在の形状から、接地判定と同じ短い下向き探索で足元のColliderを検索する。
	/// 鉛直速度や保持中の接地状態は変更せず、現在位置だけで問い合わせる
	/// </summary>
	/// <param name="world">自身を除外した衝突ワールド。初期化後に障害物が変わった場合も再確認する</param>
	/// <param name="objectId">成功時に設定する地面のID。範囲内にない場合は0。同距離では小さいIDを優先する</param>
	/// <returns>true:検索成功 false:未初期化、形状が無効、または現在位置が障害物と重なる。失敗時は出力を保持する</returns>
	bool FindGroundObject(const CollisionWorld &world,
	                      SceneObjectId &objectId) const;

	/// <summary>
	/// 上向きを正とする現在の鉛直速度を取得する
	/// </summary>
	/// <returns>ワールド座標の速度（毎秒）</returns>
	float GetVerticalVelocity() const {
		return verticalVelocity_;
	}

  private:
	/// <summary>
	/// 固定刻み1回分の移動を処理する
	/// </summary>
	/// <param name="input">この更新で使用する移動入力</param>
	/// <param name="world">静的な衝突ワールド</param>
	/// <returns>true:成功 false:移動不能な状態</returns>
	bool Step(const CharacterInput &input, const CollisionWorld &world);

	Transform transform_{};
	BoxCollider collider_{};
	CharacterSettings settings_{};
	float verticalVelocity_ = 0.0f;
	double accumulatedTime_ = 0.0;
	double coyoteRemaining_ = 0.0;
	double jumpBufferRemaining_ = 0.0;
	bool initialized_ = false;
	bool grounded_ = false;
	bool jumpPending_ = false;

	static constexpr double kFixedStepSeconds = 1.0 / 120.0;
	static constexpr float kMaximumFrameSeconds = 0.1f;
	static constexpr float kGroundProbeDistance = 0.002f;
};
