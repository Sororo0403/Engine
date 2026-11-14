#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
class ImGuiSystem;
struct ImGuiInputTextCallbackData;

/// <summary>
/// LuaアセットをEditor内で作成・構文確認・編集し、成功時だけ保存する
/// </summary>
class ScriptEditorDialog {
  public:
	enum class Result { NONE, SAVED, SAVED_AND_PLAY, CANCELLED };
	/// <summary>
	/// Assetsの既存フォルダへ作成する新規コードの下書きを準備する
	/// </summary>
	/// <param name="assets">アセット検索先</param>
	/// <param name="folder">検索先基準の既存フォルダ</param>
	/// <param name="error">失敗理由</param>
	/// <returns>true:表示予約成功 false:フォルダが無効</returns>
	bool ShowCreate(const std::filesystem::path &assets,
	                const std::filesystem::path &folder, std::string &error);
	/// <summary>
	/// 既存Luaファイルを読み、編集の下書きを準備する。まだ保存しない
	/// </summary>
	/// <param name="path">実行ファイル基準または絶対参照</param>
	/// <param name="error">失敗理由</param>
	/// <param name="diagnostic">開いたときに表示位置を合わせるLuaエラー</param>
	/// <param name="diagnosticPath">診断に記録された移動前のパス。空なら編集先のパスを使う</param>
	/// <returns>true:表示予約成功 false:読み込み失敗</returns>
	bool ShowEdit(const std::filesystem::path &path, std::string &error,
	              std::string_view diagnostic = {},
	              const std::filesystem::path &diagnosticPath = {});
	/// <summary>
	/// 作成・編集ダイアログを表示する。構文エラーと外部変更時は元ファイルを保持する
	/// </summary>
	/// <param name="ui">フレーム開始済みのUI</param>
	/// <param name="playScene">保存後の試遊先。空パスは編集中のシーン、nullptrは試遊なし</param>
	/// <returns>確定した操作。表示中はNONE</returns>
	Result Draw(const ImGuiSystem &ui, const std::filesystem::path *playScene = nullptr);

	/// <summary>
	/// 保存完了したファイルを取得する
	/// </summary>
	/// <returns>ShowEditの参照、または作成先の検索先と同じ指定方式の参照</returns>
	const std::filesystem::path &GetSavedPath() const {
		return saved_;
	}

  private:
	enum class Confirmation { NONE, DISCARD, RELOAD };

	/// <summary>
	/// ディスク上のLuaを読み直し、成功時にだけ下書きと保存時の比較元を更新する
	/// </summary>
	void ReloadFromDisk();

	/// <summary>
	/// コード欄で行移動または次の検索結果への移動を実行する
	/// </summary>
	static int NavigateCode(ImGuiInputTextCallbackData *data);

	std::filesystem::path assets_, folder_, path_, saved_;
	std::string name_, source_, original_, error_;
	std::uint32_t inputRevision_ = 0;
	int pendingLine_ = 0;
	int targetLine_ = 1;
	std::string search_;
	bool findRequested_ = false;
	bool searchMissed_ = false;
	bool opening_ = false, creating_ = false;
	bool saveConflict_ = false;
	Confirmation confirmation_ = Confirmation::NONE;
};
