#pragma once

#include "gameplay/EditorGamePreview.h"
#include "gameplay/EditorPlayAction.h"
#include "gameplay/EditorPlaySession.h"
#include "gameplay/GameConfig.h"
#include "graphics/ui/ViewportInput.h"
#include "scene/SceneRenderer.h"
#include <memory>

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
	EditorPlayMode(ModelManager &models, RenderSystem &render);
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
	/// <returns>ユーザーが要求した操作</returns>
	Action DrawUI(const ImGuiSystem &ui, bool canStart,
	              const GameConfig &config = GameConfig{},
	              bool canUseSelection = false,
	              const Scene *editingScene = nullptr);

	/// <summary>
	/// 編集シーンを変更せずに再生を始める
	/// </summary>
	/// <param name="scene">編集シーン</param>
	/// <param name="error">追加済みコンポーネントの設定・配置・表示するモデルの読み込みエラー</param>
	/// <param name="fallbackPose">Primary Cameraがない場合に使用するSceneビューの姿勢</param>
	/// <param name="config">出力設定。Gameビューの描画幅・高さ・操作キー・移動基準・照明を開始時に保持する</param>
	/// <param name="invalidObject">指定時は設定やモデル読み込みに問題がある物体IDを設定する。成功時や描画サイズ・操作キーの失敗では0</param>
	/// <param name="cameraOverride">0は通常のPrimary選択。指定時は再生複製内だけ、そのCameraをPrimaryとして使用する</param>
	/// <returns>true:開始成功 false:設定が無効、または表示するモデルを読み込めない</returns>
	bool Start(const Scene &scene, std::string &error,
	           const CameraPose &fallbackPose = CameraPose{},
	           const GameConfig &config = GameConfig{},
	           SceneObjectId *invalidObject = nullptr,
	           SceneObjectId cameraOverride = 0);

	/// <summary>
	/// Play開始時の状態から再実行し、Gameパネルへフォーカスを戻す。一時停止状態は保持する
	/// </summary>
	/// <param name="error">失敗理由。成功時は空</param>
	/// <returns>true:再実行成功 false:未再生または初期化失敗</returns>
	bool Restart(std::string &error);

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
	/// <returns>true:成功 false:更新失敗</returns>
	bool Update(float seconds, const InputSystem &input,
	            bool inputBlocked = false, bool sceneVisible = false);

	/// <summary>
	/// 編集中のGame画像を更新する。非表示時と再生中は何もせず、物理演算は実行しない
	/// </summary>
	/// <param name="scene">全ての編集を反映したシーン</param>
	/// <param name="fallback">Primary Cameraがない場合のSceneビューの姿勢</param>
	/// <returns>true:更新成功または更新不要 false:描画リソースの更新失敗。無効なカメラ設定は空の画像に戻す</returns>
	bool UpdatePreview(const Scene &scene, const CameraPose &fallback);

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
	/// 拡大表示と切り替え予約を解除する。再生状態・配置・選択を保ち、Gameへフォーカスを移さない
	/// </summary>
	void RestoreLayout();

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
	SceneObjectId previewCamera_ = 0;
	EditorGamePreview preview_;
	SceneRenderer renderer_;
	SceneRenderer previewRenderer_;
	RenderSystem &render_;
	ModelManager &models_;
	std::unique_ptr<RenderTexture> texture_;
	ViewportInput viewport_{};
	GameInputBindings inputBindings_;
	LightingSettings lighting_;
	bool cameraRelativeMovement_ = false;
	int gameWidth_ = 1280;
	int gameHeight_ = 720;
	bool visible_ = false, focusRequested_ = false, ready_ = false;
	bool previewFrame_ = false;
	bool maximized_ = false;
	bool maximizeRequested_ = false;
};
