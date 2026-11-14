#pragma once

#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

/// <summary>
/// 出力中のファイル操作の失敗と、その操作が対象にしたパスを保持する
/// </summary>
class OutputFileError : public std::runtime_error {
  public:
	OutputFileError(std::string message, std::filesystem::path path, bool readingSource)
	    : std::runtime_error(message), path_(std::move(path)), readingSource_(readingSource) {}

	const std::filesystem::path &GetPath() const { return path_; }
	bool IsReadingSource() const { return readingSource_; }

  private:
	std::filesystem::path path_;
	bool readingSource_;
};

/// <summary>
/// 新規フォルダへ直接出力し、失敗時は識別情報が一致する作成物だけを片付ける
/// </summary>
class OutputDirectory {
  public:
	OutputDirectory() noexcept;
	~OutputDirectory();
	OutputDirectory(const OutputDirectory &) = delete;
	OutputDirectory &operator=(const OutputDirectory &) = delete;

	/// <summary>
	/// 既存項目を変更せず、新しい出力先と不足する親フォルダを作成する
	/// </summary>
	/// <param name="path">事前に検証した絶対パス。既存の出力先は拒否する</param>
	/// <remarks>失敗時はCleanupで作成済みの項目を片付ける。同じインスタンスでは1回だけ呼び出す</remarks>
	void Create(const std::filesystem::path &path);

	/// <summary>
	/// 既存フォルダを出力先に指定する。フォルダ自身は片付けの対象にしない
	/// </summary>
	void OpenExisting(const std::filesystem::path &path);

	/// <summary>
	/// 出力先に新しいサブフォルダを作成する。既存フォルダは拒否する
	/// </summary>
	void CreateSubdirectory(const std::filesystem::path &relative);

	/// <summary>
	/// 通常ファイルを新規出力へコピーする。既存項目や参照元は変更しない
	/// </summary>
	/// <param name="source">通常ファイルの絶対パス</param>
	/// <param name="relative">出力先を基準にした相対ファイルパス</param>
	void Copy(const std::filesystem::path &source,
	          const std::filesystem::path &relative);

	/// <summary>
	/// 文字列を新規出力ファイルへ、そのまま書き込む
	/// </summary>
	/// <param name="relative">出力先を基準にした相対ファイルパス</param>
	/// <param name="text">書き込むバイト列</param>
	void Text(const std::filesystem::path &relative, const std::string &text);

	/// <summary>
	/// 出力を確定し、後片付けの対象から外す
	/// </summary>
	void Complete();

	/// <summary>
	/// 今回作成した項目を逆順に片付ける。他の項目への置き換わりや追加は保持する
	/// </summary>
	/// <returns>成功:空文字列 失敗:残った項目の件数・出力先・理由</returns>
	std::string Cleanup();

  private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};
