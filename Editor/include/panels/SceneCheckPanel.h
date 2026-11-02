#pragma once

#include "gameplay/SceneValidation.h"
#include <string>
#include <vector>
class ImGuiSystem;
class Scene;

/// <summary>
/// 編集シーンの再生設定を手動検証し、結果と対象物体への選択要求を表示する
/// </summary>
class SceneCheckPanel {
  public:
	/// <summary>
	/// パネルを開き、次の描画で現在のシーンを検証する
	/// </summary>
	void Show();

	/// <summary>
	/// 検証結果とパネルの表示状態をクリアする
	/// </summary>
	void Reset();

	/// <summary>
	/// 現在の編集シーンを検証し、全ての結果をまとめて置き換える
	/// </summary>
	/// <param name="scene">変更しない検証対象</param>
	/// <param name="fallback">Primary Cameraがない場合のSceneカメラ姿勢</param>
	void Check(const Scene &scene, const CameraPose &fallback);

	/// <summary>
	/// 最後の検証結果と再検証ボタンを描画する。シーンは変更しない
	/// </summary>
	/// <param name="ui">フレーム開始済みのUI</param>
	/// <param name="scene">検証対象と選択先を確認する編集シーン</param>
	/// <param name="fallback">現在のSceneカメラ姿勢</param>
	/// <returns>選択を要求する既存物体ID。要求なしは0</returns>
	SceneObjectId Draw(const ImGuiSystem &ui, const Scene &scene,
	                   const CameraPose &fallback);

  private:
	std::vector<SceneValidationIssue> issues_;
	std::string error_;
	bool open_ = false;
	bool checkRequested_ = false;
	bool focusRequested_ = false;
	bool checked_ = false;
};
