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
	struct Impl;
	std::unique_ptr<Impl> impl_;
};
