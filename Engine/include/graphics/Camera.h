#pragma once

#include <DirectXMath.h>

class Camera {
  public:
	/// <summary>
	/// ビュー行列と投影行列を更新する
	/// </summary>
	/// <param name="width">描画領域の幅</param>
	/// <param name="height">描画領域の高さ</param>
	/// <returns>true:成功 false:サイズが無効</returns>
	bool Update(int width, int height);

	/// <summary>
	/// 更新済みのビュー行列を取得する
	/// </summary>
	const DirectX::XMFLOAT4X4 &GetViewMatrix() const;

	/// <summary>
	/// 更新済みの投影行列を取得する
	/// </summary>
	const DirectX::XMFLOAT4X4 &GetProjectionMatrix() const;

  private:
	DirectX::XMFLOAT3 position_{0.0f, 0.0f, -2.0f};
	DirectX::XMFLOAT3 target_{0.0f, 0.0f, 0.0f};
	DirectX::XMFLOAT3 up_{0.0f, 1.0f, 0.0f};

	float fovY_ = DirectX::XM_PIDIV4;
	float nearZ_ = 0.1f;
	float farZ_ = 100.0f;

	DirectX::XMFLOAT4X4 viewMatrix_{};
	DirectX::XMFLOAT4X4 projectionMatrix_{};
};
