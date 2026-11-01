#pragma once

#include "math/Transform.h"
#include "physics/BoxCollider.h"

#include <filesystem>
#include <optional>
#include <string>

/// <summary>
/// シーン上の物体の名前・変形情報・描画と衝突の設定を保持する
/// </summary>
struct SceneObject {
	std::string name = "Object";
	Transform transform{};

	// 空の場合はモデルを持たない物体として扱う
	std::filesystem::path modelPath;

	// falseの場合はモデルを描画せず、Scene上の選択判定にも使用しない
	bool visible = true;

	// 指定した物体は衝突判定に使用する。未指定の場合は衝突形状を持たない
	std::optional<BoxCollider> collider;
};
