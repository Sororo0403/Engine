#pragma once

#include <DirectXMath.h>
#include <array>

/// <summary>
/// 剛体に使用する質量・摩擦・反発と、開始時の速度を保持する
/// </summary>
struct RigidBodySettings {
	static constexpr float kMinimumMass = 0.001f;
	static constexpr float kMaximumMass = 1000000.0f;
	static constexpr float kMaximumLinearSpeed = 500.0f;
	static constexpr float kMaximumAngularSpeed = 100.0f;
	bool enabled = true;
	// 有効時は重力や衝突の力で動かず、指定した姿勢まで移動する
	bool isKinematic = false;
	float mass = 1.0f;
	float friction = 0.5f;
	float restitution = 0.0f;
	float gravityFactor = 1.0f;
	float linearDamping = 0.05f;
	float angularDamping = 0.05f;
	DirectX::XMFLOAT3 linearVelocity{};
	// ワールド軸まわりの角速度。単位はrad/s
	DirectX::XMFLOAT3 angularVelocity{};
	// ワールドX・Y・Z軸まわりの回転をそれぞれ固定する
	std::array<bool, 3> freezeRotation{};

	/// <summary>
	/// 質量・係数と開始速度が対応する有限の範囲内か確認する
	/// </summary>
	/// <returns>true:有効 false:非有限値または範囲外</returns>
	bool IsValid() const;

	/// <summary>
	/// 設定値が一致するか確認する
	/// </summary>
	/// <param name="other">比較する設定</param>
	/// <returns>true:一致 false:不一致</returns>
	bool operator==(const RigidBodySettings &other) const;
};
