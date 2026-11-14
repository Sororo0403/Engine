#pragma once

#include "gameplay/GameActionReader.h"
#include "gameplay/GameConfig.h"
#include "gameplay/SceneSimulation.h"
#include "scene/IScene.h"
#include "scene/SceneAudioPlayer.h"
#include "scene/SceneRenderer.h"
#include <chrono>

struct EngineContext;
class InputSystem;
class RenderSystem;

/// <summary>
/// 出力されたシーンをEditorなしで更新・描画する
/// </summary>
class RuntimeScene final : public IScene {
  public:
	/// <summary>
	/// 共通機能と起動設定を保持する
	/// </summary>
	/// <param name="context">初期化済みの共通機能</param>
	/// <param name="config">検証済みのゲーム起動設定</param>
	/// <param name="launchedFromEditor">Editorの配布結果から試遊を開始したか</param>
	RuntimeScene(const EngineContext &context, GameConfig config, bool launchedFromEditor);

	/// <summary>
	/// 出力されたシーンを読み込み、実行と描画を初期化する
	/// </summary>
	/// <returns>true:成功 false:ファイル・設定・描画の初期化失敗</returns>
	bool Initialize() override;

	/// <summary>
	/// アクティブ時に共通入力とゲーム時間を反映し、描画を更新する
	/// </summary>
	/// <param name="seconds">経過秒数</param>
	/// <returns>true:成功 false:実行または描画更新失敗</returns>
	bool Update(float seconds) override;

	/// <summary>
	/// バックバッファへゲームを描画する
	/// </summary>
	void Draw() const override;

	/// <summary>
	/// GPUの利用完了後に実行・描画用データを解放する
	/// </summary>
	void Finalize() override;

	/// <summary>
	/// Escによる終了要求を確認する
	/// </summary>
	/// <returns>true:終了する false:継続する</returns>
	bool IsCloseApproved() const override;

  private:
	/// <summary>
	/// 保存対象の共有データに変更があればユーザー設定を保存する
	/// </summary>
	void SaveScriptSettings();

	const InputSystem &input_;
	RenderSystem &render_;
	GameConfig config_;
	bool launchedFromEditor_ = false;
	GameActionReader actionReader_;
	SceneSimulation simulation_;
	SceneRenderer renderer_;
	SceneAudioPlayer audioPlayer_;
	std::filesystem::path scriptSettingsPath_;
	std::string observedScriptData_;
	std::string savedScriptSettings_;
	std::chrono::steady_clock::time_point nextScriptSettingsWrite_{};
	bool close_ = false;
};
