#include "assets/AudioImporter.h"

#include "assets/AssetFolders.h"
#include "audio/WaveReader.h"
#include "platform/windows/FileUtils.h"
#include "platform/windows/PathUtils.h"
#include <fstream>
#include <stdexcept>

bool AudioImporter::Import(const AudioImportOptions &options,
                           std::filesystem::path &imported,
                           std::string &error) {
	try {
		if (options.source.empty() ||
		    options.source.native().find(L'\0') != std::wstring::npos ||
		    !PathUtils::HasExtension(options.source, ".wav") ||
		    !PathUtils::IsValidFileName(options.fileName) ||
		    !PathUtils::HasExtension(options.fileName, ".wav")) {
			throw std::invalid_argument("Choose a WAV audio file and a valid "
			                            "file name ending in .wav.");
		}

		const auto folder =
		    AssetFolders::Resolve(options.assetRoot, options.folder);
		const auto destination = folder / options.fileName;

		if (std::filesystem::exists(destination)) {
			throw std::runtime_error(
			    "An asset with that name already exists. Choose another name.");
		}

		const auto source = PathUtils::ResolveFromExecutable(options.source);
		std::ifstream file(source, std::ios::binary | std::ios::ate);
		const auto size = file.tellg();

		if (!file || size < 12 ||
		    size > static_cast<std::streamoff>(WaveData::kMaximumBytes)) {
			throw std::runtime_error(
			    "The WAV file cannot be read or exceeds 256 MiB.");
		}

		std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
		file.seekg(0);

		if (!file.read(reinterpret_cast<char *>(bytes.data()),
		               static_cast<std::streamsize>(bytes.size())) ||
		    file.peek() != std::char_traits<char>::eof()) {
			throw std::runtime_error(
			    "The WAV file changed or could not be read completely.");
		}

		{
			WaveData wave;

			if (!WaveReader::Decode(bytes, wave, error)) {
				return false;
			}
		}

		auto selected =
		    AssetFolders::MakeReference(options.assetRoot, destination);

		if (!FileUtils::WriteBytesAtomically(
		        destination, std::as_bytes(std::span{bytes}), error, false)) {
			return false;
		}

		imported.swap(selected);
		error.clear();
		return true;
	} catch (const std::exception &exception) {
		error = exception.what();
		return false;
	}
}
