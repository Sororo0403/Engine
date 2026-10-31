#pragma once

#include "graphics/camera/CameraPose.h"

struct ViewportInput;

class InputSystem;

/// <summary>
/// Sceneビューのカメラ移動と視点回転を管理する
/// </summary>
class EditorCameraController {
  public:
	/// <summary>
	/// Sceneビューへの入力からカメラの姿勢を更新する
	/// </summary>
	/// <param name="input">マウスとキーボードの入力状態</param>
	/// <param name="viewport">Scene画像に対する操作の可否</param>
	/// <param name="deltaTime">前フレームからの経過時間。単位は秒</param>
	void Update(const InputSystem &input, const ViewportInput &viewport,
	            float deltaTime);

	/// <summary>
	/// 現在のカメラの姿勢を取得する
	/// </summary>
	/// <returns>位置・注視点・上方向</returns>
	CameraPose GetPose() const;

	/// <summary>
	/// カメラの姿勢と操作状態を初期状態へ戻す
	/// </summary>
	void Reset();

  private:
	/// <summary>
	/// 現在の回転角から前方向を計算する
	/// </summary>
	/// <returns>長さ1の前方向</returns>
	DirectX::XMFLOAT3 GetForward() const;

	DirectX::XMFLOAT3 position_{0.0f, 0.0f, -2.0f};

	float yaw_ = 0.0f;
	float pitch_ = 0.0f;

	bool navigating_ = false;

	// カメラ操作の設定
	float moveSpeed_ = 3.0f;
	float fastMultiplier_ = 4.0f;
	float mouseSensitivity_ = 0.003f;
};
