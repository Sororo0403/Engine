#include "assets/AssetFolders.h"
#include "platform/windows/PathUtils.h"
#include <Windows.h>
#include <stdexcept>

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

	auto current = resolved.begin();
	for (const auto &part : root) {
		if (current == resolved.end() ||
		    CompareStringOrdinal(part.c_str(), -1, current->c_str(), -1,
		                         TRUE) != CSTR_EQUAL) {
			throw std::invalid_argument("The folder resolves outside Assets.");
		}
		++current;
	}

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
		std::filesystem::path selected;
		auto component = destination.begin();

		// 検証済みのルート部分を、文字の表記差を許容して要素数で取り除く。
		for (auto rootPart = root.begin(); rootPart != root.end(); ++rootPart) {
			++component;
		}

		for (; component != destination.end(); ++component) {
			selected /= *component;
		}

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
