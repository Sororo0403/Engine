#include "graphics/material/MaterialManager.h"
#include "core/Logger.h"
#include "graphics/material/MaterialResource.h"
#include "graphics/render/D3D12Graphics.h"
#include "platform/windows/PathUtils.h"
#include <Windows.h>
#include <format>
#include <map>
#include <stdexcept>
#include <utility>

struct MaterialManager::Impl {
	/// <summary>
	/// Windows上で同じ表記になる材質パスを、大文字・小文字を区別せず共有する
	/// </summary>
	struct PathLess {
		bool operator()(const std::filesystem::path &a,
		                const std::filesystem::path &b) const {
			return CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) ==
			       CSTR_LESS_THAN;
		}
	};

	struct Entry {
		MaterialHandle handle;
		std::filesystem::file_time_type modified{};
		std::uintmax_t size = 0;
		bool failed = false;
	};

	explicit Impl(D3D12Graphics &graphics) : graphics(graphics) {
	}

	D3D12Graphics &graphics;
	std::map<std::filesystem::path, Entry, PathLess> entries;
};

MaterialManager::MaterialManager(D3D12Graphics &graphics)
    : impl_(std::make_unique<Impl>(graphics)) {
}

MaterialManager::~MaterialManager() = default;

MaterialHandle MaterialManager::LoadTexture(const std::filesystem::path &path) {
	return LoadTextureFile(path, false);
}

MaterialHandle
MaterialManager::ReloadTexture(const std::filesystem::path &path) {
	return LoadTextureFile(path, true);
}

MaterialHandle
MaterialManager::LoadTextureFile(const std::filesystem::path &path,
                                 bool reload) {
	Impl::Entry *previous = nullptr;

	try {
		if ((reload && path.empty()) ||
		    (!path.empty() &&
		     (!PathUtils::IsImageFile(path) ||
		      path.native().find(L'\0') != std::wstring::npos))) {
			throw std::invalid_argument("Invalid texture path.");
		}

		const auto absolute = path.empty()
		                          ? std::filesystem::path{}
		                          : std::filesystem::weakly_canonical(
		                                PathUtils::ResolveFromExecutable(path));
		auto &entry = impl_->entries[absolute];
		previous = &entry;
		std::error_code modifiedError, sizeError;
		const auto modified =
		    absolute.empty()
		        ? std::filesystem::file_time_type{}
		        : std::filesystem::last_write_time(absolute, modifiedError);
		const auto size = absolute.empty()
		                      ? 0
		                      : std::filesystem::file_size(absolute, sizeError);

		if (!reload && (entry.handle || entry.failed) &&
		    entry.modified == modified && entry.size == size) {
			return entry.handle;
		}

		entry.modified = modified;
		entry.size = size;
		entry.failed = true;

		if (modifiedError || sizeError) {
			throw std::runtime_error(
			    "Could not read texture file information.");
		}

		TextureData image;

		if (!absolute.empty()) {
			image.source = TextureSource::FILE;
			image.filePath = absolute;
		}

		auto resource = std::make_shared<MaterialResource>();

		if (!resource->Initialize(impl_->graphics.GetDevice(),
		                          impl_->graphics.GetCommandQueue(),
		                          MaterialData{}, image)) {
			throw std::runtime_error("Could not create texture GPU resources.");
		}

		MaterialHandle result;
		result.resource_ = std::move(resource);
		result.device_ = impl_->graphics.GetDevice();
		entry.handle = result;
		entry.failed = false;
		return result;
	} catch (const std::exception &error) {
		Logger::Error(std::format("MaterialManager::{}: {}: {}",
		                          reload ? "ReloadTexture" : "LoadTexture",
		                          PathUtils::ToUtf8(path), error.what()));
		return !reload && previous ? previous->handle : MaterialHandle{};
	}
}

void MaterialManager::Finalize() {
	impl_->entries.clear();
}
