#include "settings/RecentScenes.h"

#include "core/Logger.h"
#include "platform/windows/PathUtils.h"

#include <algorithm>
#include <exception>
#include <format>
#include <stdexcept>
#include <utility>

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

void RecentScenes::FollowProjectMove(const std::filesystem::path &previousRoot,
                                     const std::filesystem::path &root) {
	namespace fs = std::filesystem;
	const std::pair move{previousRoot, root};
	auto updated = paths_;
	bool changed = false;
	for (auto &path : updated) {
		const auto candidate = PathUtils::AfterMove(path, std::span{&move, 1});
		if (candidate == path) {
			continue;
		}
		std::error_code status;
		if (fs::symlink_status(path, status).type() != fs::file_type::not_found) {
			continue;
		}
		const auto resolved = fs::canonical(candidate, status);
		if (status || !fs::is_regular_file(resolved, status)) {
			continue;
		}
		try {
			PathUtils::RelativeInside(root, resolved);
		} catch (const std::runtime_error &) {
			continue;
		}
		path = resolved;
		changed = true;
	}

	if (changed) {
		RecentScenes recent;
		for (auto entry = updated.rbegin(); entry != updated.rend(); ++entry) {
			if (!recent.Add(*entry)) {
				return;
			}
		}
		paths_.swap(recent.paths_);
	}
}
