#pragma once

#include "gameplay/EditorGamePreview.h"
#include "gameplay/EditorPlayAction.h"
#include "gameplay/EditorPlaySession.h"
#include "gameplay/GameActionReader.h"
#include "gameplay/GameConfig.h"
#include "graphics/ui/ViewportInput.h"
#include "scene/SceneAudioPlayer.h"
#include "scene/SceneRenderer.h"
#include <memory>
#include <optional>

class RenderTexture;
class RenderSystem;
class ImGuiSystem;
class InputSystem;

/// <summary>
/// Gameパネルと独立した再生シーンの描画・入力を管理する
/// </summary>
class EditorPlayMode {
  public:
	/// <summary>
	/// Gameパネルから要求する再生・再実行・停止・描画エラーの通知
	/// </summary>
	using Action = EditorPlayAction;

	/// <summary>
	/// 共通描画機能を参照し、再生シーンを保持する
	/// </summary>
	/// <param name="models">モデル管理</param>
	/// <param name="render">描画機能</param>
	/// <param name="clips">音声データ管理</param>
	/// <param name="audio">共有音声基盤</param>
	EditorPlayMode(ModelManager &models, RenderSystem &render,
	               AudioClipManager &clips, AudioSystem &audio);
	~EditorPlayMode();

	/// <summary>
	/// 再生用の描画ワールドを作成する
	/// </summary>
	/// <returns>true:成功 false:作成失敗</returns>
	bool Initialize();

	/// <summary>
	/// Gameパネルを構築して再生・再実行・停止の要求を取得する
	/// </summary>
	/// <param name="ui">UI機能</param>
	/// <param name="canStart">再生開始と速度変更を受け付けるか</param>
	/// <param name="config">編集中のプレビューに使用する描画幅・高さ・照明。再生中は開始時の設定を保持する</param>
	/// <param name="canUseSelection">再生中の選択物体を使った停止操作を許可するか</param>
	/// <param name="editingScene">指定時は停止中のプレビューCamera名を表示するために参照する。保持しない</param>
	/// <param name="editingScenePath">編集中のシーンのパス。未保存の場合は空</param>
	/// <param name="startupScenePath">プロジェクトの起動シーンのパス。未設定の場合は空</param>
	/// <param name="playSourcePath">停止後に編集できる実行元シーン。なければ空</param>
	/// <param name="canRevealPlayObject">選択中の物体が実行元シーンに存在するか</param>
	/// <param name="lastPlayScenePath">直前に試遊したシーン。未保存なら空</param>
	/// <param name="lastPlayCameraOverride">直前の試遊で指定したカメラ。通常の試遊なら0</param>
	/// <returns>ユーザーが要求した操作</returns>
	Action DrawUI(const ImGuiSystem &ui, bool canStart,
	              const GameConfig &config = GameConfig{},
	              bool canUseSelection = false,
	              const Scene *editingScene = nullptr,
	              const std::filesystem::path &editingScenePath = {},
	              const std::filesystem::path &startupScenePath = {},
	              const std::filesystem::path &playSourcePath = {},
	              bool canRevealPlayObject = false,
	              const std::filesystem::path &lastPlayScenePath = {},
	              SceneObjectId lastPlayCameraOverride = 0);

	/// <summary>
	/// 編集シーンを変更せずに再生を始める
	/// </summary>
	/// <param name="scene">編集シーン</param>
	/// <param name="error">追加済みコンポーネントの設定・配置・表示するモデルの読み込みエラー</param>
	/// <param name="fallbackPose">Primary Cameraがない場合に使用するSceneビューの姿勢</param>
	/// <param name="config">出力設定。Gameビューの描画幅・高さ・操作キー・移動基準・照明を開始時に保持する</param>
	/// <param name="invalidObject">指定時は設定やモデル読み込みに問題がある物体IDを設定する。成功時や描画サイズ・操作キーの失敗では0</param>
	/// <param name="cameraOverride">0は通常のPrimary選択。指定時は再生複製内だけ、そのCameraをPrimaryとして使用する</param>
	/// <param name="invalidInputAction">指定時は修正が必要な名前付き入力を返す。該当しない場合は空</param>
	/// <param name="failedTestSave">テスト用セーブの読み込みに失敗した場合の場所。場所が不明な場合は空のパス</param>
	/// <param name="failedModelFile">モデルの読み込みエラーで確認するファイルの場所</param>
	/// <param name="skipTestSave">今回の試遊だけテスト用セーブを読み込まない</param>
	/// <returns>true:開始成功 false:設定が無効、または表示するモデルを読み込めない</returns>
	bool Start(const Scene &scene, std::string &error,
	           const CameraPose &fallbackPose = CameraPose{},
	           const GameConfig &config = GameConfig{},
	           SceneObjectId *invalidObject = nullptr,
	           SceneObjectId cameraOverride = 0,
	           std::string *invalidInputAction = nullptr,
	           std::optional<std::filesystem::path> *failedTestSave = nullptr,
	           std::filesystem::path *failedModelFile = nullptr,
	           bool skipTestSave = false);

	/// <summary>
	/// 指定したシーンから再実行し、Gameパネルへフォーカスを戻す。一時停止状態は保持する
	/// </summary>
	/// <param name="error">失敗理由。成功時は空</param>
	/// <param name="currentScene">true:現在のシーン false:Play開始時のシーン</param>
	/// <param name="failedScript">指定時は原因のLuaファイルを返す</param>
	/// <param name="invalidObject">指定時は再実行に失敗した原因の物体IDを返す。対象がなければ0</param>
	/// <returns>true:再実行成功 false:未再生または初期化失敗</returns>
	bool Restart(std::string &error, bool currentScene = false,
	             std::filesystem::path *failedScript = nullptr,
	             SceneObjectId *invalidObject = nullptr);

	/// <summary>
	/// 再生中の一時停止と再開を切り替える
	/// </summary>
	void TogglePause();

	/// <summary>
	/// 一時停止中の1フレーム実行を予約する
	/// </summary>
	void RequestStep();

	/// <summary>
	/// 再生中の一時停止状態を取得する
	/// </summary>
	/// <returns>true:一時停止中 false:通常再生または停止中</returns>
	bool IsPaused() const;

	/// <summary>
	/// 再生シーンを破棄する。編集シーンとUndo履歴は保持する
	/// </summary>
	void Stop();

	/// <summary>
	/// Gameにフォーカスがある場合だけ操作入力を渡し、再生とGPUデータを更新する
	/// </summary>
	/// <param name="seconds">経過秒数</param>
	/// <param name="input">ゲーム用の共通入力</param>
	/// <param name="inputBlocked">Sceneの操作など、別の画面へ入力を渡しているか</param>
	/// <param name="sceneVisible">独立したSceneビューが表示されているか。Game非表示時も再生時間を進める</param>
	/// <param name="invalidObject">指定時は更新に失敗したスクリプトの物体ID。特定できなければ0</param>
	/// <param name="suspended">ダイアログを表示している間、試遊の時間を止めるか</param>
	/// <returns>true:成功 false:更新失敗</returns>
	bool Update(float seconds, const InputSystem &input,
	            bool inputBlocked = false, bool sceneVisible = false,
	            SceneObjectId *invalidObject = nullptr, bool suspended = false);

	/// <summary>
	/// 編集中のGame画像を更新する。非表示時と再生中は何もせず、物理演算は実行しない
	/// </summary>
	/// <param name="scene">全ての編集を反映したシーン</param>
	/// <param name="fallback">Primary Cameraがない場合のSceneビューの姿勢</param>
	/// <returns>true:更新成功または更新不要 false:描画リソースの更新失敗。無効なカメラ設定は空の画像に戻す</returns>
	bool UpdatePreview(const Scene &scene, const CameraPose &fallback);

	/// <summary>
	/// Gameのプレビューと試遊で使用するCanvas画像を読み直す
	/// </summary>
	bool ReloadCanvasImage(const std::filesystem::path &path, std::string &error);

	/// <summary>
	/// 停止中のGame表示で使用するCameraを指定する。PlayとシーンのPrimaryは変更しない
	/// </summary>
	/// <param name="objectId">表示するCamera。0は通常のPrimaryへ戻す。対象の存在は描画更新時に確認する</param>
	/// <returns>true:指定成功 false:再生中。以前の指定は保持する</returns>
	bool SetPreviewCamera(SceneObjectId objectId);

	/// <summary>
	/// 停止中のプレビューに指定したCameraを取得する
	/// </summary>
	/// <returns>指定したID。0は通常のPrimary。シーンの切り替えや対象の削除時には解除する</returns>
	SceneObjectId GetPreviewCamera() const {
		return previewCamera_;
	}

	/// <summary>
	/// 更新したGame画像を描画する
	/// </summary>
	void Draw() const;

	/// <summary>
	/// GPU利用完了後に描画先と再生用ワールドを解放する
	/// </summary>
	void Finalize();

	/// <summary>
	/// 再生状態を取得する
	/// </summary>
	/// <returns>true:再生中 false:停止中</returns>
	bool IsPlaying() const {
		return session_.IsPlaying();
	}

	/// <summary>
	/// 状態表示用に再生セッションを取得する
	/// </summary>
	/// <returns>この再生モードの生存中に有効な読み取り専用のセッション</returns>
	const EditorPlaySession &GetSession() const;

	/// <summary>
	/// 状態表示用に、この再生シーンが所有する音声の状態を取得する
	/// </summary>
	/// <returns>この再生モードの生存中に有効な音声セッション</returns>
	const SceneAudioPlayer &GetAudioPlayer() const {
		return audioPlayer_;
	}

	/// <summary>
	/// シミュレーションまたはシーン音声の最後の更新エラーを取得する
	/// </summary>
	/// <returns>失敗理由。成功時は空</returns>
	const std::string &GetLastUpdateError() const {
		return lastUpdateError_.empty() ? session_.GetLastUpdateError()
		                                : lastUpdateError_;
	}

	/// <summary>
	/// 拡大表示と切り替え予約を解除する。再生状態・配置・選択を保ち、Gameへフォーカスを移さない
	/// </summary>
	void RestoreLayout();

	/// <summary>
	/// 次の新規PlayでGameを自動拡大するか設定する。現在の再生・配置とRestartには影響しない
	/// </summary>
	/// <param name="enabled">true:新規Playの次の描画から拡大 false:通常の配置で開始</param>
	void SetMaximizeOnPlay(bool enabled) {
		maximizeOnPlay_ = enabled;
	}

	/// <summary>
	/// 新規Play時の自動拡大設定を取得する
	/// </summary>
	/// <returns>true:自動拡大が有効 false:無効</returns>
	bool GetMaximizeOnPlay() const {
		return maximizeOnPlay_;
	}

	/// <summary>
	/// 次の新規Playを初期化直後から一時停止するか設定する
	/// </summary>
	/// <param name="enabled">true:一時停止 false:通常再生</param>
	void SetStartPaused(bool enabled) {
		startPaused_ = enabled;
	}

	/// <summary>
	/// 新規Playを一時停止で始める設定を取得する
	/// </summary>
	/// <returns>true:一時停止で開始 false:通常再生で開始</returns>
	bool GetStartPaused() const {
		return startPaused_;
	}

	/// <summary>
	/// Gameだけを作業領域へ拡大して表示しているか取得する。描画解像度とドック配置は変更しない
	/// </summary>
	/// <returns>true:再生中の拡大表示 false:通常の配置</returns>
	bool IsMaximized() const {
		return maximized_;
	}

	/// <summary>
	/// 再生開始時、または停止中のプレビューに使用する照明を取得する
	/// </summary>
	/// <returns>この再生モードが保持する照明</returns>
	const LightingSettings &GetLighting() const {
		return lighting_;
	}

  private:
	EditorPlaySession session_;
	SceneAudioPlayer audioPlayer_;
	std::string lastUpdateError_;
	SceneObjectId previewCamera_ = 0;
	EditorGamePreview preview_;
	SceneRenderer renderer_;
	SceneRenderer previewRenderer_;
	RenderSystem &render_;
	ModelManager &models_;
	std::unique_ptr<RenderTexture> texture_;
	ViewportInput viewport_{};
	GameInputBindings inputBindings_;
	std::vector<GameActionBinding> actionBindings_;
	GameActionReader actionReader_;
	LightingSettings lighting_;
	bool cameraRelativeMovement_ = false;
	bool gamepadEnabled_ = true;
	int gameWidth_ = 1280;
	int gameHeight_ = 720;
	bool visible_ = false, focusRequested_ = false, ready_ = false;
	bool previewFrame_ = false;
	bool maximized_ = false;
	bool maximizeRequested_ = false;
	bool maximizeOnPlay_ = false;
	bool startPaused_ = false;
	bool loadTestSave_ = false;
};
