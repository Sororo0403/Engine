#pragma once

/// <summary>
/// Editorのマウス操作とショートカットの一覧を表示する
/// </summary>
class ControlsPanel {
  public:
	/// <summary>
	/// 操作一覧を開き、次の描画時にフォーカスを移す
	/// </summary>
	void Show();

	/// <summary>
	/// 開いている場合に操作一覧のウィンドウを構築する
	/// </summary>
	void Draw();

  private:
	bool open_ = false;
	bool focusRequested_ = false;
};
