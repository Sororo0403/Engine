#include "settings/RecentScenes.h"

#include "core/Logger.h"
#include "platform/windows/PathUtils.h"

#include <algorithm>
#include <exception>
#include <format>

bool RecentScenes::Add(const std::filesystem::path &path) {
	try {
		const auto resolved = PathUtils::ResolveFromExecutable(path);
		auto paths = paths_;
		std::erase(paths, resolved);
		paths.insert(paths.begin(), resolved);

		if (paths.size() > kMaximumCount) {
			paths.resize(kMaximumCount);
		}

		paths_.swap(paths);
		return true;
	} catch (const std::exception &error) {
		Logger::Error(std::format("RecentScenes::Add: {}", error.what()));
		return false;
	}
}

std::span<const std::filesystem::path> RecentScenes::GetPaths() const {
	return paths_;
}

void RecentScenes::Clear() {
	paths_.clear();
}

void RecentScenes::RemoveMissing() {
	std::erase_if(paths_, [](const auto &path) {
		std::error_code error;
		return std::filesystem::status(path, error).type() ==
		       std::filesystem::file_type::not_found;
	});
}
