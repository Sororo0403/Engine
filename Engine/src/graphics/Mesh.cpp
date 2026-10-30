#include "graphics/Mesh.h"
#include "core/Logger.h"

#include <directxtk12/BufferHelpers.h>

#include <climits>
#include <format>
#include <utility>

bool Mesh::Initialize(ID3D12Device *device, std::span<const Vertex> vertices,
                      std::span<const std::uint16_t> indices) {
	if (!device || vertices.empty() || indices.empty() ||
	    indices.size() % 3 != 0) {
		Logger::Error("Mesh::Initialize: 引数が無効です。");
		return false;
	}

	if (vertexBuffer_ || indexBuffer_) {
		Logger::Error("Mesh::Initialize: 初期化済みです。");
		return false;
	}

	// バッファビューのサイズはUINTで指定する
	if (vertices.size_bytes() > UINT_MAX || indices.size_bytes() > UINT_MAX) {
		Logger::Error("Mesh::Initialize: データサイズが大きすぎます。");
		return false;
	}

	// 存在しない頂点を参照していないか確認する
	for (const auto index : indices) {
		if (index >= vertices.size()) {
			Logger::Error("Mesh::Initialize: 頂点番号が範囲外です。");
			return false;
		}
	}

	// 両方の作成が成功してからメンバに反映する
	Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer;
	Microsoft::WRL::ComPtr<ID3D12Resource> indexBuffer;

	HRESULT hr = DirectX::CreateUploadBuffer(
	    device, vertices.data(), vertices.size(), vertexBuffer.GetAddressOf());

	if (FAILED(hr)) {
		Logger::Error(std::format(
		    "Meshの頂点バッファ作成に失敗しました。HRESULT=0x{:08X}",
		    static_cast<unsigned long>(hr)));
		return false;
	}

	hr = DirectX::CreateUploadBuffer(device, indices.data(), indices.size(),
	                                 indexBuffer.GetAddressOf());

	if (FAILED(hr)) {
		Logger::Error(std::format(
		    "Meshのインデックスバッファ作成に失敗しました。HRESULT=0x{:08X}",
		    static_cast<unsigned long>(hr)));
		return false;
	}

	vertexBuffer_ = std::move(vertexBuffer);
	indexBuffer_ = std::move(indexBuffer);

	vertexBufferView_.BufferLocation = vertexBuffer_->GetGPUVirtualAddress();
	vertexBufferView_.SizeInBytes = static_cast<UINT>(vertices.size_bytes());
	vertexBufferView_.StrideInBytes = static_cast<UINT>(sizeof(Vertex));

	indexBufferView_.BufferLocation = indexBuffer_->GetGPUVirtualAddress();
	indexBufferView_.SizeInBytes = static_cast<UINT>(indices.size_bytes());
	indexBufferView_.Format = DXGI_FORMAT_R16_UINT;

	indexCount_ = static_cast<UINT>(indices.size());

	Logger::Info(
	    std::format("Meshを初期化しました。頂点数={}、インデックス数={}",
	                vertices.size(), indices.size()));

	return true;
}

void Mesh::Draw(ID3D12GraphicsCommandList *commandList) const {
	if (!commandList || !vertexBuffer_ || !indexBuffer_) {
		Logger::Error("Mesh::Draw: 引数が無効、または未初期化です。");
		return;
	}

	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);
	commandList->IASetIndexBuffer(&indexBufferView_);

	commandList->DrawIndexedInstanced(indexCount_, 1, 0, 0, 0);
}

void Mesh::Finalize() {
	vertexBuffer_.Reset();
	indexBuffer_.Reset();

	vertexBufferView_ = {};
	indexBufferView_ = {};

	indexCount_ = 0;
}
