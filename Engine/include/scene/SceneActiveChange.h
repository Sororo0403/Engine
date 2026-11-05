#pragma once

#include "scene/SceneObjectId.h"

/// <summary>
/// 再生中に変更する、物体自身の有効設定。子自身の設定は上書きしない
/// </summary>
struct SceneActiveChange {
	SceneObjectId objectId = 0;
	bool active = true;
};
