#pragma once

#include "physics/Collider.h"
#include "scene/BoxCreationSettings.h"

class ImGuiSystem;
struct AxisAlignedBox;

/// <summary>
/// 実寸を指定するBox作成画面の操作結果
/// </summary>
enum class BoxCreationResult { NONE, ACCEPTED, CANCELLED, FRAME_PREVIEW };

/// <summary>
/// 名前・中心位置・実寸を下書きで指定し、作成または取消を受け付ける
/// </summary>
class BoxCreationDialog {
  public:
	/// <summary>
	/// 組み込みモデルのローカル範囲を保持し、初期値で表示を予約する
	/// </summary>
	/// <param name="bounds">実際のモデルから求めたローカルのBox</param>
	/// <param name="planeHeight">底面合わせで使用する作業平面の高さ。表示開始時の値を保持する</param>
	void Show(const Collider &bounds, float planeHeight = 0.0f);

	/// <summary>
	/// 入力した寸法を検証する。シーンや描画リソースは変更しない
	/// </summary>
	/// <param name="ui">フレーム開始済みのUI</param>
	/// <returns>編集中・作成確定・取消・下書きのフレーミング要求。フレーミングでは画面を閉じない</returns>
	BoxCreationResult Draw(const ImGuiSystem &ui);

	/// <summary>
	/// 作成時と同じ寸法検証を使い、現在の下書きのワールド座標の衝突範囲を取得する
	/// </summary>
	/// <param name="bounds">成功時に置き換える外枠。シーンやモデルのインスタンスは作成しない</param>
	/// <returns>true:取得成功 false:中心・寸法・モデルのローカル範囲が無効。失敗時は出力を保持する</returns>
	bool GetPreviewBounds(AxisAlignedBox &bounds) const;

	/// <summary>
	/// 入力した作成設定を取得する
	/// </summary>
	/// <returns>現在の下書き。作成側でもモデルの範囲と合わせて再検証する</returns>
	const BoxCreationSettings &GetSettings() const {
		return settings_;
	}

  private:
	BoxCreationSettings settings_;
	Collider bounds_;
	float planeHeight_ = 0.0f;
	bool opening_ = false;
};
