#include "graphics/MeshInstance.h"
#include "core/Logger.h"
#include "graphics/Camera.h"

#include <format>

bool MeshInstance::Initialize(ID3D12Device *device, const Mesh &mesh) {
	if (!device || constantBuffer_ || mesh_) {
		Logger::Error(
		    "MeshInstance::Initialize: 引数が無効、または初期化済みです。");
		return false;
	}

	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
	heapProperties.CreationNodeMask = 1;
	heapProperties.VisibleNodeMask = 1;

	D3D12_RESOURCE_DESC desc{};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Width = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
	desc.Height = 1;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.SampleDesc.Count = 1;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	HRESULT hr = device->CreateCommittedResource(
	    &heapProperties, D3D12_HEAP_FLAG_NONE, &desc,
	    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
	    IID_PPV_ARGS(constantBuffer_.GetAddressOf()));

	if (FAILED(hr)) {
		Logger::Error(std::format(
		    "物体用の定数バッファ作成に失敗しました。HRESULT=0x{:08X}",
		    static_cast<unsigned long>(hr)));
		return false;
	}

	void *mapped = nullptr;
	const D3D12_RANGE readRange{0, 0};

	hr = constantBuffer_->Map(0, &readRange, &mapped);

	if (FAILED(hr)) {
		Logger::Error(std::format(
		    "物体用の定数バッファのMapに失敗しました。HRESULT=0x{:08X}",
		    static_cast<unsigned long>(hr)));

		Finalize();
		return false;
	}

	mappedWorld_ = static_cast<DirectX::XMFLOAT4X4 *>(mapped);
	mesh_ = &mesh;

	DirectX::XMStoreFloat4x4(mappedWorld_, DirectX::XMMatrixIdentity());

	return true;
}

void MeshInstance::Update(const Camera &camera) {
	if (!mappedWorld_) {
		Logger::Error("MeshInstance::Update: 未初期化です。");
		return;
	}

	using namespace DirectX;

	const XMMATRIX scale = XMMatrixScaling(
	    transform_.scale.x, transform_.scale.y, transform_.scale.z);

	const XMMATRIX rotation = XMMatrixRotationRollPitchYaw(
	    transform_.rotation.x, transform_.rotation.y, transform_.rotation.z);

	const XMMATRIX translation =
	    XMMatrixTranslation(transform_.translation.x, transform_.translation.y,
	                        transform_.translation.z);

	const XMMATRIX world = scale * rotation * translation;
	const XMMATRIX view = XMLoadFloat4x4(&camera.GetViewMatrix());
	const XMMATRIX projection = XMLoadFloat4x4(&camera.GetProjectionMatrix());

	XMStoreFloat4x4(mappedWorld_, world * view * projection);
}

void MeshInstance::SetTransform(const Transform &transform) {
	transform_ = transform;
}

const Transform &MeshInstance::GetTransform() const { return transform_; }

const Mesh *MeshInstance::GetMesh() const { return mesh_; }

D3D12_GPU_VIRTUAL_ADDRESS
MeshInstance::GetConstantBufferAddress() const {
	if (!constantBuffer_) {
		return 0;
	}

	return constantBuffer_->GetGPUVirtualAddress();
}

void MeshInstance::Finalize() {
	if (constantBuffer_ && mappedWorld_) {
		constantBuffer_->Unmap(0, nullptr);
	}

	mappedWorld_ = nullptr;
	constantBuffer_.Reset();
	mesh_ = nullptr;
}
