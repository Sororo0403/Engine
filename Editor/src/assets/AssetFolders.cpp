#include "assets/AssetFolders.h"
#include "platform/windows/PathUtils.h"
#include <charconv>
#include <limits>
#include <stdexcept>

std::filesystem::path AssetFolders::SuggestAvailableName(
    const std::filesystem::path &directory, std::wstring stem,
    const std::wstring &extension) {
	const std::filesystem::path original = stem + extension;
	if (!PathUtils::IsValidFileName(original)) {
		throw std::invalid_argument("Enter a valid file or folder name.");
	}

	const auto occupied = [&](const std::filesystem::path &candidate) {
		return std::filesystem::exists(
		    std::filesystem::symlink_status(directory / candidate));
	};
	if (!occupied(original)) {
		return original;
	}

	std::size_t index = 2;
	const auto separator = stem.rfind(L'-');
	if (separator != std::wstring::npos && separator != 0) {
		const auto suffix = PathUtils::ToUtf8(stem.substr(separator + 1));
		std::size_t number = 0;
		const auto end = suffix.data() + suffix.size();
		const auto parsed = std::from_chars(suffix.data(), end, number);
		const std::filesystem::path base = stem.substr(0, separator) + extension;
		// 元の名前も使用中なら、提案済みの連番として次の番号へ進める。
		if (parsed.ec == std::errc{} && parsed.ptr == end && number >= 2 &&
		    suffix.front() != '0' &&
		    number < (std::numeric_limits<std::size_t>::max)() &&
		    PathUtils::IsValidFileName(base) && occupied(base)) {
			stem.resize(separator);
			index = number + 1;
		}
	}

	auto candidate = original;
	for (;; ++index) {
		candidate = stem + L"-" + std::to_wstring(index) + extension;
		if (!PathUtils::IsValidFileName(candidate)) {
			throw std::invalid_argument("Shorten the name to allow a numbered suffix.");
		}
		if (!occupied(candidate)) {
			return candidate;
		}
		if (index == (std::numeric_limits<std::size_t>::max)()) {
			throw std::runtime_error("No numbered name is available.");
		}
	}
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
