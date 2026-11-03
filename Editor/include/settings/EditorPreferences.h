#pragma once

#include "assets/AssetSources.h"
#include "camera/EditorCameraBookmarks.h"
#include "camera/EditorCameraController.h"
#include "gameplay/GameConfig.h"
#include "gizmo/TransformGizmo.h"
#include "physics/ColliderBounds.h"
#include "scene/SceneGrid.h"

#include <filesystem>
#include <vector>

/// <summary>
/// シーンとは独立したEditorの表示・ギズモ・素材フォルダ・ゲーム出力設定を保存する
/// </summary>
class EditorPreferences {
  public:
	bool showGrid = true;
	bool showSelectionBounds = true;
	bool showCameraGuides = true;
	bool showRespawnGuides = true;
	bool showJumpGuide = false;
	bool showObjectMarkers = true;
	bool showMeasurements = false;
	ColliderBounds::Mode colliderDisplay = ColliderBounds::Mode::SELECTED;
	bool snapping = false;
	TransformGizmo::Operation operation = TransformGizmo::Operation::TRANSLATE;
	TransformGizmo::Space space = TransformGizmo::Space::LOCAL;
	SelectionPivot::Mode pivotMode = SelectionPivot::Mode::CENTER;
	TransformGizmo::SnapSettings snap;
	EditorCameraController::Settings camera;
	SceneGrid::Settings grid;
	EditorCameraBookmarks cameraBookmarks;

	// タイトル・画面サイズ・操作キー・移動基準・照明だけを保存し、起動シーンとカメラ姿勢は出力時に決める
	GameConfig gameExport;

	std::vector<std::filesystem::path> recentScenes;
	AssetSources assets;

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
};
