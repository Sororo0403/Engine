#pragma once

#include "scene/SceneObjectId.h"

#include <string>

/// <summary>
/// 名前の変更対象と、プレビュー時点の変更前後の名前
/// </summary>
struct ObjectRename {
	SceneObjectId objectId = 0;
	std::string before;
	std::string after;
};
