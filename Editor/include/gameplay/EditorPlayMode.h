#pragma once

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
	/// <param name="canStart">再生開始を受け付けるか</param>
	/// <returns>ユーザーが要求した操作</returns>
	Action DrawUI(const ImGuiSystem &ui, bool canStart);

	/// <summary>
	/// 編集シーンを変更せずに再生を始める
	/// </summary>
	/// <param name="scene">編集シーン</param>
	/// <param name="error">追加済みコンポーネントの設定や配置エラー</param>
	/// <param name="fallbackPose">Primary Cameraがない場合に使用するSceneビューの姿勢</param>
	/// <param name="config">出力設定。Gameビューの描画幅と高さを使用する</param>
	/// <returns>true:開始成功 false:設定が無効</returns>
	bool Start(const Scene &scene, std::string &error,
	           const CameraPose &fallbackPose = CameraPose{},
	           const GameConfig &config = GameConfig{});

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
	/// <returns>true:成功 false:更新失敗</returns>
	bool Update(float seconds, const InputSystem &input);

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

  private:
	EditorPlaySession session_;
	SceneRenderer renderer_;
	RenderSystem &render_;
	std::unique_ptr<RenderTexture> texture_;
	ViewportInput viewport_{};
	int gameWidth_ = 1280;
	int gameHeight_ = 720;
	bool visible_ = false, focusRequested_ = false, ready_ = false;
};
