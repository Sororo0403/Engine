#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace ShellUtils {
/// <summary>
/// 起動するアプリへ指定する初期表示。実際の表示方法は起動先が決める
/// </summary>
enum class WindowVisibility { NORMAL, HIDDEN };

/// <summary>
/// 起動した実行ファイルの終了を待たずに確認する。破棄しても起動先は終了しない
/// </summary>
class ExecutableProcess {
  public:
	ExecutableProcess() = default;
	~ExecutableProcess();
	ExecutableProcess(const ExecutableProcess &) = delete;
	ExecutableProcess &operator=(const ExecutableProcess &) = delete;

	/// <summary>
	/// 起動してプロセスの参照を保持する。COM初期化後に呼ぶ
	/// </summary>
	/// <param name="arguments">Windowsに渡すコマンドライン引数。実行ファイルのパスは含めない</param>
	bool Start(const std::filesystem::path &path, std::string &error,
	           WindowVisibility visibility = WindowVisibility::NORMAL,
	           const std::wstring &arguments = {});

	/// <summary>
	/// 待たずに終了コードを確認する。動作中はexitCodeが空で成功する
	/// </summary>
	bool Poll(std::optional<std::uint32_t> &exitCode, std::string &error) const;

	/// <summary>
	/// 起動したプロセスの参照を保持しているか確認する
	/// </summary>
	bool IsTracking() const { return handle_ != nullptr; }

	/// <summary>
	/// プロセスの参照を解放する
	/// </summary>
	void Reset();

  private:
	void *handle_ = nullptr;
};

/// <summary>
/// 実在するフォルダをWindowsのシェルで開く
/// </summary>
/// <param name="path">絶対パス、または実行ファイル基準のフォルダ</param>
/// <param name="error">失敗理由。成功時は空</param>
/// <returns>true:開く要求に成功 false:パスやシェルの操作が無効</returns>
/// <remarks>呼び出し側でCOMを初期化する。フォルダや内容は作成・変更しない</remarks>
bool OpenDirectory(const std::filesystem::path &path, std::string &error);

/// <summary>
/// 実在するファイルまたはフォルダの親をExplorerで開き、対象を選択する
/// </summary>
/// <remarks>COM初期化済みのスレッドから呼ぶ。パスは絶対または実行ファイル基準</remarks>
bool RevealInExplorer(const std::filesystem::path &path, std::string &error);

/// <summary>
/// 項目をExplorerで選択する。項目がない場合は最も近い既存の親フォルダを開く
/// </summary>
bool RevealLocationInExplorer(const std::filesystem::path &path,
                              std::string &error);

/// <summary>
/// 検証済みの絶対パスにあるファイルまたはフォルダをWindowsのごみ箱へ移動する
/// </summary>
/// <remarks>COM初期化済みのスレッドから呼ぶ。ごみ箱へ移せない場合は失敗を返す</remarks>
bool RecycleAsset(const std::filesystem::path &path, std::string &error);

/// <summary>
/// 実在する.exeを引数なしで独立して起動し、作業ディレクトリをその親フォルダにする
/// </summary>
/// <param name="path">絶対パス、または実行ファイル基準の実行ファイル</param>
/// <param name="error">失敗理由。成功時は空</param>
/// <param name="visibility">起動先へ指定する初期表示</param>
/// <returns>true:起動要求に成功 false:パスや起動処理が無効</returns>
/// <remarks>呼び出し側でCOMを初期化する。起動先の初期化・終了は待たず、終了状態や生存期間は管理しない</remarks>
bool RunExecutable(const std::filesystem::path &path, std::string &error,
                   WindowVisibility visibility = WindowVisibility::NORMAL);
} // namespace ShellUtils
