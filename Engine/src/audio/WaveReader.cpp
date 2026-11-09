#include "audio/WaveReader.h"

#include "platform/windows/PathUtils.h"
#include <algorithm>
#include <array>
#include <exception>
#include <fstream>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace {
/// <summary>
/// 範囲を確認済みの2バイトをリトルエンディアンで読み取る
/// </summary>
/// <param name="bytes">読み取る範囲</param>
/// <param name="offset">2バイトを持つ位置</param>
/// <returns>符号なし整数</returns>
std::uint16_t Read16(std::span<const std::uint8_t> bytes, std::size_t offset) {
	return std::uint16_t(bytes[offset]) |
	       (std::uint16_t(bytes[offset + 1]) << 8);
}

/// <summary>
/// 範囲を確認済みの4バイトをリトルエンディアンで読み取る
/// </summary>
/// <param name="bytes">読み取る範囲</param>
/// <param name="offset">4バイトを持つ位置</param>
/// <returns>符号なし整数</returns>
std::uint32_t Read32(std::span<const std::uint8_t> bytes, std::size_t offset) {
	return std::uint32_t(Read16(bytes, offset)) |
	       (std::uint32_t(Read16(bytes, offset + 2)) << 16);
}

/// <summary>
/// 範囲を確認済みのチャンクIDを比較する
/// </summary>
/// <param name="bytes">読み取る範囲</param>
/// <param name="offset">IDを持つ位置</param>
/// <param name="id">4文字の識別名</param>
/// <returns>true:一致 false:別のチャンク</returns>
bool HasId(std::span<const std::uint8_t> bytes, std::size_t offset,
           std::string_view id) {
	return std::equal(id.begin(), id.end(), bytes.begin() + offset);
}

/// <summary>
/// fmtチャンクの非圧縮形式とフレーム境界を検証する
/// </summary>
/// <param name="format">fmtチャンクの内容</param>
/// <param name="data">サンプル表現の出力先</param>
void ReadFormat(std::span<const std::uint8_t> format, WaveData &data) {
	if (format.size() < 16 || format.size() == 17 ||
	    (format.size() >= 18 && Read16(format, 16) > format.size() - 18)) {
		throw std::runtime_error("The WAV fmt chunk is truncated.");
	}

	auto tag = Read16(format, 0);
	data.channels = Read16(format, 2);
	data.sampleRate = Read32(format, 4);
	const auto bits = Read16(format, 14);

	if (tag == 0xfffe) {
		// EXTENSIBLEでは、コンテナと有効ビット数が同じ標準PCM/floatだけを受け入れる。
		constexpr std::array<std::uint8_t, 12> guidTail{
		    0, 0, 0x10, 0, 0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71};
		if (format.size() < 40 || Read16(format, 16) < 22 ||
		    Read16(format, 18) != bits ||
		    !std::equal(guidTail.begin(), guidTail.end(),
		                format.begin() + 28) ||
		    Read16(format, 26) != 0) {
			throw std::runtime_error(
			    "The extensible WAV format is unsupported or incomplete.");
		}

		const auto mask = Read32(format, 20);

		if (mask != 0 && mask != (data.channels == 1 ? 4u : 3u)) {
			throw std::runtime_error("Only mono-center or stereo-left/right "
			                         "WAV channel layouts are supported.");
		}

		tag = Read16(format, 24);
	}

	if (tag == 1 && bits == 8)
		data.format = WaveSampleFormat::PCM8;
	else if (tag == 1 && bits == 16)
		data.format = WaveSampleFormat::PCM16;
	else if (tag == 1 && bits == 24)
		data.format = WaveSampleFormat::PCM24;
	else if (tag == 1 && bits == 32)
		data.format = WaveSampleFormat::PCM32;
	else if (tag == 3 && bits == 32)
		data.format = WaveSampleFormat::FLOAT32;
	else
		throw std::runtime_error(
		    "Use uncompressed PCM 8/16/24/32-bit or IEEE float32 WAV audio.");
	const auto block = data.GetBytesPerSample() * data.channels;
	if ((data.channels != 1 && data.channels != 2) || data.sampleRate < 8000 ||
	    data.sampleRate > 192000 || Read16(format, 12) != block ||
	    Read32(format, 8) != data.sampleRate * block) {
		throw std::runtime_error("The WAV channel count, sample rate, block "
		                         "alignment or byte rate is invalid.");
	}
}
} // namespace

bool WaveReader::Decode(std::span<const std::uint8_t> bytes, WaveData &data,
                        std::string &error) {
	error.clear();

	try {
		if (bytes.size() < 12 || bytes.size() > WaveData::kMaximumBytes ||
		    !HasId(bytes, 0, "RIFF") || !HasId(bytes, 8, "WAVE")) {
			throw std::runtime_error(
			    "Use a RIFF/WAVE file no larger than 256 MiB.");
		}

		const auto declared = std::uint64_t(Read32(bytes, 4)) + 8;

		if (declared < 12 || declared > bytes.size()) {
			throw std::runtime_error("The WAV RIFF extent is invalid.");
		}

		bytes = bytes.first(static_cast<std::size_t>(declared));
		std::span<const std::uint8_t> format, samples;
		bool foundFormat = false, foundData = false;

		for (std::size_t offset = 12; offset < bytes.size();) {
			if (bytes.size() - offset < 8) {
				throw std::runtime_error("The WAV chunk header is truncated.");
			}

			const auto length = Read32(bytes, offset + 4);
			const std::uint64_t padded = std::uint64_t(length) + (length & 1u);

			if (padded > bytes.size() - offset - 8) {
				throw std::runtime_error(
				    "The WAV chunk or its alignment byte exceeds RIFF.");
			}

			const auto chunk = bytes.subspan(offset + 8, length);

			if (HasId(bytes, offset, "fmt ")) {
				if (foundFormat)
					throw std::runtime_error(
					    "The WAV has duplicate fmt chunks.");
				format = chunk;
				foundFormat = true;
			} else if (HasId(bytes, offset, "data")) {
				if (foundData)
					throw std::runtime_error(
					    "The WAV has duplicate data chunks.");
				samples = chunk;
				foundData = true;
			}

			offset += 8 + static_cast<std::size_t>(padded);
		}

		if (!foundFormat || !foundData || samples.empty()) {
			throw std::runtime_error(
			    "The WAV needs one fmt chunk and nonempty data.");
		}

		WaveData next;
		ReadFormat(format, next);
		next.samples.assign(samples.begin(), samples.end());

		if (!next.IsValid()) {
			throw std::runtime_error("The WAV samples contain incomplete "
			                         "frames or non-finite float values.");
		}

		data = std::move(next);
		return true;
	} catch (const std::exception &exception) {
		error = exception.what();
		return false;
	}
}

bool WaveReader::Load(const std::filesystem::path &path, WaveData &data,
                      std::string &error) {
	error.clear();

	try {
		if (path.empty() || !PathUtils::HasExtension(path, ".wav")) {
			throw std::runtime_error("Choose a WAV audio file.");
		}

		const auto resolved = PathUtils::ResolveFromExecutable(path);
		std::ifstream file(resolved, std::ios::binary | std::ios::ate);
		const auto size = file.tellg();

		if (!file || size < 12 ||
		    size > static_cast<std::streamoff>(WaveData::kMaximumBytes)) {
			throw std::runtime_error(
			    "The WAV file cannot be read or exceeds 256 MiB.");
		}

		std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
		file.seekg(0);

		if (!file.read(reinterpret_cast<char *>(bytes.data()),
		               static_cast<std::streamsize>(bytes.size()))) {
			throw std::runtime_error(
			    "The WAV file changed or could not be read completely.");
		}

		return Decode(bytes, data, error);
	} catch (const std::exception &exception) {
		error = exception.what();
		return false;
	}
}
