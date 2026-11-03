#include "graphics/material/MaterialManager.h"
#include "core/Logger.h"
#include "graphics/material/MaterialAsset.h"
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
		std::filesystem::path texture;
		std::filesystem::file_time_type textureModified{};
		std::uintmax_t textureSize = 0;
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

MaterialHandle MaterialManager::Load(const std::filesystem::path &path) {
	Impl::Entry *previous = nullptr;
	try {
		const auto absolute = std::filesystem::weakly_canonical(
		    PathUtils::ResolveFromExecutable(path));
		auto &entry = impl_->entries[absolute];
		previous = &entry;
		std::error_code ec;
		const auto modified = std::filesystem::last_write_time(absolute, ec);
		const auto size = std::filesystem::file_size(absolute, ec);
		const auto textureModified =
		    entry.texture.empty()
		        ? std::filesystem::file_time_type{}
		        : std::filesystem::last_write_time(entry.texture, ec);
		const auto textureSize =
		    entry.texture.empty()
		        ? 0
		        : std::filesystem::file_size(entry.texture, ec);

		if ((entry.handle || entry.failed) && entry.modified == modified &&
		    entry.size == size && entry.textureModified == textureModified &&
		    entry.textureSize == textureSize) {
			return entry.handle;
		}

		entry.modified = modified;
		entry.size = size;
		entry.textureModified = textureModified;
		entry.textureSize = textureSize;
		entry.failed = true;
		MaterialAsset asset;
		std::string error;

		if (!MaterialSerializer::Load(absolute, asset, error)) {
			throw std::runtime_error(error);
		}

		TextureData image;

		if (!asset.baseColorTexture.empty()) {
			image.source = TextureSource::FILE;
			image.filePath = (absolute.parent_path() / asset.baseColorTexture)
			                     .lexically_normal();
		}

		// 画像が未作成でも参照先を保持し、後から画像が追加されたときに再試行する。
		entry.texture = image.filePath;
		entry.textureModified =
		    image.filePath.empty()
		        ? std::filesystem::file_time_type{}
		        : std::filesystem::last_write_time(image.filePath, ec);
		entry.textureSize =
		    image.filePath.empty()
		        ? 0
		        : std::filesystem::file_size(image.filePath, ec);
		auto resource = std::make_shared<MaterialResource>();

		if (!resource->Initialize(impl_->graphics.GetDevice(),
		                          impl_->graphics.GetCommandQueue(), asset.data,
		                          image)) {
			throw std::runtime_error(
			    "Could not create material GPU resources.");
		}

		MaterialHandle result;
		result.resource_ = std::move(resource);
		result.device_ = impl_->graphics.GetDevice();
		entry.texture = image.filePath;
		entry.textureModified =
		    image.filePath.empty()
		        ? std::filesystem::file_time_type{}
		        : std::filesystem::last_write_time(image.filePath);
		entry.textureSize = image.filePath.empty()
		                        ? 0
		                        : std::filesystem::file_size(image.filePath);
		entry.handle = result;
		entry.failed = false;
		return result;
	} catch (const std::exception &e) {
		Logger::Error(std::format("MaterialManager::Load: {}: {}",
		                          PathUtils::ToUtf8(path), e.what()));
		return previous ? previous->handle : MaterialHandle{};
	}
}

MaterialHandle MaterialManager::Create(const MaterialAsset &asset,
                                       const std::filesystem::path &directory) {
	try {
		if (!asset.IsValid()) {
			throw std::invalid_argument("Invalid material draft.");
		}

		TextureData image;

		if (!asset.baseColorTexture.empty()) {
			image.source = TextureSource::FILE;
			image.filePath = (PathUtils::ResolveFromExecutable(directory) /
			                  asset.baseColorTexture)
			                     .lexically_normal();
		}

		auto resource = std::make_shared<MaterialResource>();

		if (!resource->Initialize(impl_->graphics.GetDevice(),
		                          impl_->graphics.GetCommandQueue(), asset.data,
		                          image)) {
			return {};
		}

		MaterialHandle result;
		result.resource_ = std::move(resource);
		result.device_ = impl_->graphics.GetDevice();
		return result;
	} catch (const std::exception &error) {
		Logger::Error(std::format("MaterialManager::Create: {}", error.what()));
		return {};
	}
}

void MaterialManager::Finalize() {
	impl_->entries.clear();
}
