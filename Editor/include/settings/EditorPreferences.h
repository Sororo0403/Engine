#pragma once

#include "gizmo/TransformGizmo.h"
#include "camera/EditorCameraController.h"

#include <filesystem>

/// <summary>
/// シーンとは独立したEditorの表示・ギズモ設定を保存する
/// </summary>
class EditorPreferences {
  public:
	bool showGrid = true;
	bool showSelectionBounds = true;
	bool snapping = false;
	TransformGizmo::Operation operation = TransformGizmo::Operation::TRANSLATE;
	TransformGizmo::SnapSettings snap;
	EditorCameraController::Settings camera;

	/// <summary>
	/// 設定を読み込む。ファイルがない場合は現在の値を保持する
	/// </summary>
	/// <param name="filePath">実行ファイル基準の設定ファイル</param>
	/// <returns>true:成功またはファイルなし false:読み込み失敗。現在の値は保持する</returns>
	bool Load(const std::filesystem::path &filePath = L"config/editor.json");

	/// <summary>
	/// 設定を一時ファイルへ書き込み、成功後に保存先を置き換える
	/// </summary>
	/// <param name="filePath">実行ファイル基準の設定ファイル</param>
	/// <returns>true:保存成功 false:データが無効、または保存失敗</returns>
	bool Save(const std::filesystem::path &filePath = L"config/editor.json") const;

	/// <summary>
	/// 保存する設定の値が一致するか確認する
	/// </summary>
	/// <param name="other">比較する設定</param>
	/// <returns>true:一致 false:不一致</returns>
	bool operator==(const EditorPreferences &other) const;
};
