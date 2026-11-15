#pragma once
#include "scene/SceneObjectId.h"

class Scene;

/// <summary>
/// 箱、子オブジェクト、床と照明を作り、親の箱のIDを返す
/// </summary>
SceneObjectId CreateLessonBox(Scene &scene);
