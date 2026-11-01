#pragma once

#include "diagnostics/FrameTimingHistory.h"

class EditorSelection;
class ImGuiSystem;
class Scene;

/// <summary>
/// フレーム間隔の推移と、現在のシーンの物体数・描画サイズを表示する
/// </summary>
class PerformancePanel {
  public:
	/// <summary>
	/// パネルを開き、次の描画時にフォーカスを移す
	/// </summary>
	void Show();

	/// <summary>
	/// パネルと計測状態を初期化する。最初の更新は計測の準備に使用する
	/// </summary>
	void Reset();

	/// <summary>
	/// 一時停止中を除きフレーム間隔を記録する。パネルを閉じている間も記録する
	/// </summary>
	/// <param name="deltaTime">前フレームからの経過時間（秒）</param>
	void Record(float deltaTime);

	/// <summary>
	/// パネルが開いている場合に計測値と現在のシーンの情報を描画する
	/// </summary>
	/// <param name="ui">フレーム開始済みのUI</param>
	/// <param name="scene">物体数を調べるシーン</param>
	/// <param name="selection">現在の選択。シーンに存在する物体だけを数える</param>
	/// <param name="width">Scene描画先の幅。非表示の場合は0</param>
	/// <param name="height">Scene描画先の高さ。非表示の場合は0</param>
	void Draw(const ImGuiSystem &ui, const Scene &scene,
	          const EditorSelection &selection, int width, int height);

  private:
	FrameTimingHistory timing_;
	bool open_ = false;
	bool focusRequested_ = false;
	bool paused_ = false;
	bool firstUpdate_ = true;

	static constexpr double kMaximumGraphMilliseconds = 250.0;
	static constexpr double kMinimumGraphMilliseconds = 1000.0 / 30.0;
	static constexpr float kGraphHeight = 100.0f;
};
