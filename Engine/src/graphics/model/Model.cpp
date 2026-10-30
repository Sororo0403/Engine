#include "graphics/model/Model.h"
#include "core/Logger.h"
#include "graphics/model/ModelData.h"

#include <directxtk12/DirectXHelpers.h>

#include <array>
#include <cstdint>
#include <exception>
#include <format>

bool Model::Initialize(ID3D12Device *device, ID3D12CommandQueue *commandQueue,
                       const ModelData &data) {
	if (!device || !commandQueue || data.meshes.empty() ||
	    data.materials.empty()) {
		Logger::Error("Model::Initialize: 引数が無効です。");
		return false;
	}

	if (srvHeap_ || !parts_.empty() || !textures_.empty() ||
	    !defaultMaterials_.empty()) {
		Logger::Error("Model::Initialize: 初期化済みです。");
		return false;
	}

	if (commandQueue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT) {
		Logger::Error(
		    "Model::Initialize: DIRECT型のコマンドキューが必要です。");
		return false;
	}

	try {
		// 物体ごとの材質を作るための初期値を保存する
		defaultMaterials_.resize(data.materials.size());

		for (std::size_t i = 0; i < data.materials.size(); ++i) {
			defaultMaterials_[i].baseColor = data.materials[i].baseColor;
		}

		if (!CreateMeshes(device, data) ||
		    !CreateTextures(device, commandQueue, data) ||
		    !CreateShaderResourceViews(device)) {
			Finalize();
			return false;
		}
	} catch (const std::exception &error) {
		Logger::Error(std::format("Modelの初期化中に例外が発生しました: {}",
		                          error.what()));

		Finalize();
		return false;
	}

	Logger::Info(std::format("Modelを初期化しました。メッシュ数={}、材質数={}",
	                         GetMeshCount(), GetMaterialCount()));

	return true;
}

bool Model::CreateMeshes(ID3D12Device *device, const ModelData &data) {
	parts_.resize(data.meshes.size());

	for (std::size_t i = 0; i < data.meshes.size(); ++i) {
		const ModelMeshData &source = data.meshes[i];

		if (source.materialIndex >= data.materials.size()) {
			Logger::Error(
			    std::format("Model: メッシュ{}の材質番号が範囲外です。", i));
			return false;
		}

		MeshPart &part = parts_[i];

		if (!part.mesh.Initialize(device, source.mesh.vertices,
		                          source.mesh.indices)) {
			Logger::Error(
			    std::format("Model: メッシュ{}の作成に失敗しました。", i));
			return false;
		}

		part.materialIndex = source.materialIndex;
	}

	return true;
}

bool Model::CreateTextures(ID3D12Device *device,
                           ID3D12CommandQueue *commandQueue,
                           const ModelData &data) {
	// 0番は画像なし用の白、それ以降は各材質の画像
	textures_.resize(data.materials.size() + 1);

	const std::array<std::uint8_t, 4> white{255, 255, 255, 255};

	if (!textures_[0].CreateFromRGBA(device, commandQueue, 1, 1, white)) {
		Logger::Error("Model: 白いテクスチャの作成に失敗しました。");
		return false;
	}

	for (std::size_t i = 0; i < data.materials.size(); ++i) {
		const ModelTextureData &image = data.materials[i].baseColorTexture;

		Texture &texture = textures_[i + 1];
		bool loaded = false;

		switch (image.source) {
		case ModelTextureSource::NONE:
			continue;

		case ModelTextureSource::FILE:
			loaded = texture.Load(device, commandQueue, image.filePath);
			break;

		case ModelTextureSource::ENCODED:
			loaded = texture.LoadFromMemory(device, commandQueue, image.bytes);
			break;

		case ModelTextureSource::RGBA8:
			loaded = texture.CreateFromRGBA(device, commandQueue, image.width,
			                                image.height, image.bytes);
			break;
		}

		if (!loaded) {
			Logger::Error(std::format(
			    "Model: 材質{}のテクスチャ作成に失敗しました。", i));
			return false;
		}
	}

	return true;
}

bool Model::CreateShaderResourceViews(ID3D12Device *device) {
	const std::size_t materialCount = defaultMaterials_.size();

	srvHeap_ = std::make_unique<DirectX::DescriptorHeap>(device, materialCount);

	for (std::size_t i = 0; i < materialCount; ++i) {
		ID3D12Resource *resource = textures_[i + 1].GetResource();

		// 画像が指定されていない材質には白を使用する
		if (!resource) {
			resource = textures_[0].GetResource();
		}

		DirectX::CreateShaderResourceView(device, resource,
		                                  srvHeap_->GetCpuHandle(i));
	}

	return true;
}

std::size_t Model::GetMeshCount() const { return parts_.size(); }

const Mesh &Model::GetMesh(std::size_t index) const {
	return parts_.at(index).mesh;
}

std::size_t Model::GetMeshMaterialIndex(std::size_t meshIndex) const {
	return parts_.at(meshIndex).materialIndex;
}

std::size_t Model::GetMaterialCount() const { return defaultMaterials_.size(); }

const MaterialData &
Model::GetDefaultMaterialData(std::size_t materialIndex) const {
	return defaultMaterials_.at(materialIndex);
}

D3D12_GPU_DESCRIPTOR_HANDLE
Model::GetTextureSrv(std::size_t materialIndex) const {
	if (!srvHeap_) {
		return {};
	}

	return srvHeap_->GetGpuHandle(materialIndex);
}

ID3D12DescriptorHeap *Model::GetDescriptorHeap() const {
	return srvHeap_ ? srvHeap_->Heap() : nullptr;
}

void Model::Finalize() {
	srvHeap_.reset();

	for (auto &part : parts_) {
		part.mesh.Finalize();
	}

	parts_.clear();

	for (auto &texture : textures_) {
		texture.Finalize();
	}

	textures_.clear();
	defaultMaterials_.clear();
}
