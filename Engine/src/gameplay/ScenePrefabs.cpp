#include "gameplay/ScenePrefabs.h"
#include "platform/windows/PathUtils.h"
#include "scene/PrefabSerializer.h"
#include "scene/Scene.h"
#include <Windows.h>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>
#include <vector>

namespace {
struct PathLess {
	bool operator()(const std::filesystem::path &a,
	                const std::filesystem::path &b) const {
		return CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) ==
		       CSTR_LESS_THAN;
	}
};

std::filesystem::path Normalize(const std::filesystem::path &path) {
	return PathUtils::ResolveFromExecutable(path)
	    .lexically_normal()
	    .make_preferred();
}
} // namespace

struct ScenePrefabs::Data {
	std::map<std::filesystem::path, std::shared_ptr<const Scene>, PathLess>
	    assets;
	std::vector<std::shared_ptr<const Scene>> scenes;
	std::size_t bytes = 0, objects = 0;
};

bool ScenePrefabs::Configure(const Scene &scene, bool preserve,
                             std::string &error) {
	std::filesystem::path current;
	try {
		auto next = preserve && data_ ? std::make_shared<Data>(*data_)
		                              : std::make_shared<Data>();
		std::vector<std::filesystem::path> pending;
		std::set<std::filesystem::path, PathLess> visited;
		const auto collect = [&](const Scene &source) {
			for (const auto &[id, o] : source.GetObjects()) {
				(void)id;
				if (!o.script || o.script->prefabs.empty())
					continue;
				if (!o.script->IsValid())
					throw std::runtime_error(
					    "Script Prefab references are invalid.");
				for (const auto &[name, path] : o.script->prefabs) {
					(void)name;
					if (path.empty())
						continue;
					auto key = Normalize(path);
					if (visited.insert(key).second)
						pending.push_back(std::move(key));
				}
			}
		};
		collect(scene);
		bool changed = !preserve || !data_;
		for (std::size_t i = 0; i < pending.size(); ++i) {
			current = pending[i];
			const auto found = next->assets.find(current);
			if (found != next->assets.end()) {
				collect(*found->second);
				continue;
			}
			if (next->assets.size() >= kMaximumAssets)
				throw std::runtime_error("Prefab asset limit of 256 exceeded.");
			const auto size = std::filesystem::file_size(current);
			if (size > kMaximumFileBytes ||
			    size > kMaximumTotalBytes - next->bytes)
				throw std::runtime_error("Prefab data exceeds the 32 MiB file "
				                         "/ 64 MiB total limit.");
			std::ifstream file(current, std::ios::binary);
			std::string text(static_cast<std::size_t>(size), '\0');
			if (!file ||
			    !file.read(text.data(),
			               static_cast<std::streamsize>(text.size())) ||
			    file.peek() != std::char_traits<char>::eof())
				throw std::runtime_error(
				    "Could not read a complete Prefab file.");
			auto loaded = std::make_shared<Scene>();
			std::string issue;
			if (!PrefabSerializer::Deserialize(text, current, *loaded, issue))
				throw std::runtime_error(issue);
			if (loaded->GetObjects().size() >
			    kMaximumTemplateObjects - next->objects)
				throw std::runtime_error(
				    "Prefab templates exceed 4096 total objects.");
			next->bytes += text.size();
			next->objects += loaded->GetObjects().size();
			next->assets.emplace(current, loaded);
			next->scenes.push_back(loaded);
			changed = true;
			collect(*loaded);
		}
		if (changed)
			data_ = std::move(next);
		error.clear();
		return true;
	} catch (const std::exception &exception) {
		error =
		    "Prefab '" + PathUtils::ToUtf8(current) + "': " + exception.what();
		return false;
	}
}

std::shared_ptr<const Scene>
ScenePrefabs::Find(const std::filesystem::path &path) const {
	try {
		if (!data_ || path.empty())
			return {};
		const auto found = data_->assets.find(Normalize(path));
		return found == data_->assets.end() ? std::shared_ptr<const Scene>{}
		                                    : found->second;
	} catch (...) {
		return {};
	}
}

std::span<const std::shared_ptr<const Scene>> ScenePrefabs::GetScenes() const {
	return data_ ? std::span<const std::shared_ptr<const Scene>>{data_->scenes}
	             : std::span<const std::shared_ptr<const Scene>>{};
}
