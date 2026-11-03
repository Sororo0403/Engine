#pragma once

#include <filesystem>
#include <string>

class ImGuiSystem;

enum class GameExportResultAction { NONE, CLOSE };

/// <summary>
/// ゲーム出力の完了後に、出力フォルダを開く操作と単独起動を表示する
/// </summary>
class GameExportResultDialog {
  public:
	/// <summary>
	/// 成功した出力先を保持し、完了画面の表示を予約する
	/// </summary>
	/// <param name="directory">出力済みパッケージの絶対パス</param>
	void Show(std::filesystem::path directory);

	/// <summary>
	/// 出力先を表示し、開く・起動・閉じる操作を受け付ける。失敗時は画面を保持する
	/// </summary>
	/// <param name="ui">フレーム開始済みのUI</param>
	/// <returns>閉じる要求。開く・起動操作の後はNONEで画面を保持する</returns>
	GameExportResultAction Draw(const ImGuiSystem &ui);

  private:
	std::filesystem::path directory_;
	std::string error_;
	bool opening_ = false;
};
