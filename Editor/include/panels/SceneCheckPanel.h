#pragma once

#include "gameplay/SceneValidation.h"
#include <string>
#include <vector>
class ImGuiSystem;
class Scene;

/// <summary>
/// 編集シーンの再生設定とアセット参照を手動検証し、結果と対象物体への選択・フレーム要求を表示する
/// </summary>
class SceneCheckPanel {
  public:
	/// <summary>
	/// 描画後に実行する、問題のある物体の選択とSceneカメラの移動要求
	/// </summary>
	struct Request {
		std::vector<SceneObjectId> objectIds;
		bool frame = false;
	};

	/// <summary>
	/// パネルを開き、次の描画で現在のシーンを検証する
	/// </summary>
	void Show();

	/// <summary>
	/// 検証結果とパネルの表示状態をクリアする
	/// </summary>
	void Reset();

	/// <summary>
	/// 現在の編集シーンの再生設定・モデル・材質・依存ファイルを検証し、全ての結果をまとめて置き換える
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
	/// <returns>選択する既存物体の一覧とフレーム要求。要求なしは空の一覧</returns>
	Request Draw(const ImGuiSystem &ui, const Scene &scene,
	             const CameraPose &fallback);

  private:
	/// <summary>
	/// 最後の検証結果から、現在も存在する物体を重複せず列挙する
	/// </summary>
	/// <param name="scene">削除済みの物体を除くための現在のシーン</param>
	/// <returns>検証結果に現れる順の物体ID。物体を指さない問題は除く</returns>
	std::vector<SceneObjectId> GetIssueObjects(const Scene &scene) const;

	std::vector<SceneValidationIssue> issues_;
	std::string error_;
	bool open_ = false;
	bool checkRequested_ = false;
	bool focusRequested_ = false;
	bool checked_ = false;
};
