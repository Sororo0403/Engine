#pragma once

#include <filesystem>
#include <string>
#include <vector>

/// <summary>
/// Editorが使用するモデルアセットとフォルダの一覧を管理する
/// </summary>
class AssetDatabase {
  public:
	/// <summary>
	/// アセットを検索するフォルダを指定する。ファイルの検索はまだ行わない
	/// </summary>
	/// <param name="rootDirectory">実行ファイルのフォルダを基準にした相対パス</param>
	explicit AssetDatabase(std::filesystem::path rootDirectory);

	AssetDatabase(const AssetDatabase &) = delete;
	AssetDatabase &operator=(const AssetDatabase &) = delete;

	/// <summary>
	/// 配下のフォルダを含めてモデルの一覧を更新する
	/// </summary>
	/// <returns>true:成功 false:検索失敗。失敗時は一覧を空にする</returns>
	bool Refresh();

	/// <summary>
	/// 検索対象のフォルダを取得する
	/// </summary>
	/// <returns>実行ファイルのフォルダを基準にした相対パス</returns>
	const std::filesystem::path &GetRootDirectory() const;

	/// <summary>
	/// モデルファイルの一覧を取得する
	/// </summary>
	/// <returns>実行ファイルのフォルダを基準にした相対パスの一覧</returns>
	const std::vector<std::filesystem::path> &GetModels() const;

	/// <summary>
	/// 空のフォルダを含む、検索先配下のフォルダ一覧を取得する
	/// </summary>
	/// <returns>実行ファイルのフォルダを基準にした相対パスの一覧。検索先自身は含まない</returns>
	const std::vector<std::filesystem::path> &GetDirectories() const;

	/// <summary>
	/// 最後の検索で発生したエラーを取得する
	/// </summary>
	/// <returns>エラーメッセージ。成功時は空文字列</returns>
	const std::string &GetError() const;

	/// <summary>
	/// 一覧とエラーをクリアする。検索対象のフォルダは保持する
	/// </summary>
	void Clear();

  private:
	std::filesystem::path rootDirectory_;
	std::vector<std::filesystem::path> models_;
	std::vector<std::filesystem::path> directories_;
	std::string error_;
};
