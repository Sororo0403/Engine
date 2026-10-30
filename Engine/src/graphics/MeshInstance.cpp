#include "graphics/MeshInstance.h"
#include "core/Logger.h"
#include "graphics/Camera.h"
#include "graphics/Material.h"

#include <format>

bool MeshInstance::Initialize(ID3D12Device *device, const Mesh &mesh,
                              const Material &material) {
	if (!device || constantBuffer_ || mesh_ || material_ ||
	    material.GetConstantBufferAddress() == 0 ||
	    material.GetTextureSrv().ptr == 0) {
		Logger::Error(
		    "MeshInstance::Initialize: 引数が無効、または初期化済みです。");
		return false;
	}

	// 行列2個で128バイト。確保する256バイト以内に収める
	static_assert(sizeof(TransformationConstants) <=
	              D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);

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

	mappedConstants_ = static_cast<TransformationConstants *>(mapped);

	mesh_ = &mesh;
	material_ = &material;

	DirectX::XMStoreFloat4x4(&mappedConstants_->worldViewProjection,
	                         DirectX::XMMatrixIdentity());

	DirectX::XMStoreFloat4x4(&mappedConstants_->worldInverseTranspose,
	                         DirectX::XMMatrixIdentity());

	return true;
}

void MeshInstance::Update(const Camera &camera) {
	if (!mappedConstants_) {
		Logger::Error("MeshInstance::Update: 未初期化です。");
		return;
	}

	// 拡大率が0だと逆行列を作れない
	if (transform_.scale.x == 0.0f || transform_.scale.y == 0.0f ||
	    transform_.scale.z == 0.0f) {
		Logger::Error("MeshInstance::Update: 拡大率に0は指定できません。");
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

	// 頂点位置を画面へ投影する行列
	XMStoreFloat4x4(&mappedConstants_->worldViewProjection,
	                world * view * projection);

	// 法線をワールド座標へ変換する行列
	const XMMATRIX worldInverseTranspose =
	    XMMatrixTranspose(XMMatrixInverse(nullptr, world));

	XMStoreFloat4x4(&mappedConstants_->worldInverseTranspose,
	                worldInverseTranspose);
}

void MeshInstance::SetTransform(const Transform &transform) {
	transform_ = transform;
}

const Transform &MeshInstance::GetTransform() const { return transform_; }

const Mesh *MeshInstance::GetMesh() const { return mesh_; }

const Material *MeshInstance::GetMaterial() const { return material_; }

D3D12_GPU_VIRTUAL_ADDRESS
MeshInstance::GetConstantBufferAddress() const {
	if (!constantBuffer_) {
		return 0;
	}

	return constantBuffer_->GetGPUVirtualAddress();
}

void MeshInstance::Finalize() {
	if (constantBuffer_ && mappedConstants_) {
		constantBuffer_->Unmap(0, nullptr);
	}

	mappedConstants_ = nullptr;
	constantBuffer_.Reset();
	mesh_ = nullptr;
	material_ = nullptr;
}
