#include "graphics/ui/UITexture.h"

#include "graphics/material/Texture.h"
#include "graphics/render/D3D12Graphics.h"
#include <wrl/client.h>

/// <summary>
/// 読み込んだ画像と、作成に使用したデバイスを保持する
/// </summary>
struct UITexture::Impl {
	Texture texture;
	Microsoft::WRL::ComPtr<ID3D12Device> device;
};

UITexture::UITexture() : impl_(std::make_unique<Impl>()) {
}

UITexture::~UITexture() = default;

bool UITexture::Load(D3D12Graphics &graphics,
                     const std::filesystem::path &path) {
	if (!impl_->texture.Load(graphics.GetDevice(), graphics.GetCommandQueue(),
	                         path)) {
		return false;
	}

	impl_->device = graphics.GetDevice();
	return true;
}

int UITexture::GetWidth() const {
	return static_cast<int>(impl_->texture.GetWidth());
}

int UITexture::GetHeight() const {
	return static_cast<int>(impl_->texture.GetHeight());
}

bool UITexture::Uses(const D3D12Graphics &graphics) const {
	return impl_->device && impl_->device.Get() == graphics.GetDevice();
}

ID3D12Resource *UITexture::GetResource() const {
	return impl_->texture.GetResource();
}
