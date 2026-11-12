#pragma once

#include "scene/SceneObjectId.h"
#include <filesystem>

class ImGuiSystem;
class EditorPlaySession;

/// <summary>
/// 編集シーンとは独立した主人公・足元のCollider・描画カメラ・Triggerの再生状態と、対象への選択・フレーム操作を表示する
/// </summary>
class PlayStatePanel {
  public:
	/// <summary>
	/// 再生中の物体を選択し、必要に応じてSceneカメラへ収める要求
	/// </summary>
	struct Request {
		SceneObjectId objectId = 0;
		bool frame = false;
	};

	/// <summary>
	/// パネルを開き、次の描画時にフォーカスを移す
	/// </summary>
	void Show();

	/// <summary>
	/// パネルを閉じ、表示要求を初期化する
	/// </summary>
	void Reset();

	/// <summary>
	/// 開いている場合だけ、今回の更新で確定した再生状態を表示する
	/// </summary>
	/// <param name="ui">フレーム開始済みのUI</param>
	/// <param name="session">参照する再生セッション。シーンと時間制御は変更しない</param>
	/// <param name="initialScenePath">Play開始時のシーン。未保存なら空</param>
	/// <returns>選択先のIDとフレーム指定。操作なし・停止中はIDが0</returns>
	Request Draw(const ImGuiSystem &ui, const EditorPlaySession &session,
	             const std::filesystem::path &initialScenePath);

  private:
	bool open_ = false;
	bool focusRequested_ = false;
};
