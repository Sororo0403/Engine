#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

/// <summary>
/// 圧縮されていないWAVのサンプル表現
/// </summary>
enum class WaveSampleFormat { PCM8, PCM16, PCM24, PCM32, FLOAT32 };

/// <summary>
/// デバイスに依存しない、モノラルまたはステレオの音声データ
/// </summary>
struct WaveData {
	static constexpr std::size_t kMaximumBytes = 256 * 1024 * 1024;
	std::uint32_t sampleRate = 48000;
	std::uint16_t channels = 1;
	WaveSampleFormat format = WaveSampleFormat::PCM16;
	std::vector<std::uint8_t> samples;

	/// <summary>
	/// サンプルの形式から1チャンネル分のバイト数を取得する
	/// </summary>
	/// <returns>1・2・3・4のいずれか。不正な形式は0</returns>
	std::size_t GetBytesPerSample() const;

	/// <summary>
	/// チャンネル全体を1組としたサンプルフレーム数を取得する
	/// </summary>
	/// <returns>フレーム数。形式・チャンネル数が無効な場合は0</returns>
	std::uint64_t GetFrameCount() const;

	/// <summary>
	/// フレーム数から再生時間を秒で取得する
	/// </summary>
	/// <returns>秒数。サンプリング周波数が0の場合は0</returns>
	double GetDurationSeconds() const;

	/// <summary>
	/// 周波数・チャンネル数・形式・サイズ・サンプル境界と、浮動小数点サンプルの有限性を確認する
	/// </summary>
	/// <returns>true:有効 false:未対応、空、上限超過、途中のフレーム、または非有限</returns>
	bool IsValid() const;
};
