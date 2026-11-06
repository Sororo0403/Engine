#include "platform/windows/TextRasterizer.h"
#include "platform/windows/StringConversion.h"
#include "scene/TextComponent.h"
#include <cmath>
#include <d2d1.h>
#include <dwrite.h>
#include <exception>
#include <format>
#include <stdexcept>
#include <wincodec.h>
#include <wrl/client.h>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

using Microsoft::WRL::ComPtr;

struct TextRasterizer::Impl {
	ComPtr<ID2D1Factory> drawing;
	ComPtr<IDWriteFactory> writing;
	ComPtr<IWICImagingFactory> imaging;
};

namespace {
/// <summary>
/// COM処理の失敗を、呼び出し名とHRESULTを持つ例外へ変換する
/// </summary>
/// <param name="result">確認する処理結果</param>
/// <param name="operation">識別する処理名</param>
void Require(HRESULT result, const char *operation) {
	if (FAILED(result))
		throw std::runtime_error(
		    std::format("{} failed: 0x{:08X}", operation,
		                static_cast<unsigned long>(result)));
}
} // namespace

TextRasterizer::TextRasterizer() : impl_(std::make_unique<Impl>()) {
}

TextRasterizer::~TextRasterizer() = default;

bool TextRasterizer::Rasterize(const TextComponent &text, int width, int height,
                               float scale, std::vector<std::uint8_t> &pixels,
                               std::string &error) {
	try {
		if (!text.IsValid() || width < 1 || height < 1 || width > 4096 ||
		    height > 4096 || !std::isfinite(scale) || scale <= 0 ||
		    !std::isfinite(text.fontSize * scale))
			throw std::runtime_error(
			    "Text rasterization settings are invalid.");
		if (!impl_->drawing)
			Require(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
			                          impl_->drawing.GetAddressOf()),
			        "D2D1CreateFactory");
		if (!impl_->writing)
			Require(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
			                            __uuidof(IDWriteFactory),
			                            reinterpret_cast<IUnknown **>(
			                                impl_->writing.GetAddressOf())),
			        "DWriteCreateFactory");
		if (!impl_->imaging)
			Require(CoCreateInstance(
			            CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
			            IID_PPV_ARGS(impl_->imaging.GetAddressOf())),
			        "Create WIC factory");
		ComPtr<IWICBitmap> bitmap;
		Require(impl_->imaging->CreateBitmap(
		            static_cast<UINT>(width), static_cast<UINT>(height),
		            GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad,
		            bitmap.GetAddressOf()),
		        "Create text bitmap");
		ComPtr<ID2D1RenderTarget> target;
		const auto properties = D2D1::RenderTargetProperties(
		    D2D1_RENDER_TARGET_TYPE_SOFTWARE,
		    D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
		                      D2D1_ALPHA_MODE_PREMULTIPLIED),
		    96, 96);
		Require(impl_->drawing->CreateWicBitmapRenderTarget(
		            bitmap.Get(), properties, target.GetAddressOf()),
		        "Create text render target");
		const auto family = StringConversion::Utf8ToWide(text.fontFamily);
		ComPtr<IDWriteTextFormat> format;
		Require(
		    impl_->writing->CreateTextFormat(
		        family.c_str(), nullptr,
		        text.bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
		        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
		        text.fontSize * scale, L"", format.GetAddressOf()),
		    "Create text format");
		Require(format->SetTextAlignment(
		            text.alignment == TextComponent::Alignment::LEFT
		                ? DWRITE_TEXT_ALIGNMENT_LEADING
		            : text.alignment == TextComponent::Alignment::CENTER
		                ? DWRITE_TEXT_ALIGNMENT_CENTER
		                : DWRITE_TEXT_ALIGNMENT_TRAILING),
		        "Set text alignment");
		Require(
		    format->SetParagraphAlignment(
		        text.verticalAlignment == TextComponent::VerticalAlignment::TOP
		            ? DWRITE_PARAGRAPH_ALIGNMENT_NEAR
		        : text.verticalAlignment ==
		                TextComponent::VerticalAlignment::MIDDLE
		            ? DWRITE_PARAGRAPH_ALIGNMENT_CENTER
		            : DWRITE_PARAGRAPH_ALIGNMENT_FAR),
		    "Set vertical text alignment");
		Require(format->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP),
		        "Set text wrapping");
		const auto content = StringConversion::Utf8ToWide(text.text);
		ComPtr<IDWriteTextLayout> layout;
		Require(impl_->writing->CreateTextLayout(
		            content.data(), static_cast<UINT32>(content.size()),
		            format.Get(), static_cast<float>(width),
		            static_cast<float>(height), layout.GetAddressOf()),
		        "Create text layout");
		ComPtr<ID2D1SolidColorBrush> brush;
		Require(target->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1),
		                                      brush.GetAddressOf()),
		        "Create text brush");
		target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
		target->BeginDraw();
		target->Clear(D2D1::ColorF(0, 0, 0, 0));
		target->DrawTextLayout(D2D1::Point2F(0, 0), layout.Get(), brush.Get(),
		                       D2D1_DRAW_TEXT_OPTIONS_CLIP);
		Require(target->EndDraw(), "Draw text");
		std::vector<std::uint8_t> candidate(static_cast<std::size_t>(width) *
		                                    height * 4);
		Require(bitmap->CopyPixels(nullptr, static_cast<UINT>(width) * 4,
		                           static_cast<UINT>(candidate.size()),
		                           candidate.data()),
		        "Read text bitmap");
		for (std::size_t i = 0; i < candidate.size(); i += 4)
			candidate[i] = candidate[i + 1] = candidate[i + 2] = 255;
		pixels.swap(candidate);
		error.clear();
		return true;
	} catch (const std::exception &exception) {
		error = exception.what();
		return false;
	}
}
