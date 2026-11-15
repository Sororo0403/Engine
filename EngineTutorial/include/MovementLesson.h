#pragma once

class Keyboard;
struct Transform;

/// <summary>
/// WASDの押下状態から、この更新で箱を動かす
/// </summary>
void MoveLessonBox(Transform &box, const Keyboard &keyboard, float seconds);
