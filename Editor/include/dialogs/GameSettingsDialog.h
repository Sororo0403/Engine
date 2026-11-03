#pragma once

#include "dialogs/FileDialog.h"
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
	/// 編集用の値と出力ゲームからの読み込みを表示し、有効な値の適用か取り消しを受け付ける
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
	/// <summary>
	/// 起動設定を検証し、タイトル・画面サイズ・操作キー・移動基準だけを下書きへ読み込む
	/// </summary>
	/// <param name="path">読み込むゲーム起動設定の絶対パス</param>
	/// <returns>true:下書きを更新 false:下書きと適用済み設定を保持し、失敗理由を設定する</returns>
	bool LoadConfiguration(const std::filesystem::path &path);

	GameConfig config_;
	FileDialog picker_;
	std::filesystem::path source_;
	GameInputBindings input_;
	bool cameraRelativeMovement_ = false;
	std::string title_, error_;
	int width_ = 1280;
	int height_ = 720;
	bool opening_ = false;
	bool selecting_ = false;
};
