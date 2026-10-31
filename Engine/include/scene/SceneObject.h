#pragma once

#include "graphics/Transform.h"

#include <filesystem>
#include <string>

/// <summary>
/// シーン上の物体の名前・変形情報・モデルの参照先を保持する
/// </summary>
struct SceneObject {
	std::string name = "Object";
	Transform transform{};

	// 空の場合はモデルを持たない物体として扱う
	std::filesystem::path modelPath;
};
