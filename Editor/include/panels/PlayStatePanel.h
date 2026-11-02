#pragma once

class ImGuiSystem;
class EditorPlaySession;

/// <summary>
/// 編集シーンとは独立した主人公と描画カメラの再生状態を、読み取り専用で表示する
/// </summary>
class PlayStatePanel {
  public:
	/// <summary>
	/// パネルを開き、次の描画時にフォーカスを移す
	/// </summary>
	void Show();

	/// <summary>
	/// パネルを閉じ、表示要求を初期化する
	/// </summary>
	void Reset();

	/// <summary>
	/// 開いている場合だけ、今回の更新で確定した再生状態を表示する
	/// </summary>
	/// <param name="ui">フレーム開始済みのUI</param>
	/// <param name="session">参照する再生セッション。シーンと時間制御は変更しない</param>
	void Draw(const ImGuiSystem &ui, const EditorPlaySession &session);

  private:
	bool open_ = false;
	bool focusRequested_ = false;
};
