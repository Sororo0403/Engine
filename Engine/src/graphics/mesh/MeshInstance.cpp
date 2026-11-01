#include "graphics/mesh/MeshInstance.h"
#include "core/Logger.h"
#include "graphics/camera/Camera.h"
#include "graphics/material/Material.h"
#include "math/TransformUtils.h"

bool MeshInstance::Initialize(ID3D12Device *device, const Mesh &mesh,
                              const Material &material) {
	if (!device || constantBuffer_.IsInitialized() || mesh_ || material_ ||
	    material.GetConstantBufferAddress() == 0 ||
	    material.GetTextureSrv().ptr == 0) {
		Logger::Error(
		    "MeshInstance::Initialize: 引数が無効、または初期化済みです。");
		return false;
	}

	TransformationConstants initialValue{};
	DirectX::XMStoreFloat4x4(&initialValue.worldViewProjection,
	                         DirectX::XMMatrixIdentity());
	DirectX::XMStoreFloat4x4(&initialValue.worldInverseTranspose,
	                         DirectX::XMMatrixIdentity());

	if (!constantBuffer_.Initialize(device, initialValue)) {
		return false;
	}

	mesh_ = &mesh;
	material_ = &material;

	return true;
}

void MeshInstance::Update(const Camera &camera) {
	if (!constantBuffer_.IsInitialized()) {
		Logger::Error("MeshInstance::Update: 未初期化です。");
		return;
	}

	if (!TransformUtils::IsValid(transform_)) {
		Logger::Error("MeshInstance::Update: 変形情報が無効です。");
		return;
	}

	using namespace DirectX;

	TransformationConstants constants{};
	const XMMATRIX world = TransformUtils::MakeWorldMatrix(transform_);
	const XMMATRIX view = XMLoadFloat4x4(&camera.GetViewMatrix());
	const XMMATRIX projection = XMLoadFloat4x4(&camera.GetProjectionMatrix());

	// 頂点位置を画面へ投影する行列
	XMStoreFloat4x4(&constants.worldViewProjection, world * view * projection);

	// 法線をワールド座標へ変換する行列
	const XMMATRIX worldInverseTranspose =
	    XMMatrixTranspose(XMMatrixInverse(nullptr, world));

	XMStoreFloat4x4(&constants.worldInverseTranspose, worldInverseTranspose);
	constantBuffer_.Update(constants);
}

void MeshInstance::SetTransform(const Transform &transform) {
	transform_ = transform;
}

const Transform &MeshInstance::GetTransform() const {
	return transform_;
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
	mesh_ = nullptr;
	material_ = nullptr;
}
