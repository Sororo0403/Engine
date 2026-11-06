#pragma once
#include "scene/SceneObjectId.h"
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
class Scene;
class ImGuiSystem;
class EditorHistory;
struct SceneObject;

/// <summary>
/// Lua Scriptの割り当て・初期状態・名前付き物体参照を編集する
/// </summary>
class ScriptInspector {
  public:
	/// <summary>
	/// 編集の下書きと、このフレームのスクリプト編集要求を解除する
	/// </summary>
	void Clear();

	/// <summary>
	/// 下書きを保持し、このフレームのコード編集要求だけを解除する
	/// </summary>
	void ResetRequest() {
		edit_.clear();
	}

	/// <summary>
	/// スクリプトの設定を、確定ごとに履歴へ記録する
	/// </summary>
	/// <param name="ui">フレーム開始済みのUI</param>
	/// <param name="scene">参照先候補を取得するシーン</param>
	/// <param name="id">編集する物体ID</param>
	/// <param name="object">編集する物体</param>
	/// <param name="history">設定を記録する履歴</param>
	/// <param name="scripts">割り当て可能なLuaファイル</param>
	/// <returns>true:設定変更あり false:変更なし</returns>
	bool Draw(const ImGuiSystem &ui, const Scene &scene, SceneObjectId id,
	          SceneObject &object, EditorHistory &history,
	          std::span<const std::filesystem::path> scripts);
	/// <summary>
	/// 再生中の設定と、物体ごとの現在のstateを変更せず表示する
	/// </summary>
	/// <param name="ui">表示するUI</param>
	/// <param name="object">再生物体</param>
	/// <param name="state">実行状態。未初期化の場合はnullopt</param>
	void DrawReadOnly(const ImGuiSystem &ui, const SceneObject &object,
	                  std::optional<std::string_view> state) const;

	/// <summary>
	/// このフレームにコード編集を要求したアセットを取得する
	/// </summary>
	/// <returns>対象パス。次のDrawまたはClearで解除する</returns>
	const std::filesystem::path &GetEditRequest() const {
		return edit_;
	}

  private:
	SceneObjectId draftObject_ = 0;
	std::string initial_, draft_, referenceName_ = "target", error_;
	std::filesystem::path edit_;
};
