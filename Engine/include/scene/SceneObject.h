#pragma once

#include "math/Transform.h"

#include <filesystem>
#include <string>

/// <summary>
/// シーン上の物体の名前・変形情報・モデルの参照先と表示状態を保持する
/// </summary>
struct SceneObject {
	std::string name = "Object";
	Transform transform{};

	// 空の場合はモデルを持たない物体として扱う
	std::filesystem::path modelPath;

	// falseの場合はモデルを描画せず、Scene上の選択判定にも使用しない
	bool visible = true;
};
