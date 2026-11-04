#pragma once

#include <filesystem>
#include <string>

/// <summary>
/// 取り込む画像と、Assets内の既存フォルダ・保存名を指定する
/// </summary>
struct TextureImportOptions {
	std::filesystem::path source;
	std::filesystem::path assetRoot;
	std::filesystem::path folder;
	std::filesystem::path fileName;
};

/// <summary>
/// 画像データを検証し、元画像と既存アセットを変更せずAssetsへコピーする
/// </summary>
class TextureImporter {
  public:
	/// <summary>
	/// 読み取ったバイト列を復号検証し、新しい画像ファイルとして確定する。シーンや履歴は変更しない
	/// </summary>
	/// <param name="options">入力元・検索先・検索先基準の相対フォルダ・画像名</param>
	/// <param name="imported">成功時だけ置き換える取り込み先。assetRootが絶対指定なら絶対参照、それ以外は実行ファイル基準</param>
	/// <param name="error">失敗理由。後片付け失敗時は残った場所も含む</param>
	/// <returns>true:取り込み成功 false:無効な画像・パス・同名ファイル・保存失敗</returns>
	/// <remarks>COM初期化後に呼び出す。画像は256MiBまで、各辺は16384ピクセルまで。先頭フレームを検証する</remarks>
	static bool Import(const TextureImportOptions &options,
	                   std::filesystem::path &imported, std::string &error);
};
