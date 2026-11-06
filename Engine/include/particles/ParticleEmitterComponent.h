#pragma once
#include <DirectXMath.h>
#include <array>
#include <cstdint>
#include <filesystem>

/// <summary>
/// 画像付きの粒子を円盤から円錐方向へ発生させる、編集・保存用の設定
/// </summary>
struct ParticleEmitterComponent {
	static constexpr int kMaximumParticles = 4096;
	static constexpr float kMaximumRate = 10000;
	static constexpr float kMaximumLifetime = 30;
	bool playOnStart = true;
	bool loop = true;
	bool localSpace = false;
	bool additive = false;
	std::filesystem::path texture;
	std::uint32_t seed = 1;
	int maxParticles = 256;
	int burst = 16;
	float duration = 2;
	float rate = 20;
	float lifetimeMin = .5f, lifetimeMax = 1.5f;
	float speedMin = 1, speedMax = 2;
	float radius = .05f;
	float coneDegrees = 25;
	float sizeStart = .2f, sizeEnd = 0;
	float rotationSpeed = 0;
	DirectX::XMFLOAT3 gravity{0, -1, 0};
	std::array<float, 4> colorStart{1, 1, 1, 1}, colorEnd{1, 1, 1, 0};

	/// <summary>
	/// 発生数・時間・姿勢・色と、任意の画像参照を検証する
	/// </summary>
	/// <returns>true:有効 false:範囲外・非有限・不正な画像参照</returns>
	bool IsValid() const;
	/// <summary>
	/// 保存する設定がすべて一致するか確認する
	/// </summary>
	/// <param name="other">比較する発生元</param>
	/// <returns>true:一致 false:不一致</returns>
	bool operator==(const ParticleEmitterComponent &other) const;
};
