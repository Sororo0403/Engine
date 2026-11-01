#include "graphics/render/DebugLineBatch.h"

#include "core/Logger.h"
#include "math/MathUtils.h"

#include <directxtk12/BufferHelpers.h>

#include <cstring>
#include <format>
#include <utility>

bool DebugLineBatch::Update(ID3D12Device *device,
                            std::span<const DebugLine> lines) {
	Clear();

	if (!device) {
		Logger::Error("DebugLineBatch::Update: デバイスが無効です。");
		return false;
	}

	if (lines.empty()) {
		return true;
	}

	// 1本の線には2頂点を使用する
	if (lines.size() > kMaxVertexCount / 2) {
		Logger::Error("DebugLineBatch::Update: 線の数が多すぎます。");
		return false;
	}

	const std::size_t requiredCount = lines.size() * 2;

	vertices_.resize(requiredCount);

	for (std::size_t i = 0; i < lines.size(); ++i) {
		const auto &line = lines[i];

		if (!MathUtils::IsFinite(line.start) ||
		    !MathUtils::IsFinite(line.end) ||
		    !MathUtils::IsFinite(line.color)) {
			Logger::Error(
			    "DebugLineBatch::Update: 線の座標または色が無効です。");
			return false;
		}

		vertices_[i * 2] = {
		    line.start,
		    line.color,
		};

		vertices_[i * 2 + 1] = {
		    line.end,
		    line.color,
		};
	}

	if (!EnsureCapacity(device, requiredCount)) {
		return false;
	}

	const std::size_t byteSize = vertices_.size() * sizeof(DebugLineVertex);

	std::memcpy(mappedData_, vertices_.data(), byteSize);

	vertexBufferView_.BufferLocation = vertexBuffer_->GetGPUVirtualAddress();
	vertexBufferView_.SizeInBytes = static_cast<UINT>(byteSize);
	vertexBufferView_.StrideInBytes =
	    static_cast<UINT>(sizeof(DebugLineVertex));

	vertexCount_ = static_cast<UINT>(requiredCount);
	return true;
}

void DebugLineBatch::Clear() {
	vertices_.clear();
	vertexCount_ = 0;
	vertexBufferView_.SizeInBytes = 0;
}

UINT DebugLineBatch::GetVertexCount() const {
	return vertexCount_;
}

const D3D12_VERTEX_BUFFER_VIEW &DebugLineBatch::GetVertexBufferView() const {
	return vertexBufferView_;
}

void DebugLineBatch::Finalize() {
	if (vertexBuffer_ && mappedData_) {
		vertexBuffer_->Unmap(0, nullptr);
	}

	mappedData_ = nullptr;
	vertexBuffer_.Reset();

	vertexBufferView_ = {};
	capacity_ = 0;
	vertexCount_ = 0;
	vertices_.clear();
}

bool DebugLineBatch::EnsureCapacity(ID3D12Device *device,
                                    std::size_t requiredCount) {
	if (requiredCount <= capacity_) {
		return true;
	}

	std::size_t newCapacity =
	    capacity_ == 0 ? kInitialVertexCapacity : capacity_;

	// 少しずつ増える場合に、毎回バッファを作り直さないようにする
	while (newCapacity < requiredCount) {
		if (newCapacity > kMaxVertexCount / 2) {
			newCapacity = kMaxVertexCount;
		} else {
			newCapacity *= 2;
		}
	}

	Microsoft::WRL::ComPtr<ID3D12Resource> resource;

	HRESULT hr = DirectX::CreateUploadBuffer(device, nullptr, newCapacity,
	                                         sizeof(DebugLineVertex),
	                                         resource.GetAddressOf());

	if (FAILED(hr)) {
		Logger::Error(
		    std::format("線の頂点バッファ作成に失敗しました。HRESULT=0x{:08X}",
		                static_cast<unsigned long>(hr)));
		return false;
	}

	void *mapped = nullptr;
	const D3D12_RANGE readRange{0, 0};

	hr = resource->Map(0, &readRange, &mapped);

	if (FAILED(hr)) {
		Logger::Error(
		    std::format("線の頂点バッファのMapに失敗しました。HRESULT=0x{:08X}",
		                static_cast<unsigned long>(hr)));
		return false;
	}

	// 作成とMapが成功してから、以前のバッファを置き換える
	if (vertexBuffer_ && mappedData_) {
		vertexBuffer_->Unmap(0, nullptr);
	}

	vertexBuffer_ = std::move(resource);
	mappedData_ = mapped;
	capacity_ = newCapacity;

	return true;
}
