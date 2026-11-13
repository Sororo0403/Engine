#include "assets/AssetFolders.h"
#include "platform/windows/PathUtils.h"
#include <stdexcept>

std::string AssetFolders::SuggestImportName(
    const std::filesystem::path &assetRoot, const std::filesystem::path &folder,
    const std::string &name, bool keepExtension) {
	const auto original = PathUtils::FromUtf8(name);
	if (!PathUtils::IsValidFileName(original)) {
		throw std::invalid_argument("Enter a valid import name.");
	}

	const auto directory = Resolve(assetRoot, folder);
	const auto stem = keepExtension ? original.stem() : original;
	const auto extension = keepExtension ? original.extension().native() : std::wstring{};
	auto candidate = original;
	for (std::size_t index = 2;
	     std::filesystem::exists(std::filesystem::symlink_status(directory / candidate));
	     ++index) {
		candidate = stem.native() + L"-" + std::to_wstring(index) + extension;
		if (!PathUtils::IsValidFileName(candidate)) {
			throw std::invalid_argument("Shorten the import name to allow a numbered suffix.");
		}
	}
	return PathUtils::ToUtf8(candidate);
}

std::filesystem::path
AssetFolders::MakeReference(const std::filesystem::path &assetRoot,
                            const std::filesystem::path &absolutePath) {
	if (assetRoot.empty() ||
	    assetRoot.native().find(L'\0') != std::wstring::npos ||
	    (assetRoot.has_root_path() && !assetRoot.is_absolute()) ||
	    !absolutePath.is_absolute() ||
	    absolutePath.native().find(L'\0') != std::wstring::npos) {
		throw std::invalid_argument("Choose valid asset references.");
	}
	const auto target = absolutePath.lexically_normal();

	if (assetRoot.is_absolute()) {
		return target;
	}

	const auto relative =
	    target.lexically_relative(PathUtils::GetExecutableDirectory());
	return relative.empty() ? target : relative;
}

std::filesystem::path
AssetFolders::Resolve(const std::filesystem::path &assetRoot,
                      const std::filesystem::path &folder) {
	if (assetRoot.empty() ||
	    assetRoot.native().find(L'\0') != std::wstring::npos ||
	    folder.has_root_path() ||
	    folder.native().find(L'\0') != std::wstring::npos) {
		throw std::invalid_argument("Choose a folder inside Assets.");
	}

	for (const auto &part : folder) {
		if (part == L".." || part.native().find(L':') != std::wstring::npos) {
			throw std::invalid_argument("Choose a folder inside Assets.");
		}
	}

	const auto root =
	    std::filesystem::canonical(PathUtils::ResolveFromExecutable(assetRoot));
	const auto resolved = std::filesystem::canonical(root / folder);

	if (!std::filesystem::is_directory(root) ||
	    !std::filesystem::is_directory(resolved)) {
		throw std::invalid_argument("The Assets folder does not exist.");
	}

	PathUtils::RelativeInside(root, resolved);

	return resolved;
}

bool AssetFolders::Create(const std::filesystem::path &assetRoot,
                          const std::filesystem::path &parent,
                          const std::filesystem::path &name,
                          std::filesystem::path &created, std::string &error) {
	try {
		if (!PathUtils::IsValidFileName(name)) {
			throw std::invalid_argument("Enter a single valid folder name.");
		}

		const auto root = Resolve(assetRoot, {});
		const auto destination = Resolve(assetRoot, parent) / name;
		auto selected = PathUtils::RelativeInside(root, destination);

		if (selected.empty()) {
			throw std::runtime_error(
			    "Could not create the Assets folder reference.");
		}

		if (!std::filesystem::create_directory(destination)) {
			throw std::runtime_error(
			    "That folder already exists. Choose another name.");
		}

		created.swap(selected);
		error.clear();
		return true;
	} catch (const std::exception &exception) {
		error = exception.what();
		return false;
	}
}
