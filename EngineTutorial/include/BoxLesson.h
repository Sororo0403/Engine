#pragma once
#include "math/Transform.h"
#include "scene/SceneObjectId.h"

class Scene;

/// <summary>
/// 教材で使う立方体モデルを指定の位置・大きさ・色で作る
/// </summary>
SceneObjectId AddLessonCube(Scene &scene, const char *name,
                            const DirectX::XMFLOAT3 &position,
                            const DirectX::XMFLOAT3 &size,
                            const DirectX::XMFLOAT3 &color);

/// <summary>
/// 箱や足場の形が見えるように平行光を配置する
/// </summary>
void AddLessonLight(Scene &scene);

/// <summary>
/// 箱、子オブジェクト、床と照明を作り、親の箱のIDを返す
/// </summary>
SceneObjectId CreateLessonBox(Scene &scene);
