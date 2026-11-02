#pragma once

#include "assets/AssetDatabase.h"
#include "camera/CameraFrustum.h"
#include "camera/EditorCameraController.h"
#include "dialogs/BatchRenameDialog.h"
#include "dialogs/GameExportDialog.h"
#include "dialogs/GameSettingsDialog.h"
#include "dialogs/ObjectArrayDialog.h"
#include "dialogs/SaveChangesDialog.h"
#include "dialogs/SceneFileDialog.h"
#include "dialogs/SceneReloadDialog.h"
#include "gameplay/EditorPlayMode.h"
#include "gameplay/FollowCameraPreview.h"
#include "gameplay/RespawnPreview.h"
#include "gizmo/TransformGizmo.h"
#include "history/AlignmentAxis.h"
#include "layout/EditorDockLayout.h"
#include "panels/AssetsPanel.h"
#include "panels/ConsolePanel.h"
#include "panels/ControlsPanel.h"
#include "panels/HierarchyPanel.h"
#include "panels/HistoryPanel.h"
#include "panels/InspectorPanel.h"
#include "panels/MaterialPanel.h"
#include "panels/PerformancePanel.h"
#include "panels/PlayStatePanel.h"
#include "panels/SceneCheckPanel.h"
#include "panels/SceneViewPanel.h"
#include "physics/ColliderBounds.h"
#include "physics/ColliderPlacement.h"
#include "scene/IScene.h"
#include "scene/SceneDocument.h"
#include "scene/SceneGrid.h"
#include "scene/SceneRenderer.h"
#include "selection/EditorClipboard.h"
#include "selection/EditorSelection.h"
#include "selection/ObjectMarkers.h"
#include "selection/SelectionBounds.h"
#include "settings/EditorPreferences.h"
#include "settings/RecentScenes.h"

#include <filesystem>
#include <span>
#include <string>

struct BoxCollider;
struct Ray;
struct SceneObject;
struct EngineContext;
struct ViewportInput;

class InputSystem;
class ImGuiSystem;
class ModelManager;

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
	/// メニューやショートカットから要求される編集操作
	/// </summary>
	enum class EditorAction {
		NONE,
		NEW,
		NEW_SAMPLE,
		OPEN,
		SAVE,
		SAVE_AS,
		SAVE_COPY,
		EXPORT_GAME,
		GAME_SETTINGS,
		RELOAD_SCENE,
		EXIT,
		UNDO,
		REDO,
		CREATE_EMPTY,
		CREATE_BOX,
		CREATE_CAMERA,
		DUPLICATE_OBJECTS,
		DUPLICATE_ARRAY,
		DELETE_OBJECTS,
		FRAME_SELECTED,
		FRAME_ALL,
		SELECT_ALL,
		DESELECT_ALL,
		GIZMO_MOVE,
		GIZMO_ROTATE,
		GIZMO_SCALE,
		GIZMO_SPACE,
		GIZMO_PIVOT,
		COPY_OBJECTS,
		CUT_OBJECTS,
		PASTE_OBJECTS,
		RESET_TRANSFORMS,
		SNAP_POSITIONS,
		DROP_TO_GROUND,
		DROP_TO_SURFACE,
		ADD_COLLIDERS,
		FIT_COLLIDERS,
		REMOVE_COLLIDERS,
		PASTE_COLLIDERS,
		PASTE_CAMERA_LENSES,
		PLACE_COLLIDERS_RIGHT,
		PLACE_COLLIDERS_LEFT,
		PLACE_COLLIDERS_ABOVE,
		PLACE_COLLIDERS_BELOW,
		PLACE_COLLIDERS_FRONT,
		PLACE_COLLIDERS_BACK,
		ALIGN_X,
		ALIGN_Y,
		ALIGN_Z,
		DISTRIBUTE_X,
		DISTRIBUTE_Y,
		DISTRIBUTE_Z,
		RESET_CAMERA,
		VIEW_FRONT,
		VIEW_BACK,
		VIEW_LEFT,
		VIEW_RIGHT,
		VIEW_TOP,
		VIEW_BOTTOM,
		RENAME_OBJECT,
		OPEN_RECENT,
		SHOW_ALL_MODELS,
		REVEAL_ACTIVE,
	};

	/// <summary>
	/// カメラへ収める物体の範囲
	/// </summary>
	enum class FrameRequest { NONE, SELECTED, ALL, RESPAWN, COLLIDER };

	/// <summary>
	/// ファイル操作と設定ダイアログの進行状態
	/// </summary>
	enum class DialogStage {
		IDLE,
		CONFIRM_SAVE,
		CONFIRM_MATERIAL_SAVE,
		CONFIRM_MATERIAL_OPEN,
		OPEN_FILE,
		SAVE_FILE,
		SAVE_COPY_FILE,
		EXPORT_GAME,
		GAME_SETTINGS,
		CONFIRM_RELOAD,
		BATCH_RENAME,
		OBJECT_ARRAY,
		ERROR,
	};

	/// <summary>
	/// メニューバーを構築する
	/// </summary>
	/// <returns>選択された操作</returns>
	EditorAction DrawMenuBar();

	/// <summary>
	/// カメラ操作設定のポップアップを描画する
	/// </summary>
	void DrawCameraSettings();

	/// <summary>
	/// グリッドの表示・間隔・表示範囲を設定するUIを構築する
	/// </summary>
	void DrawGridSettings();

	/// <summary>
	/// 編集操作用ショートカットを判定する
	/// </summary>
	/// <param name="objectPanelFocused">HierarchyかSceneにフォーカスがあるか</param>
	/// <returns>要求された操作</returns>
	EditorAction ReadShortcut(bool objectPanelFocused) const;

	/// <summary>
	/// 編集操作を開始する
	/// </summary>
	/// <param name="action">開始する操作</param>
	void StartAction(EditorAction action);

	/// <summary>
	/// 保存確認後に保留していた操作を実行する
	/// </summary>
	void ExecutePendingAction();

	/// <summary>
	/// 保留中のNew・Open・Exitに対してシーンの保存を確認し、変更がなければ操作を続ける
	/// </summary>
	void ConfirmSceneChanges();

	/// <summary>
	/// 材質の保存確認後に保留操作を続ける。ゲーム出力はシーンの保存を要求しない
	/// </summary>
	void ContinueAfterMaterialConfirmation();

	/// <summary>
	/// 割り当てた材質の編集を開始する。別の材質を編集中なら保存を確認する
	/// </summary>
	/// <param name="path">Inspectorから開く材質のパス</param>
	void OpenMaterialForEdit(const std::filesystem::path &path);

	/// <summary>
	/// 保留した材質を読み込み、成功した場合だけ編集対象を切り替える
	/// </summary>
	/// <param name="discardChanges">確認で破棄を選択したか</param>
	void CompleteMaterialOpen(bool discardChanges);

	/// <summary>
	/// シーンを読み込み、成功した場合に選択・カメラ・最近の一覧を更新する
	/// </summary>
	/// <param name="path">読み込むシーンのパス</param>
	void OpenScene(const std::filesystem::path &path);

	/// <summary>
	/// 保存を開始する。保存先がない場合は選択ダイアログを開く
	/// </summary>
	/// <param name="saveAs">保存先を必ず選択するか</param>
	void StartSave(bool saveAs);

	/// <summary>
	/// 現在の保存先と保存地点を保持するコピーの保存先選択を開始する
	/// </summary>
	void StartSaveCopy();

	/// <summary>
	/// 選択物体の原点の平均位置、選択なしの場合はワールド原点を指定方向から見る
	/// </summary>
	/// <param name="view">対象を見る方向</param>
	void SetCameraView(CameraView view);

	/// <summary>
	/// 指定モデルを使用する物体をまとめて選択し、Hierarchyで表示する
	/// </summary>
	/// <param name="path">検索するモデルのパス</param>
	void SelectModelInstances(const std::filesystem::path &path);

	/// <summary>
	/// 指定材質を明示的に割り当てた物体をまとめて選択し、Hierarchyで表示する
	/// </summary>
	/// <param name="path">使用物体を検索する材質ファイル</param>
	void SelectMaterialUsers(const std::filesystem::path &path);

	/// <summary>
	/// 選択物体を直接追従するFollow Cameraをまとめて選択し、Hierarchyで表示する
	/// </summary>
	void SelectFollowingCameras();

	/// <summary>
	/// Sceneビューの位置と向きから固定Cameraを作り、選択とHierarchy表示を更新する
	/// </summary>
	void CreateCameraFromView();

	/// <summary>
	/// 通常の物体を履歴付きで追加し、選択とHierarchy表示を更新する。
	/// モデル付きColliderは読み込んだモデルに合わせ、取得失敗時は追加しない
	/// </summary>
	/// <param name="object">追加する名前・モデル・コンポーネント</param>
	void CreateObject(SceneObject object);

	/// <summary>
	/// モデルに合う衝突形状を設定する。モデルなしは単位ボックスを設定する
	/// </summary>
	/// <param name="id">対象物体ID</param>
	void FitCollider(SceneObjectId id);

	/// <summary>
	/// 選択物体へ一括適用する衝突形状の操作
	/// </summary>
	enum class ColliderOperation { ADD_MISSING, FIT_ALL, REMOVE_ALL };

	/// <summary>
	/// モデルに合うローカル形状を作る。モデルなしは単位ボックスを使用する
	/// </summary>
	/// <param name="object">形状を作る物体</param>
	/// <param name="collider">成功時に設定する形状</param>
	/// <returns>true:取得成功 false:モデルまたは外接ボックスを取得できない</returns>
	bool GetFittedCollider(const SceneObject &object, BoxCollider &collider);

	/// <summary>
	/// 選択全体の衝突形状を準備し、全件成功時だけ1操作として反映する
	/// </summary>
	/// <param name="operation">追加・全件の形状合わせ・削除のいずれか</param>
	void ConfigureSelectedColliders(ColliderOperation operation);

	/// <summary>
	/// コピーしたローカル衝突形状を全選択へ一括適用する。失敗時は全対象を保持する
	/// </summary>
	void PasteCollidersToSelection();

	/// <summary>
	/// コピーしたCameraのレンズを全選択へ1操作で適用する。既存Primary・姿勢・追従設定は保持する
	/// </summary>
	void PasteCameraLensesToSelection();

	/// <summary>
	/// 保存済み材質の同じスロットへの割り当てを、全選択へ1回のUndo操作で適用する
	/// </summary>
	/// <param name="request">材質パスとスロット。空のパスはモデル既定材質へ戻す</param>
	void
	AssignMaterialToSelection(const MaterialPanel::AssignmentRequest &request);

	/// <summary>
	/// 現在のダイアログを描画し、選択結果を処理する
	/// </summary>
	void UpdateDialogs();

	/// <summary>
	/// 操作を中止してエラー表示を予約する
	/// </summary>
	/// <param name="message">表示するメッセージ</param>
	/// <param name="objectId">表示する問題の物体。0は物体選択の操作なし</param>
	void ShowError(std::string message, SceneObjectId objectId = 0);

	/// <summary>
	/// 編集操作を保留しているか取得する
	/// </summary>
	/// <returns>true:操作中 false:操作なし</returns>
	bool IsBusy() const;

	/// <summary>
	/// モデルの読み込みを確認してからシーンへ配置する
	/// </summary>
	/// <param name="modelPath">実行ファイル基準のモデルの相対パス</param>
	/// <param name="position">配置するワールド座標</param>
	/// <returns>true:配置成功 false:読み込みまたは追加に失敗</returns>
	bool PlaceModel(const std::filesystem::path &modelPath,
	                const DirectX::XMFLOAT3 &position = {});

	/// <summary>
	/// ドラッグ先の面とモデルの外接ボックスから配置位置を求め、1回の履歴操作で追加する
	/// </summary>
	/// <param name="modelPath">配置するモデルの参照先</param>
	/// <param name="ray">Sceneビュー上のドロップ位置から作成したレイ</param>
	/// <returns>true:配置成功 false:位置の取得・読み込み・追加に失敗</returns>
	bool PlaceDroppedModel(const std::filesystem::path &modelPath,
	                       const Ray &ray);

	/// <summary>
	/// 読み込みを確認してから、物体のモデル参照だけを変更する
	/// </summary>
	/// <param name="objectId">変更する物体のID</param>
	/// <param name="modelPath">変更先のモデル。空の場合は参照を解除する</param>
	/// <returns>true:成功 false:物体がない、読み込みまたは変更に失敗</returns>
	bool AssignModel(SceneObjectId objectId,
	                 const std::filesystem::path &modelPath);

	/// <summary>
	/// 表示するモデルの読み込みを確認し、表示状態をまとめて変更する
	/// </summary>
	/// <param name="objectIds">変更する物体のID一覧</param>
	/// <param name="visible">変更後の表示状態</param>
	/// <returns>true:成功 false:読み込みまたは変更に失敗</returns>
	bool ChangeVisibility(std::span<const SceneObjectId> objectIds,
	                      bool visible);

	/// <summary>
	/// 非表示の物体を全て再表示する。選択状態は変更しない
	/// </summary>
	void ShowAllModels();

	/// <summary>
	/// 編集を確定し、履歴パネルで選択した状態へ戻す
	/// </summary>
	/// <param name="index">履歴の開始地点を0とした移動先</param>
	void RestoreHistory(std::size_t index);

	/// <summary>
	/// Inspectorに表示中の物体へのTransform・Camera操作を反映する
	/// </summary>
	/// <param name="request">Transform操作、Camera追加、Primary切り替えなどの要求</param>
	void ApplyInspectorRequest(const InspectorPanel::ObjectRequest &request);

	/// <summary>
	/// 編集中の内容を履歴へ確定し、ギズモ操作を中止する
	/// </summary>
	void FinishEdit();

	/// <summary>
	/// 選択した支点の設定で、複数編集用の変形情報を作成する
	/// </summary>
	/// <param name="transform">成功した場合に設定する変形情報</param>
	/// <returns>true:成功 false:複数選択ではない、または対象が無効</returns>
	bool MakeSelectionTransform(Transform &transform) const;

	/// <summary>
	/// Sceneビューで確定したクリックまたは範囲選択を反映する
	/// </summary>
	/// <param name="request">画像内の正規化した選択操作</param>
	/// <param name="view">更新済みのビュー行列</param>
	/// <param name="projection">更新済みの投影行列</param>
	/// <param name="runtimeScene">再生シーンと観察用の選択を使用するか。falseは編集シーン</param>
	/// <returns>true:選択変更あり false:変更なし、または無効な範囲</returns>
	bool ApplySceneSelection(const SceneViewPanel::SelectionRequest &request,
	                         const DirectX::XMFLOAT4X4 &view,
	                         const DirectX::XMFLOAT4X4 &projection,
	                         bool runtimeScene = false);

	/// <summary>
	/// 選択物体またはシーン全体へカメラを寄せる操作を判定する
	/// </summary>
	/// <param name="viewport">Sceneビューの入力状態</param>
	/// <param name="hierarchyFocused">Hierarchyにフォーカスがあるか</param>
	/// <returns>FキーまたはShift＋Fキーで要求された表示範囲</returns>
	FrameRequest ReadFrameShortcut(const ViewportInput &viewport,
	                               bool hierarchyFocused) const;

	/// <summary>
	/// モデル・Collider・空の物体の範囲が収まる位置へ編集用カメラを移動する
	/// </summary>
	/// <param name="request">選択物体・シーン全体・要求されたコンポーネント。物体のフレーミングは非表示モデルを除く</param>
	/// <param name="projection">Sceneビューの更新済み投影行列</param>
	/// <param name="runtimeScene">再生シーンの現在の配置を使用するか。falseは編集シーン</param>
	/// <returns>true:カメラを移動した false:対象なし、または移動できない</returns>
	bool FrameObjects(FrameRequest request,
	                  const DirectX::XMFLOAT4X4 &projection,
	                  bool runtimeScene = false);

	/// <summary>
	/// 再生シーンを独立したSceneカメラで更新する。編集や再生入力には反映しない
	/// </summary>
	/// <param name="deltaTime">Sceneカメラの経過時間。Pause中もカメラ操作には使用する</param>
	/// <param name="action">メニューから要求されたフレーミング操作</param>
	/// <param name="hierarchyFocused">再生用Hierarchyにフォーカスがあるか</param>
	/// <returns>true:成功または非表示 false:Scene描画の更新失敗</returns>
	bool UpdatePlaySceneView(float deltaTime, EditorAction action,
	                         bool hierarchyFocused = false);

	/// <summary>
	/// 観察用の選択・未確定のScene選択・Hierarchy表示をクリアし、次回のPlayで編集時の選択を複製し直す
	/// </summary>
	void ClearPlaySelection();

	/// <summary>
	/// 選択中の全物体を複製し、複製した物体を選択する
	/// </summary>
	/// <remarks>呼び出し前にFinishEditを呼び出す</remarks>
	void DuplicateSelectedObjects();

	/// <summary>
	/// 選択中の全物体を削除し、選択を解除する
	/// </summary>
	/// <remarks>呼び出し前にFinishEditを呼び出す</remarks>
	void DeleteSelectedObjects();

	/// <summary>
	/// 選択物体をEditor内へコピーし、必要に応じて元の物体を削除する
	/// </summary>
	/// <param name="cut">コピー成功後に元の物体を削除するか</param>
	void CopySelectedObjects(bool cut);

	/// <summary>
	/// コピーした物体を1操作として貼り付け、作成した物体を選択する
	/// </summary>
	void PasteObjects();

	/// <summary>
	/// 選択した全物体のTransformを初期値へ戻す
	/// </summary>
	void ResetSelectedTransforms();

	/// <summary>
	/// 選択物体の原点を、ギズモの移動スナップ間隔で個別に丸めて1操作として記録する
	/// </summary>
	void SnapSelectedPositions();

	/// <summary>
	/// 表示中の選択モデルの最下端をY=0へ合わせ、全選択物体を同じ距離だけ移動する
	/// </summary>
	void DropSelectionToGround();

	/// <summary>
	/// 選択した全Colliderを、配置関係を保って真下の別Collider上面へ下ろす。全変更を1操作として記録する
	/// </summary>
	void DropSelectionToSurface();

	/// <summary>
	/// アクティブ物体を基準にし、残りの選択Colliderを指定側の面へまとめて配置する。1操作として記録する
	/// </summary>
	/// <param name="face">基準物体に対するワールド座標の方向</param>
	void PlaceSelectionNextToActive(ColliderPlacement::Face face);

	/// <summary>
	/// 選択した全物体の名前を保持し、一括リネームのプレビューを開く
	/// </summary>
	void StartBatchRename();

	/// <summary>
	/// 選択物体を等間隔で複製するダイアログを開始する
	/// </summary>
	void StartObjectArray();

	/// <summary>
	/// 選択物体の原点を、アクティブ物体の指定軸の座標へそろえる
	/// </summary>
	/// <param name="axis">ワールド座標のX・Y・Zのいずれか</param>
	void AlignSelection(AlignmentAxis axis);

	/// <summary>
	/// 選択物体の原点を指定軸の方向で等間隔に配置する。両端は固定する
	/// </summary>
	/// <param name="axis">ワールド座標のX・Y・Zのいずれか</param>
	void DistributeSelection(AlignmentAxis axis);

	/// <summary>
	/// 選択に、表示中でモデルを持つ物体があるか確認する
	/// </summary>
	/// <returns>true:有効な全選択の中に対象あり false:対象なし、または選択が無効</returns>
	bool HasVisibleSelectedModel() const;

	/// <summary>
	/// シーンに存在する物体が1つ以上選択されているか確認する
	/// </summary>
	/// <returns>true:有効な選択あり false:未選択、または存在しないIDあり</returns>
	bool HasSelection() const;

	/// <summary>
	/// シーンに存在する物体が1つだけ選択されているか確認する
	/// </summary>
	/// <returns>true:単一選択 false:未選択、複数選択、または物体なし</returns>
	bool HasSingleSelection() const;

  private:
	const ImGuiSystem &ui_;
	const InputSystem &input_;
	ModelManager &models_;

	// 描画側の参照より先に宣言する
	SceneDocument document_;
	SceneRenderer renderer_;
	EditorPlayMode playMode_;
	SceneRenderer playSceneRenderer_;
	bool playFrame_ = false;
	bool playSceneReady_ = false;

	EditorDockLayout dockLayout_;

	bool resetDockLayout_ = false;
	bool showGrid_ = true;
	bool showSelectionBounds_ = true;
	bool showCameraGuides_ = true;
	bool showRespawnGuides_ = true;
	bool showObjectMarkers_ = true;
	// フォーカス復帰直後の押しっぱなしを、再生操作の押下として扱わない。
	bool playInputWasActive_ = false;

	EditorPreferences preferences_;
	bool canSavePreferences_ = false;
	bool cameraSettingsRequested_ = false;
	bool gridSettingsRequested_ = false;
	RecentScenes recentScenes_;
	std::filesystem::path recentOpenPath_;
	FrameRequest frameRequest_ = FrameRequest::NONE;
	SceneObjectId frameObject_ = 0;
	bool hierarchySelectionScope_ = false;

	HierarchyPanel hierarchyPanel_;
	InspectorPanel inspectorPanel_;
	SceneViewPanel sceneViewPanel_;
	EditorCameraController cameraController_;

	MaterialPanel materialPanel_;
	AssetDatabase assets_{L"resources/editor"};
	AssetsPanel assetsPanel_;

	EditorSelection selection_;
	EditorSelection playSelection_;
	HierarchyPanel playHierarchyPanel_;
	bool playSelectionActive_ = false;
	FrameRequest playFrameRequest_ = FrameRequest::NONE;
	EditorClipboard clipboard_;

	SaveChangesDialog saveChangesDialog_;
	BatchRenameDialog batchRenameDialog_;
	ObjectArrayDialog objectArrayDialog_;
	SceneFileDialog fileDialog_;
	GameExportDialog exportDialog_;
	GameSettingsDialog gameSettingsDialog_;
	GameConfig exportConfig_;
	SceneReloadDialog sceneReloadDialog_;

	DialogStage stage_ = DialogStage::IDLE;
	EditorAction pendingAction_ = EditorAction::NONE;
	std::filesystem::path pendingMaterialPath_;

	std::string errorMessage_;
	SceneObjectId errorObjectId_ = 0;
	bool errorOpenRequested_ = false;

	// ほかのダイアログ中の終了要求も、操作完了まで保持する
	bool exitQueued_ = false;
	bool closeApproved_ = false;

	TransformGizmo gizmo_;
	bool gizmoEditActive_ = false;

	// 複数選択用のギズモと、ドラッグ開始時点の変形情報
	Transform selectionGizmoTransform_;
	Transform selectionGizmoStart_;

	SelectionBounds selectionBounds_;
	ObjectMarkers objectMarkers_;
	ColliderBounds colliderBounds_;
	ColliderBounds::Mode colliderDisplay_ = ColliderBounds::Mode::SELECTED;
	RespawnPreview respawnPreview_;
	CameraFrustum cameraFrustum_;
	FollowCameraPreview followCameraPreview_;

	SceneGrid sceneGrid_;
	ControlsPanel controlsPanel_;
	SceneCheckPanel sceneCheckPanel_;
	ConsolePanel consolePanel_;
	HistoryPanel historyPanel_;
	PerformancePanel performancePanel_;
	PlayStatePanel playStatePanel_;
};
