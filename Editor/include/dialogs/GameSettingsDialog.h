#pragma once

#include "gameplay/GameConfig.h"
#include <string>

class ImGuiSystem;

enum class GameSettingsResult { NONE, CANCELLED, ACCEPTED };

/// <summary>
/// シーンを変更せず、ゲームのタイトル・画面サイズ・操作キー・移動基準を編集する
/// </summary>
class GameSettingsDialog {
  public:
	/// <summary>
	/// 現在の設定から編集用の値を作り、モーダルを開く
	/// </summary>
	/// <param name="config">変更前のゲーム設定</param>
	void Show(const GameConfig &config);

	/// <summary>
	/// 編集用の値を表示し、有効な値の適用か取り消しを受け付ける
	/// </summary>
	/// <param name="ui">フレーム開始済みのUI</param>
	/// <returns>適用・取り消し・編集中</returns>
	GameSettingsResult Draw(const ImGuiSystem &ui);

	/// <summary>
	/// 検証済みの適用値を取得する。編集中や取り消しでは変更しない
	/// </summary>
	/// <returns>ACCEPTED後に使用する設定</returns>
	const GameConfig &GetConfig() const {
		return config_;
	}

  private:
	GameConfig config_;
	GameInputBindings input_;
	bool cameraRelativeMovement_ = false;
	std::string title_, error_;
	int width_ = 1280;
	int height_ = 720;
	bool opening_ = false;
};
