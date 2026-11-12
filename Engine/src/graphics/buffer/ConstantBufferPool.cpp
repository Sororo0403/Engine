#include "graphics/buffer/ConstantBufferPool.h"
#include "core/Logger.h"

#include <directxtk12/BufferHelpers.h>
#include <wrl/client.h>

#include <algorithm>
#include <bit>
#include <exception>
#include <format>
#include <utility>

namespace {
constexpr std::size_t kPageBytes = 256 * 1024;
constexpr std::size_t kAlignment = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
constexpr std::size_t kMaximumAllocation =
    D3D12_REQ_CONSTANT_BUFFER_ELEMENT_COUNT * 16;
}

struct ConstantBufferPool::Page {
	~Page() {
		if (mapped)
			resource->Unmap(0, nullptr);
	}

	Microsoft::WRL::ComPtr<ID3D12Resource> resource;
	std::byte *mapped = nullptr;
	D3D12_GPU_VIRTUAL_ADDRESS address = 0;
	std::size_t slotSize = 0;
	std::vector<std::size_t> available;
	std::vector<std::size_t> retired;
};

ConstantBufferPool::Allocation::Allocation(std::shared_ptr<Page> page,
                                            std::size_t slot) noexcept
    : page_(std::move(page)), slot_(slot) {
}

ConstantBufferPool::Allocation::~Allocation() {
	Reset();
}

ConstantBufferPool::Allocation &
ConstantBufferPool::Allocation::operator=(Allocation &&other) noexcept {
	if (this != &other) {
		Reset();
		page_ = std::move(other.page_);
		slot_ = other.slot_;
	}
	return *this;
}

void *ConstantBufferPool::Allocation::GetMappedData() const noexcept {
	return page_ ? page_->mapped + slot_ * page_->slotSize : nullptr;
}

D3D12_GPU_VIRTUAL_ADDRESS
ConstantBufferPool::Allocation::GetGPUVirtualAddress() const noexcept {
	return page_ ? page_->address + slot_ * page_->slotSize : 0;
}

void ConstantBufferPool::Allocation::Reset() noexcept {
	if (page_) {
		page_->retired.push_back(slot_);
		page_.reset();
	}
}

ConstantBufferPool::Allocation ConstantBufferPool::Allocate(std::size_t bytes) {
	if (!device_ || bytes == 0 || bytes > kMaximumAllocation)
		return {};

	try {
		const auto slotSize = std::bit_ceil((std::max)(bytes, kAlignment));
		const auto found = std::ranges::find_if(pages_, [&](const auto &page) {
			return page->slotSize == slotSize && !page->available.empty();
		});
		auto page = found == pages_.end() ? nullptr : *found;
		if (!page) {
			page = std::make_shared<Page>();
			page->slotSize = slotSize;
			const auto slots = kPageBytes / slotSize;
			page->available.reserve(slots);
			page->retired.reserve(slots);

			// 各領域は使用中・再利用待ち・空きのいずれか一つに属する。
			// 両リストを先に確保して、解放と再利用でメモリ確保を発生させない。
			for (auto slot = slots; slot > 0; --slot)
				page->available.push_back(slot - 1);

			HRESULT hr = DirectX::CreateUploadBuffer(
			    device_, nullptr, 1, kPageBytes, page->resource.GetAddressOf());
			if (FAILED(hr)) {
				Logger::Error(std::format(
				    "共有定数バッファの作成に失敗しました。HRESULT=0x{:08X}",
				    static_cast<unsigned long>(hr)));
				return {};
			}

			void *mapped = nullptr;
			const D3D12_RANGE readRange{0, 0};
			hr = page->resource->Map(0, &readRange, &mapped);
			if (FAILED(hr)) {
				Logger::Error(std::format(
				    "共有定数バッファのMapに失敗しました。HRESULT=0x{:08X}",
				    static_cast<unsigned long>(hr)));
				return {};
			}
			page->mapped = static_cast<std::byte *>(mapped);
			page->address = page->resource->GetGPUVirtualAddress();
			pages_.push_back(page);
		}

		const auto slot = page->available.back();
		page->available.pop_back();
		return Allocation(std::move(page), slot);
	} catch (const std::exception &error) {
		Logger::Error(std::format("定数バッファ領域の確保に失敗しました。{}",
		                          error.what()));
		return {};
	}
}

void ConstantBufferPool::ReclaimCompletedFrame() noexcept {
	for (const auto &page : pages_) {
		page->available.insert(page->available.end(), page->retired.begin(),
		                       page->retired.end());
		page->retired.clear();
	}
}

void ConstantBufferPool::Finalize() noexcept {
	pages_.clear();
	device_ = nullptr;
}
