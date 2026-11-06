#pragma once
#include "scene/SceneObjectId.h"

/// <summary>
/// 保存設定と分離した、再生中のボタンの表示状態
/// </summary>
struct UIButtonVisual {
	enum class State { NORMAL, HIGHLIGHTED, PRESSED, DISABLED };
	SceneObjectId object = 0;
	State state = State::NORMAL;
};
