#pragma once

#include "gameplay/CharacterSettings.h"
#include "gameplay/FollowCameraSettings.h"
#include "gameplay/RespawnSettings.h"
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

	// 再生時に入力・重力で動かす物体。衝突形状も必要
	std::optional<CharacterSettings> character;
	// 再生時のカメラ。対象IDは同じシーンの物体を指定する
	std::optional<FollowCameraSettings> followCamera;
	// 落下時の復帰。Character Controllerを持つ物体に使用する
	std::optional<RespawnSettings> respawn;
};
