#pragma once

struct LightingSettings;
class ImGuiSystem;

namespace LightingControls {
/// <summary>
/// 平行光・環境光・影の編集項目を描画する
/// </summary>
void Draw(const ImGuiSystem &ui, LightingSettings &lighting);
} // namespace LightingControls
