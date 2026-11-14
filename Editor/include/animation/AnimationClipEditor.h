#pragma once

#include "animation/AnimationClipComponent.h"
#include <string>
#include <vector>

class ImGuiSystem;
class Scene;

/// <summary>
/// Assetsから開いたクリップの編集と保存を管理する
/// </summary>
class AnimationClipEditor {
  public:
	enum class Result { NONE, SAVED, CANCELLED };

	void ShowCreate(const std::filesystem::path &directory);
	bool ShowEdit(const std::filesystem::path &path, const Scene &editingScene,
	              std::string &error);
	Result Draw(const ImGuiSystem &ui);
	const std::filesystem::path &GetSavedPath() const { return path_; }

	/// <summary>
	/// シーン内の旧クリップまたは、割り当て枠を使うアセットのキーを編集する
	/// </summary>
	static void DrawKeys(const ImGuiSystem &ui, const Scene *scene, AnimationClipComponent &clip,
	                     float &playhead);

  private:
	AnimationClipComponent clip_, original_;
	std::filesystem::path path_, directory_;
	std::vector<float> markerTimes_;
	std::string name_, error_;
	float playhead_ = 0;
	bool openRequested_ = false;
};
