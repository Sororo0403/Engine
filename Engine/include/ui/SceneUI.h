#pragma once
#include "ui/CanvasLayout.h"
#include "ui/UIButtonVisual.h"
#include "ui/UIInput.h"
#include <span>
#include <string>
#include <vector>
class Scene;

/// <summary>
/// シーンのUIに対する選択・押下・クリックと表示状態を保持する。編集データは変更しない
/// </summary>
class SceneUI {
  public:
	/// <summary>
	/// 入力を1回反映し、このフレームのクリックと消費した操作を更新する
	/// </summary>
	/// <param name="scene">描画されているゲームシーン</param>
	/// <param name="input">表示先に対応した入力</param>
	/// <param name="error">失敗理由。成功時は空</param>
	/// <returns>true:成功 false:入力・配置が無効。以前の実行状態を保持する</returns>
	bool Update(const Scene &scene, const UIInput &input, std::string &error);
	/// <summary>
	/// スクリプトや有効設定の変更後に、古い押下・選択を解除して表示を同期する。クリックは追加しない
	/// </summary>
	/// <param name="scene">確定予定のゲームシーン</param>
	/// <param name="error">失敗理由。成功時は空</param>
	/// <returns>true:同期成功 false:配置が無効。以前の状態を保持する</returns>
	bool Refresh(const Scene &scene, std::string &error);
	/// <summary>
	/// フォーカス喪失や一時停止時に操作を解除する。復帰後は一度離すまで決定を受け付けない
	/// </summary>
	void Cancel() noexcept;

	/// <summary>
	/// このフレームにクリックされた物体を取得する
	/// </summary>
	/// <returns>次のUpdate・Cancelまで有効な、重複しない物体ID</returns>
	std::span<const SceneObjectId> GetClicks() const {
		return clicks_;
	}

	/// <summary>
	/// 現在の表示状態を取得する
	/// </summary>
	/// <returns>次の状態変更まで有効な一覧</returns>
	std::span<const UIButtonVisual> GetVisuals() const {
		return visuals_;
	}

	/// <summary>
	/// UIがポインター操作を使用しているか確認する
	/// </summary>
	/// <returns>true:背後のゲーム操作へ渡さない false:UIは使用していない</returns>
	bool ConsumesPointer() const {
		return consumesPointer_;
	}

	/// <summary>
	/// UIがキーボード・ゲームパッドの操作を使用しているか確認する
	/// </summary>
	/// <returns>true:移動・ジャンプ・カメラへ渡さない false:UIは使用していない</returns>
	bool ConsumesNavigation() const {
		return consumesNavigation_;
	}

	/// <summary>
	/// 選択しているボタンを取得する
	/// </summary>
	/// <returns>物体ID。未選択は0</returns>
	SceneObjectId GetSelected() const {
		return selected_;
	}

  private:
	/// <summary>
	/// 反映済みの入力に対して押下・選択の有効性と表示を再評価する
	/// </summary>
	/// <param name="scene">描画するシーン</param>
	/// <param name="elements">現在の配置</param>
	void Synchronize(const Scene &scene, std::span<const UIElement> elements);
	SceneObjectId selected_ = 0, pressed_ = 0, hovered_ = 0;
	SceneObjectId pressedParent_ = 0, pressedCanvas_ = 0;
	UIRect pressedRect_;
	UITransform pressedTransform_;
	UIInput last_;
	bool pointerSequence_ = false, keyboardMode_ = false, pointerArmed_ = false,
	     submitArmed_ = false;
	bool consumesPointer_ = false, consumesNavigation_ = false;
	std::vector<SceneObjectId> clicks_;
	std::vector<UIButtonVisual> visuals_;
};
