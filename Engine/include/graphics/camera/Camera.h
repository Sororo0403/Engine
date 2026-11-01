#pragma once

#include "graphics/camera/CameraPose.h"

#include <DirectXMath.h>

struct Ray;

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
	/// <returns>このカメラが保持するビュー行列への読み取り専用参照</returns>
	const DirectX::XMFLOAT4X4 &GetViewMatrix() const;

	/// <summary>
	/// 更新済みの投影行列を取得する
	/// </summary>
	/// <returns>このカメラが保持する投影行列への読み取り専用参照</returns>
	const DirectX::XMFLOAT4X4 &GetProjectionMatrix() const;

	/// <summary>
	/// カメラの位置と注視点を同じ量だけ移動する
	/// </summary>
	/// <param name="offset">ワールド座標での移動量</param>
	void Translate(const DirectX::XMFLOAT3 &offset);

	/// <summary>
	/// カメラの位置・注視点・上方向を設定する
	/// </summary>
	/// <param name="pose">設定する姿勢</param>
	/// <returns>true:成功 false:値が無効、または視線を計算できない</returns>
	bool SetPose(const CameraPose &pose);

	/// <summary>
	/// 更新済みの行列から、画像内の位置を通るレイを作成する
	/// </summary>
	/// <param name="imageX">画像内の横位置。左端0、右端1</param>
	/// <param name="imageY">画像内の縦位置。上端0、下端1</param>
	/// <param name="ray">成功した場合に設定するワールド座標のレイ</param>
	/// <returns>true:成功 false:位置または行列が無効</returns>
	bool CreatePickingRay(float imageX, float imageY, Ray &ray) const;

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
