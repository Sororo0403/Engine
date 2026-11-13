#pragma once

#include "assets/AssetSources.h"
#include "camera/EditorCameraBookmarks.h"
#include "camera/EditorCameraController.h"
#include "export/GameExporter.h"
#include "gameplay/GameConfig.h"
#include "gizmo/TransformGizmo.h"
#include "graphics/camera/CameraSettings.h"
#include "physics/ColliderBounds.h"
#include "scene/SceneGrid.h"
#include "scene/SceneObjectId.h"

#include <filesystem>
#include <map>
#include <span>
#include <utility>
#include <vector>

/// <summary>
/// シーンとは独立したEditorの表示・ギズモ・素材フォルダ・ゲーム出力設定を保存する
/// </summary>
class EditorPreferences {
  public:
	// Editorのローカル設定に保存するプロジェクト別の配布下書き。
	std::map<std::filesystem::path, GameExportDraft> gameExportDrafts;
	bool showGrid = true;
	bool showSelectionBounds = true;
	bool showCameraGuides = true;
	bool showRespawnGuides = true;
	bool showJumpGuide = false;
	bool showMotionTrail = false;
	bool maximizeGameOnPlay = false;
	bool startPlayPaused = false;
	bool searchProjectScenes = false;
	bool showObjectMarkers = true;
	bool showMeasurements = false;
	ColliderBounds::Mode colliderDisplay = ColliderBounds::Mode::SELECTED;
	bool snapping = false;
	TransformGizmo::Operation operation = TransformGizmo::Operation::TRANSLATE;
	TransformGizmo::Space space = TransformGizmo::Space::LOCAL;
	SelectionPivot::Mode pivotMode = SelectionPivot::Mode::CENTER;
	TransformGizmo::SnapSettings snap;
	EditorCameraController::Settings camera;
	CameraSettings sceneProjection;
	SceneGrid::Settings grid;
	EditorCameraBookmarks cameraBookmarks;

	// タイトル・画面サイズ・操作キー・移動基準・照明だけを保存し、起動シーンとカメラ姿勢は出力時に決める
	GameConfig gameExport;

	std::vector<std::filesystem::path> recentScenes;
	std::filesystem::path lastProject;
	std::vector<std::filesystem::path> recentProjects;
	std::map<std::filesystem::path, std::filesystem::path> projectScenes;
	std::map<std::filesystem::path, EditorCameraController::ViewState> sceneViews;
	AssetSources assets;

	/// <summary>
	/// プロジェクト内の保存済みシーンを、ルート基準で記憶する
	/// </summary>
	void RememberProjectScene(const std::filesystem::path &root,
	                          const std::filesystem::path &scene);

	/// <summary>
	/// 開いたプロジェクトを最近使った順に記憶する
	/// </summary>
	void RememberProject(const std::filesystem::path &root);

	/// <summary>
	/// フォルダまたはproject.jsonが見つからないプロジェクトを履歴から取り除く
	/// </summary>
	/// <param name="activeRoot">編集中のプロジェクト。指定した場合は履歴に残す</param>
	/// <returns>設定を変更した場合はtrue</returns>
	bool RemoveMissingProjects(const std::filesystem::path &activeRoot);

	/// <summary>
	/// プロジェクトごとに成功したゲーム出力先を記憶する
	/// </summary>
	void RememberGameExport(const std::filesystem::path &root,
	                        const std::filesystem::path &directory);

	/// <summary>
	/// 開いた配布先を履歴へ追加する。最後の出力を移動した場合はその場所も更新する
	/// </summary>
	/// <param name="previousDirectory">移動前の出力先。指定した場合は履歴から除く</param>
	/// <returns>true:有効なパスを記録 false:無効なパス</returns>
	bool RememberOpenedGameExport(const std::filesystem::path &root,
	                              const std::filesystem::path &directory,
	                              const std::filesystem::path &previousDirectory = {});

	/// <summary>
	/// 最後の出力先を取得する。プロジェクトなしの場合は空のルートを指定する
	/// </summary>
	std::filesystem::path LastGameExport(const std::filesystem::path &root) const;

	/// <summary>
	/// プロジェクトの配布先を新しい順に取得する
	/// </summary>
	std::span<const std::filesystem::path>
	RecentGameExports(const std::filesystem::path &root) const;

	/// <summary>
	/// 見つからない配布先を、このプロジェクトの履歴から取り除く
	/// </summary>
	/// <returns>履歴を変更した場合はtrue</returns>
	bool RemoveMissingGameExports(const std::filesystem::path &root);

	/// <summary>
	/// 記憶したシーンが現在もプロジェクト内にあれば絶対パスを返す
	/// </summary>
	/// <param name="root">プロジェクトの絶対パス</param>
	/// <param name="unavailable">指定時は、記憶していたシーンを利用できない場合にそのパスを返す</param>
	std::filesystem::path LastProjectScene(
	    const std::filesystem::path &root,
	    std::filesystem::path *unavailable = nullptr) const;

	/// <summary>
	/// Assetsパネルで開いたフォルダをAssets基準の相対パスで記憶する。空はルート
	/// </summary>
	void RememberProjectAssetFolder(const std::filesystem::path &root,
	                                const std::filesystem::path &folder);

	/// <summary>
	/// プロジェクトで最後に開いたAssetsフォルダを取得する。未記録なら空
	/// </summary>
	std::filesystem::path LastProjectAssetFolder(const std::filesystem::path &root) const;

	/// <summary>
	/// 保存済みシーンに対して、Sceneビューの位置と向きを記憶する
	/// </summary>
	void RememberSceneView(const std::filesystem::path &scene,
	                        const EditorCameraController::ViewState &view);

	/// <summary>
	/// シーンの保存済み視点を取得する。未記録ならnullptrを返す
	/// </summary>
	const EditorCameraController::ViewState *
	FindSceneView(const std::filesystem::path &scene) const;

	/// <summary>
	/// 保存済みシーンで選択していたオブジェクトを選択順に記憶する
	/// </summary>
	void RememberSceneSelection(const std::filesystem::path &scene,
	                             std::span<const SceneObjectId> selection);

	/// <summary>
	/// シーンで最後に選択していたオブジェクトを選択順に取得する
	/// </summary>
	std::span<const SceneObjectId>
	LastSceneSelection(const std::filesystem::path &scene) const;

	/// <summary>
	/// 移動に成功した素材の新しいパスをシーンの履歴と編集状態に反映する
	/// </summary>
	void RelocateScenes(std::span<const std::pair<std::filesystem::path,
	                                             std::filesystem::path>> moves);

	/// <summary>
	/// 設定を読み込む。ファイルがない場合は現在の値を保持する
	/// </summary>
	/// <param name="filePath">実行ファイル基準の設定ファイル</param>
	/// <returns>true:成功またはファイルなし false:読み込み失敗。現在の値は保持する</returns>
	bool Load(const std::filesystem::path &filePath = L"config/editor.json");

	/// <summary>
	/// 設定を新規の一時ファイルへ書き込み、完了後だけ置き換える。既存の一時ファイルは保持する
	/// </summary>
	/// <param name="filePath">実行ファイル基準の設定ファイル</param>
	/// <returns>true:保存成功 false:データが無効、または保存失敗</returns>
	bool
	Save(const std::filesystem::path &filePath = L"config/editor.json") const;

	/// <summary>
	/// 保存する設定の値が一致するか確認する
	/// </summary>
	/// <param name="other">比較する設定</param>
	/// <returns>true:一致 false:不一致</returns>
	bool operator==(const EditorPreferences &other) const;

  private:
	std::map<std::filesystem::path, std::filesystem::path> lastGameExports_;
	std::map<std::filesystem::path, std::vector<std::filesystem::path>> recentGameExports_;
	std::map<std::filesystem::path, std::filesystem::path> projectAssetFolders_;
	std::map<std::filesystem::path, std::vector<SceneObjectId>> sceneSelections_;
};
