#pragma once

struct LightingSettings;
struct ShadowSettings;
class ImGuiSystem;

namespace LightingControls {
/// <summary>
/// 平行光・環境光・影の編集項目を描画する
/// </summary>
void Draw(const ImGuiSystem &ui, LightingSettings &lighting);

/// <summary>
/// 平行光源の影の編集項目を描画する
/// </summary>
void DrawShadows(ShadowSettings &shadows);
} // namespace LightingControls
