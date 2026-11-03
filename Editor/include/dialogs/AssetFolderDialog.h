#pragma once

#include <filesystem>
#include <string>
class ImGuiSystem;

enum class AssetFolderResult { NONE, CANCELLED, ACCEPTED };

/// <summary>
/// 現在のAssetsフォルダに作成する、新しいフォルダの名前を指定する
/// </summary>
class AssetFolderDialog {
  public:
	/// <summary>
	/// 作成先を保持し、ダイアログの表示を予約する
	/// </summary>
	/// <param name="assetRoot">Assetsの検索先</param>
	/// <param name="parent">検索先基準の親フォルダ。空は検索先自身</param>
	void Show(const std::filesystem::path &assetRoot,
	          const std::filesystem::path &parent);

	/// <summary>
	/// 名前の入力とフォルダの新規作成を表示する。失敗時は入力と画面を保持する
	/// </summary>
	/// <param name="ui">フレーム開始済みのUI</param>
	/// <returns>入力中・取消・新規作成完了</returns>
	AssetFolderResult Draw(const ImGuiSystem &ui);

	/// <summary>
	/// 新しく作成したフォルダの参照を取得する
	/// </summary>
	/// <returns>ACCEPTED後に使用できる検索先基準のフォルダ</returns>
	const std::filesystem::path &GetCreatedDirectory() const;

  private:
	std::filesystem::path root_, parent_, created_;
	std::string name_, error_;
	bool opening_ = false;
};
