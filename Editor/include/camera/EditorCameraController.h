#pragma once

#include "camera/CameraView.h"
#include "graphics/camera/CameraPose.h"

#include <array>

struct ViewportInput;

class InputSystem;

/// <summary>
/// Sceneビューのカメラ移動と視点回転を管理する
/// </summary>
class EditorCameraController {
  public:
	/// <summary>
	/// 作業用カメラの位置と向き。角度はラジアンで、ロールは使用しない
	/// </summary>
	struct ViewState {
		DirectX::XMFLOAT3 position{0.0f, 0.0f, -2.0f};
		float yaw = 0.0f;
		float pitch = 0.0f;

		bool operator==(const ViewState &other) const {
			return position.x == other.position.x &&
			       position.y == other.position.y &&
			       position.z == other.position.z && yaw == other.yaw &&
			       pitch == other.pitch;
		}
	};

	/// <summary>
	/// 現在の位置と向きを、操作設定とは独立して取得する
	/// </summary>
	/// <returns>ブックマークに保存する視点</returns>
	ViewState GetViewState() const;

	/// <summary>
	/// 有効な視点を復元し、移動・パン・オービットの操作を終了する
	/// </summary>
	/// <param name="state">有限な位置・角度。pitchは上下90度以内</param>
	/// <returns>true:成功 false:姿勢を表現できない。失敗時は位置・向き・操作状態を保持する</returns>
	bool SetViewState(const ViewState &state);

	/// <summary>
	/// カメラの移動速度・加速倍率とマウス操作の設定
	/// </summary>
	struct Settings {
		float moveSpeed = 3.0f;
		float fastMultiplier = 4.0f;
		float mouseSensitivity = 0.003f;
		float wheelDistance = 0.5f;
		float panSensitivity = 0.005f;
	};

	/// <summary>
	/// 有効な操作設定を反映する。カメラ操作中は変更しない
	/// </summary>
	/// <param name="settings">有限で正の設定。加速倍率は1以上</param>
	/// <returns>true:成功 false:操作中、または値が指定範囲外</returns>
	bool SetSettings(const Settings &settings);

	/// <summary>
	/// 現在の操作設定を取得する
	/// </summary>
	/// <returns>現在の操作設定</returns>
	Settings GetSettings() const;

	/// <summary>
	/// Sceneビューへの入力からカメラの姿勢を更新する
	/// </summary>
	/// <param name="input">マウスとキーボードの入力状態</param>
	/// <param name="viewport">Scene画像に対する操作の可否</param>
	/// <param name="deltaTime">前フレームからの経過時間。単位は秒</param>
	/// <param name="orbitTarget">Alt＋左ドラッグの回転中心。nullptrは開始不可。開始後は中心を固定する</param>
	void Update(const InputSystem &input, const ViewportInput &viewport,
	            float deltaTime,
	            const DirectX::XMFLOAT3 *orbitTarget = nullptr);

	/// <summary>
	/// 現在のカメラの姿勢を取得する
	/// </summary>
	/// <returns>位置・注視点・上方向</returns>
	CameraPose GetPose() const;


	/// <summary>
	/// カメラの姿勢と操作状態を初期状態へ戻す
	/// </summary>
	void Reset();

	/// <summary>
	/// 指定中心までの距離を保ち、ワールド軸の方向から中心を見る。操作状態はリセットする
	/// </summary>
	/// <param name="view">対象を見る方向</param>
	/// <param name="center">注視する中心。現在位置と同じ場合は最小距離を使用する</param>
	/// <returns>true:成功 false:値や姿勢が無効。失敗時は現在の状態を保持する</returns>
	bool SetView(CameraView view, const DirectX::XMFLOAT3 &center);

	/// <summary>
	/// 現在の向きを保ち、指定されたボックス全体が収まる位置へ移動する
	/// </summary>
	/// <param name="corners">ワールド座標のボックスの8頂点</param>
	/// <param name="projection">描画に使用する中央対称の左手系透視投影行列</param>
	/// <returns>true:成功 false:値が無効、または描画範囲に収まらない</returns>
	bool FrameBounds(const std::array<DirectX::XMFLOAT3, 8> &corners,
	                 const DirectX::XMFLOAT4X4 &projection);

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
	bool panning_ = false;
	bool orbiting_ = false;
	DirectX::XMFLOAT3 orbitCenter_{};

	// カメラ操作の設定
	Settings settings_;
	static constexpr float kPitchLimit = DirectX::XM_PIDIV2;
	static constexpr float kMaximumMoveSpeed = 10000.0f;
	static constexpr float kMaximumFastMultiplier = 100.0f;
	static constexpr float kMaximumMouseSensitivity = 1.0f;
	static constexpr float kMaximumWheelDistance = 10000.0f;
	static constexpr float kMaximumWheelDelta = 10.0f;
	static constexpr float kMaximumPanSensitivity = 100.0f;

	// 選択物体を画面に収めるときの余白と最小距離
	static constexpr double kFramePadding = 1.1;
	static constexpr double kMinimumFrameDistance = 0.25;

	// クリップ面ぎりぎりへの配置を避ける
	static constexpr double kFrameClipPadding = 1.01;

	static constexpr double kMinimumPoseLengthSquared = 0.00000001;
};
