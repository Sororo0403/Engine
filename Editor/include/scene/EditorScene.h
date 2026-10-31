#pragma once

#include "camera/EditorCameraController.h"
#include "dialogs/SaveChangesDialog.h"
#include "dialogs/SceneFileDialog.h"
#include "graphics/ui/DockLayout.h"
#include "panels/HierarchyPanel.h"
#include "panels/InspectorPanel.h"
#include "panels/SceneViewPanel.h"
#include "scene/IScene.h"
#include "scene/SceneDocument.h"
#include "scene/SceneRenderer.h"

#include <string>

struct EngineContext;

class InputSystem;
class ImGuiSystem;

/// <summary>
/// シーンの編集と、ファイル操作の進行を管理する
/// </summary>
class EditorScene final : public IScene {
  public:
	/// <summary>
	/// Editorが使用する共通機能を受け取る
	/// </summary>
	/// <param name="context">初期化済みの共通機能</param>
	explicit EditorScene(const EngineContext &context);

	~EditorScene() override;

	EditorScene(const EditorScene &) = delete;
	EditorScene &operator=(const EditorScene &) = delete;

	/// <summary>
	/// 編集用のシーンと描画機能を初期化する
	/// </summary>
	/// <returns>true:成功 false:失敗</returns>
	bool Initialize() override;

	/// <summary>
	/// 編集UIとダイアログを更新し、描画データを反映する
	/// </summary>
	/// <param name="deltaTime">前フレームからの経過時間。単位は秒</param>
	/// <returns>true:成功 false:更新失敗</returns>
	bool Update(float deltaTime) override;

	/// <summary>
	/// 編集中のシーンを描画する
	/// </summary>
	void Draw() const override;

	/// <summary>
	/// 編集用リソースを解放する。GPUの利用完了後に呼び出す
	/// </summary>
	void Finalize() override;

	/// <summary>
	/// 終了確認を予約する。UIはUpdateで構築する
	/// </summary>
	/// <returns>false:終了を保留する</returns>
	bool RequestClose() override;

	/// <summary>
	/// 保存確認を終えて終了可能になったか取得する
	/// </summary>
	/// <returns>true:終了可能 false:終了しない</returns>
	bool IsCloseApproved() const override;

	/// <summary>
	/// ファイル名と変更状態を含むタイトルを取得する
	/// </summary>
	/// <returns>UTF-8のタイトル</returns>
	std::string GetWindowTitle() const override;

  private:
	/// <summary>
	/// メニューやショートカットから要求される操作
	/// </summary>
	enum class FileAction {
		NONE,
		NEW,
		OPEN,
		SAVE,
		SAVE_AS,
		EXIT,
	};

	/// <summary>
	/// ファイル操作の進行状態
	/// </summary>
	enum class DialogStage {
		IDLE,
		CONFIRM_SAVE,
		OPEN_FILE,
		SAVE_FILE,
		ERROR,
	};

	/// <summary>
	/// メニューバーを構築する
	/// </summary>
	/// <returns>選択された操作</returns>
	FileAction DrawMenuBar();

	/// <summary>
	/// ファイル操作用ショートカットを判定する
	/// </summary>
	/// <returns>要求された操作</returns>
	FileAction ReadShortcut() const;

	/// <summary>
	/// ファイル操作を開始する
	/// </summary>
	/// <param name="action">開始する操作</param>
	void StartAction(FileAction action);

	/// <summary>
	/// 保存確認後に保留していた操作を実行する
	/// </summary>
	void ExecutePendingAction();

	/// <summary>
	/// 保存を開始する。保存先がない場合は選択ダイアログを開く
	/// </summary>
	/// <param name="saveAs">保存先を必ず選択するか</param>
	void StartSave(bool saveAs);

	/// <summary>
	/// 現在のダイアログを描画し、選択結果を処理する
	/// </summary>
	void UpdateDialogs();

	/// <summary>
	/// 操作を中止してエラー表示を予約する
	/// </summary>
	/// <param name="message">表示するメッセージ</param>
	void ShowError(std::string message);

	/// <summary>
	/// 編集操作を保留しているか取得する
	/// </summary>
	/// <returns>true:操作中 false:操作なし</returns>
	bool IsBusy() const;

  private:
	const ImGuiSystem &ui_;
	const InputSystem &input_;

	// 描画側の参照より先に宣言する
	SceneDocument document_;
	SceneRenderer renderer_;

	DockLayout dockLayout_{
	    "EditorDockSpace", "Hierarchy", "Scene", "Inspector", 0.20f, 0.25f,
	};

	bool resetDockLayout_ = false;

	HierarchyPanel hierarchyPanel_;
	InspectorPanel inspectorPanel_;
	SceneViewPanel sceneViewPanel_;
	EditorCameraController cameraController_;

	SceneObjectId selectedObjectId_ = 0;

	SaveChangesDialog saveChangesDialog_;
	SceneFileDialog fileDialog_;

	DialogStage stage_ = DialogStage::IDLE;
	FileAction pendingAction_ = FileAction::NONE;

	std::string errorMessage_;
	bool errorOpenRequested_ = false;

	// ほかのダイアログ中の終了要求も、操作完了まで保持する
	bool exitQueued_ = false;
	bool closeApproved_ = false;
};
