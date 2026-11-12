#include "graphics/mesh/MeshInstance.h"
#include "core/Logger.h"
#include "graphics/camera/Camera.h"
#include "graphics/material/Material.h"
#include "math/MathUtils.h"
#include "math/TransformUtils.h"

bool MeshInstance::Initialize(ConstantBufferPool &pool, const Mesh &mesh,
                              const Material &material,
                              const D3D12_VERTEX_BUFFER_VIEW *weights) {
	if (constantBuffer_.IsInitialized() || mesh_ || material_ ||
	    material.GetConstantBufferAddress() == 0 ||
	    material.GetTextureSrv().ptr == 0) {
		Logger::Error(
		    "MeshInstance::Initialize: 引数が無効、または初期化済みです。");
		return false;
	}

	TransformationConstants initialValue{};
	DirectX::XMStoreFloat4x4(&initialValue.world, DirectX::XMMatrixIdentity());
	DirectX::XMStoreFloat4x4(&initialValue.worldViewProjection,
	                         DirectX::XMMatrixIdentity());
	DirectX::XMStoreFloat4x4(&initialValue.worldInverseTranspose,
	                         DirectX::XMMatrixIdentity());

	if (!constantBuffer_.Initialize(pool, initialValue)) {
		return false;
	}

	if (weights) {
		if (!weights->BufferLocation || !weights->SizeInBytes ||
		    weights->StrideInBytes != sizeof(ModelVertexWeights)) {
			constantBuffer_.Finalize();
			return false;
		}

		auto buffer = std::make_unique<ConstantBuffer<SkinningConstants>>();

		if (!buffer->Initialize(pool)) {
			constantBuffer_.Finalize();
			return false;
		}

		skinning_ = std::move(buffer);
		weights_ = weights;
	}

	mesh_ = &mesh;
	material_ = &material;

	return true;
}

bool MeshInstance::Update(const Camera &camera,
                          const DirectX::XMFLOAT4X4 &worldMatrix) {
	if (!constantBuffer_.IsInitialized()) {
		Logger::Error("MeshInstance::Update: 未初期化です。");
		return false;
	}

	DirectX::XMFLOAT4X4 inverse;

	if (!TransformUtils::TryInverseWorldMatrix(worldMatrix, inverse)) {
		Logger::Error("MeshInstance::Update: ワールド行列が無効です。");
		return false;
	}

	using namespace DirectX;

	TransformationConstants constants{};
	constants.world = worldMatrix;
	const XMMATRIX world = XMLoadFloat4x4(&worldMatrix);
	const XMMATRIX view = XMLoadFloat4x4(&camera.GetViewMatrix());
	const XMMATRIX projection = XMLoadFloat4x4(&camera.GetProjectionMatrix());

	// 頂点位置と法線に、せん断を含む同じワールド行列を反映する。
	XMStoreFloat4x4(&constants.worldViewProjection, world * view * projection);
	XMStoreFloat4x4(&constants.worldInverseTranspose,
	                XMMatrixTranspose(XMLoadFloat4x4(&inverse)));

	if (!MathUtils::IsFinite(constants.worldViewProjection)) {
		Logger::Error(
		    "MeshInstance::Update: 投影結果を有限値で表現できません。");
		return false;
	}

	constantBuffer_.Update(constants);
	return true;
}

const Mesh *MeshInstance::GetMesh() const {
	return mesh_;
}

const Material *MeshInstance::GetMaterial() const {
	return material_;
}

D3D12_GPU_VIRTUAL_ADDRESS
MeshInstance::GetConstantBufferAddress() const {
	return constantBuffer_.GetGPUVirtualAddress();
}

void MeshInstance::Finalize() {
	constantBuffer_.Finalize();
	skinning_.reset();
	weights_ = nullptr;
	mesh_ = nullptr;
	material_ = nullptr;
}

bool MeshInstance::UpdateSkinning(const SkinningConstants &constants) {
	return skinning_ &&
	       constants.boneCount <= ModelAnimationData::kMaximumBones &&
	       skinning_->Update(constants);
}

D3D12_GPU_VIRTUAL_ADDRESS MeshInstance::GetSkinningAddress() const {
	return skinning_ ? skinning_->GetGPUVirtualAddress() : 0;
}

const D3D12_VERTEX_BUFFER_VIEW *MeshInstance::GetSkinningWeights() const {
	return weights_;
}
