#pragma once

#include "dialogs/FileDialog.h"
#include "gameplay/GameConfig.h"
#include <string>

class ImGuiSystem;

enum class GameSettingsResult { NONE, CANCELLED, ACCEPTED, PREPARE_EXPORT };

/// <summary>
/// シーンを変更せず、ゲームのタイトル・画面サイズ・操作キー・ゲームパッド・移動基準・照明を編集する
/// </summary>
class GameSettingsDialog {
  public:
	/// <summary>
	/// 現在の設定から編集用の値を作り、モーダルを開く
	/// </summary>
	/// <param name="config">変更前のゲーム設定</param>
	/// <param name="directory">設定保存の初期フォルダ。空の場合は実行ファイルのフォルダ</param>
	/// <param name="inputAction">検査から移動したときに表示する名前付き操作</param>
	void Show(const GameConfig &config,
	          const std::filesystem::path &directory = {},
	          std::string inputAction = {});

	/// <summary>
	/// 編集用の値と設定の読み込み・保存を表示し、有効な値の適用か取り消しを受け付ける
	/// </summary>
	/// <param name="ui">フレーム開始済みのUI</param>
	/// <param name="retryPlay">設定適用後に呼び出し側が試遊をやり直すか</param>
	/// <param name="canPrepareExport">設定を適用して再出力の準備に進めるか</param>
	/// <returns>適用・再出力の準備・取り消し・編集中</returns>
	GameSettingsResult Draw(const ImGuiSystem &ui, bool retryPlay = false,
	                        bool canPrepareExport = false);

	/// <summary>
	/// 設定の保存に失敗した理由を表示し、編集内容を保持したまま開き直す
	/// </summary>
	void Reopen(std::string error, std::filesystem::path projectFile);

	/// <summary>
	/// 検証済みの適用値を取得する。編集中や取り消しでは変更しない
	/// </summary>
	/// <returns>ACCEPTED後に使用する設定</returns>
	const GameConfig &GetConfig() const {
		return config_;
	}

	/// <summary>
	/// 今回の画面で最後に保存に成功した設定ファイルを取得する
	/// </summary>
	/// <returns>保存成功がない場合は空。次のShowまで保持する</returns>
	const std::filesystem::path &GetLastSavedPath() const {
		return lastSavedPath_;
	}

  private:
	/// <summary>
	/// 起動設定を検証し、起動シーンと予備カメラを除くゲーム設定を下書きへ読み込む
	/// </summary>
	/// <param name="path">読み込むゲーム起動設定の絶対パス</param>
	/// <returns>true:下書きを更新 false:下書きと適用済み設定を保持し、失敗理由を設定する</returns>
	bool LoadConfiguration(const std::filesystem::path &path);

	/// <summary>
	/// 下書きを検証し、有効な場合だけ保存・適用する候補を作る
	/// </summary>
	/// <param name="candidate">成功時だけ設定する、シーン参照と予備カメラを保持した設定</param>
	/// <returns>true:成功 false:候補を保持し、下書きのエラーを設定する</returns>
	bool BuildConfiguration(GameConfig &candidate);

	GameConfig config_;
	GameConfig pendingSave_;
	FileDialog picker_;
	std::filesystem::path source_;
	std::filesystem::path destination_;
	std::filesystem::path lastSavedPath_;
	std::filesystem::path failedProjectFile_;
	GameInputBindings input_;
	std::vector<GameActionBinding> actions_;
	LightingSettings lighting_;
	std::string settingsId_;
	std::vector<std::string> persistentScriptKeys_;
	bool cameraRelativeMovement_ = false;
	bool gamepadEnabled_ = true;
	bool fullscreen_ = false;
	bool verticalSync_ = true;
	float frameTimeLimit_ = 0.0f;
	std::array<float, 3> rigidBodyGravity_{0, -9.8f, 0};
	std::string title_, error_;
	std::string locationError_;
	std::string inputAction_;
	bool focusInputAction_ = false;
	int width_ = 1280;
	int height_ = 720;
	bool opening_ = false;
	bool selecting_ = false;
	bool saving_ = false;
};
