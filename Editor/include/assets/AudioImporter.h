#pragma once

#include <filesystem>
#include <string>

/// <summary>
/// 取り込むWAVと、Assets内の既存フォルダ・保存名を指定する
/// </summary>
struct AudioImportOptions {
	std::filesystem::path source;
	std::filesystem::path assetRoot;
	std::filesystem::path folder;
	std::filesystem::path fileName;
};

/// <summary>
/// WAVを復号検証し、元音声と既存アセットを変更せずAssetsへコピーする
/// </summary>
class AudioImporter {
  public:
	/// <summary>
	/// 読み取った元のバイト列を検証し、新しいWAVファイルとして確定する。シーンや履歴は変更しない
	/// </summary>
	/// <param name="options">入力元・検索先・検索先基準の相対フォルダ・WAV名</param>
	/// <param name="imported">成功時だけ置き換える取り込み先。assetRootが絶対指定なら絶対参照、それ以外は実行ファイル基準</param>
	/// <param name="error">失敗理由。後片付け失敗時は残った場所も含む</param>
	/// <returns>true:取り込み成功 false:無効な音声・パス・同名ファイル・保存失敗</returns>
	/// <remarks>PCM8・16・24・32、IEEE float32のモノラル・ステレオ、8kHzから192kHz、256MiBまで。音声を再符号化しない。空間音声への割り当て時は別途モノラル制約を検証する</remarks>
	static bool Import(const AudioImportOptions &options,
	                   std::filesystem::path &imported, std::string &error);
};
