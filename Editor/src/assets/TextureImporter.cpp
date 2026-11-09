#include "assets/TextureImporter.h"
#include "assets/AssetFolders.h"
#include "assets/ImageValidation.h"

#include "platform/windows/FileUtils.h"
#include "platform/windows/PathUtils.h"
#include <stdexcept>

bool TextureImporter::Import(const TextureImportOptions &options,
                             std::filesystem::path &imported,
                             std::string &error) {
	try {
		if (options.source.empty() ||
		    options.source.native().find(L'\0') != std::wstring::npos ||
		    !PathUtils::IsImageFile(options.source) ||
		    !PathUtils::IsValidFileName(options.fileName) ||
		    !PathUtils::HasExtension(
		        options.fileName,
		        PathUtils::ToUtf8(options.source.extension()))) {
			throw std::invalid_argument("Choose an image and a valid file name "
			                            "with the same extension.");
		}

		const auto folder =
		    AssetFolders::Resolve(options.assetRoot, options.folder);

		const auto destination = folder / options.fileName;

		if (std::filesystem::exists(destination)) {
			throw std::runtime_error(
			    "An asset with that name already exists. Choose another name.");
		}

		const auto bytes = ImageValidation::Read(options.source);
		auto selected =
		    AssetFolders::MakeReference(options.assetRoot, destination);

		if (!FileUtils::WriteBytesAtomically(
		        destination,
		        std::as_bytes(std::span{bytes.data(), bytes.size()}), error,
		        false)) {
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
