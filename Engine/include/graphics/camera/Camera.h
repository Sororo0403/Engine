#pragma once

#include "graphics/camera/CameraPose.h"
#include "graphics/camera/CameraSettings.h"

#include <DirectXMath.h>
#include <optional>

struct Ray;

/// <summary>
/// ビューポート内の正規化位置とカメラ前方への距離
/// </summary>
struct ViewportPoint {
	float x = 0, y = 0, depth = 0;
	bool visible = false;
};

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
	/// 次のUpdateで使用する透視投影の設定を変更する
	/// </summary>
	/// <param name="settings">縦視野角と表示距離</param>
	/// <returns>true:成功 false:設定が無効。失敗時は現在値を保持する</returns>
	bool SetSettings(const CameraSettings &settings);

	/// <summary>
	/// 更新済みの行列から、画像内の位置を通るレイを作成する
	/// </summary>
	/// <param name="imageX">画像内の横位置。左端0、右端1</param>
	/// <param name="imageY">画像内の縦位置。上端0、下端1</param>
	/// <param name="ray">成功した場合に設定するワールド座標のレイ</param>
	/// <returns>true:成功 false:位置または行列が無効</returns>
	bool CreatePickingRay(float imageX, float imageY, Ray &ray) const;

	/// <summary>
	/// 更新済みの行列で、ワールド座標を左上原点のビューポート座標へ変換する
	/// </summary>
	/// <param name="position">有限なワールド座標</param>
	/// <returns>カメラ前方の投影位置。画面外やクリップ範囲外はvisibleがfalse。背面・無効時はnullopt</returns>
	std::optional<ViewportPoint>
	Project(const DirectX::XMFLOAT3 &position) const;

  private:
	DirectX::XMFLOAT3 position_{0.0f, 0.0f, -2.0f};
	DirectX::XMFLOAT3 target_{0.0f, 0.0f, 0.0f};
	DirectX::XMFLOAT3 up_{0.0f, 1.0f, 0.0f};

	CameraSettings settings_{};

	DirectX::XMFLOAT4X4 viewMatrix_{};
	DirectX::XMFLOAT4X4 projectionMatrix_{};
};
