#pragma once

#include "scene/SceneObjectId.h"
#include <filesystem>
#include <span>
#include <string>

class Scene;

/// <summary>
/// ゲーム内のButtonからLuaのコード欄を開き、一時的なシーンで実行する
/// </summary>
class GameScriptEditor {
  public:
	void OpenClickedScript(const Scene &scene, std::span<const SceneObjectId> clicks);
	void Draw(const Scene &scene);
	void Reset();
	bool IsOpen() const;

  private:
	void Run(const Scene &scene);

	SceneObjectId object_ = 0;
	std::filesystem::path path_;
	std::string starter_;
	std::string code_;
	std::string result_;
	bool loaded_ = false;
	bool opening_ = false;
	bool open_ = false;
};
