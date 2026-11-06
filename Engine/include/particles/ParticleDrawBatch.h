#pragma once
#include <DirectXMath.h>
#include <filesystem>
#include <vector>

/// <summary>
/// ワールド座標で描画する1粒子の位置・大きさ・色・回転
/// </summary>
struct ParticleSprite {
	DirectX::XMFLOAT3 position{};
	float size = 0;
	DirectX::XMFLOAT4 color{1, 1, 1, 1};
	float rotation = 0;
};

/// <summary>
/// 同じ画像と混合方式を使う粒子の読み取り用スナップショット
/// </summary>
struct ParticleDrawBatch {
	std::filesystem::path texture;
	bool additive = false;
	std::vector<ParticleSprite> sprites;
};
