#pragma once
#include "scene/SceneObjectId.h"
#include <filesystem>
#include <span>
class ImGuiSystem;
class EditorHistory;
struct SceneObject;

/// <summary>
/// 粒子の発生・移動・画像・色を編集し、連続編集を1回のUndoへまとめる
/// </summary>
class ParticleEmitterInspector {
  public:
	/// <summary>
	/// 発生設定を表示し、画像の選択・ドロップと数値の編集を行う
	/// </summary>
	/// <param name="ui">フレーム開始済みのUI</param>
	/// <param name="id">編集する発生元</param>
	/// <param name="object">履歴と同じシーンの物体</param>
	/// <param name="history">同じシーンの履歴</param>
	/// <param name="textures">Assetsに登録された画像</param>
	/// <returns>true:変更あり false:変更なし</returns>
	bool Draw(const ImGuiSystem &ui, SceneObjectId id, SceneObject &object,
	          EditorHistory &history,
	          std::span<const std::filesystem::path> textures = {}) const;

	/// <summary>
	/// 再生中の保存設定を、履歴を変更せず表示する
	/// </summary>
	/// <param name="ui">フレーム開始済みのUI</param>
	/// <param name="object">再生シーンの物体</param>
	void DrawReadOnly(const ImGuiSystem &ui, const SceneObject &object) const;
};
