#pragma once

#include "audio/AudioClipHandle.h"
#include <filesystem>
#include <memory>
#include <string>

/// <summary>
/// デバイスに依存しないWAVデータを読み込み、正規化したパスごとに共有する。メインスレッドから使用する
/// </summary>
class AudioClipManager {
  public:
	AudioClipManager();
	~AudioClipManager();
	AudioClipManager(const AudioClipManager &) = delete;
	AudioClipManager &operator=(const AudioClipManager &) = delete;

	/// <summary>
	/// WAVを読み込む。読み込み済みのパスは、大小文字の違いによらず同じ参照を返す
	/// </summary>
	/// <param name="path">絶対パスまたは実行ファイル基準の相対パス</param>
	/// <param name="error">成功時は空、失敗時は理由</param>
	/// <returns>成功:音声の共有参照 失敗:空</returns>
	AudioClipHandle Load(const std::filesystem::path &path, std::string &error);

	/// <summary>
	/// キャッシュを使用せず読み直す。成功時だけキャッシュを置き換え、取得済み参照と再生中の音は保持する
	/// </summary>
	/// <param name="path">読み直すWAV</param>
	/// <param name="error">成功時は空、失敗時は理由</param>
	/// <returns>成功:新しい共有参照 失敗:空。以前のキャッシュは保持する</returns>
	AudioClipHandle Reload(const std::filesystem::path &path,
	                       std::string &error);

	/// <summary>
	/// 検証済みの音声を移動して不変の共有データにする。ファイルキャッシュには登録しない
	/// </summary>
	/// <param name="data">サンプルを持つ音声。大きなデータにはstd::moveを使用する</param>
	/// <param name="error">成功時は空、失敗時は理由</param>
	/// <returns>成功:新しい共有参照 失敗:空</returns>
	AudioClipHandle Create(WaveData data, std::string &error);

	/// <summary>
	/// ファイルキャッシュを解除する。取得済みの共有参照と再生中の音は保持する
	/// </summary>
	void Clear();

  private:
	/// <summary>
	/// キャッシュ使用の有無を指定して共通のファイル読み込みを行う
	/// </summary>
	/// <param name="path">WAVのパス</param>
	/// <param name="reload">true:既存データを読み直す false:読み込み済みなら共有する</param>
	/// <param name="error">成功時は空、失敗時は理由</param>
	/// <returns>成功:共有参照 失敗:空</returns>
	AudioClipHandle LoadFile(const std::filesystem::path &path, bool reload,
	                         std::string &error);

	struct Impl;
	std::unique_ptr<Impl> impl_;
};
