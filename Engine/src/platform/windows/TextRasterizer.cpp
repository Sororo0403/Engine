#include "platform/windows/TextRasterizer.h"
#include "platform/windows/PathUtils.h"
#include "platform/windows/StringConversion.h"
#include "scene/TextComponent.h"
#include <cmath>
#include <d2d1.h>
#include <dwrite_3.h>
#include <exception>
#include <format>
#include <stdexcept>
#include <span>
#include <array>
#include <wincodec.h>
#include <wrl/client.h>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

using Microsoft::WRL::ComPtr;

struct TextRasterizer::Impl {
	ComPtr<ID2D1Factory> drawing;
	ComPtr<IDWriteFactory> writing;
	ComPtr<IWICImagingFactory> imaging;
	ComPtr<IDWriteFontCollection1> bundledFonts;

	void InitializeWriting();
	void LoadBundledFonts();
	ComPtr<IDWriteFontCollection1> LoadFonts(
	    std::span<const std::filesystem::path> paths);
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

void TextRasterizer::Impl::InitializeWriting() {
	if (!writing)
		Require(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
		                            reinterpret_cast<IUnknown **>(writing.GetAddressOf())),
		        "DWriteCreateFactory");
}

void TextRasterizer::ValidateFontAsset(const std::filesystem::path &path) {
	if (!PathUtils::IsFontFile(path))
		throw std::runtime_error("Choose a TTF or OTF font file.");
	Impl validation;
	validation.InitializeWriting();
	const std::array paths{PathUtils::ResolveFromExecutable(path)};
	validation.LoadFonts(paths);
}

void TextRasterizer::Impl::LoadBundledFonts() {
	if (bundledFonts) {
		return;
	}

	const auto directory = PathUtils::ResolveFromExecutable(L"resources/engine/fonts");
	const std::array paths{directory / L"MPLUS1p-Regular.ttf",
	                       directory / L"MPLUS1p-Bold.ttf"};
	bundledFonts = LoadFonts(paths);
}

ComPtr<IDWriteFontCollection1> TextRasterizer::Impl::LoadFonts(
    std::span<const std::filesystem::path> paths) {
	ComPtr<IDWriteFactory5> factory;
	Require(writing.As(&factory), "Get DirectWrite font factory");
	ComPtr<IDWriteFontSetBuilder1> fonts;
	Require(factory->CreateFontSetBuilder(fonts.GetAddressOf()),
	        "Create font set builder");

	for (const auto &path : paths) {
		ComPtr<IDWriteFontFile> file;
		Require(factory->CreateFontFileReference(path.c_str(), nullptr, file.GetAddressOf()),
		        "Open font file");
		Require(fonts->AddFontFile(file.Get()), "Add font file");
	}

	ComPtr<IDWriteFontSet> set;
	Require(fonts->CreateFontSet(set.GetAddressOf()), "Create font set");
	ComPtr<IDWriteFontCollection1> collection;
	Require(factory->CreateFontCollectionFromFontSet(set.Get(), collection.GetAddressOf()),
	        "Create font collection");
	if (collection->GetFontFamilyCount() == 0)
		throw std::runtime_error("The font file contains no supported font families.");
	return collection;
}

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
		impl_->InitializeWriting();
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
		auto family = StringConversion::Utf8ToWide(text.fontFamily);
		ComPtr<IDWriteFontCollection1> collection;
		if (!text.fontAsset.empty()) {
			const std::array paths{PathUtils::ResolveFromExecutable(text.fontAsset)};
			collection = impl_->LoadFonts(paths);
			ComPtr<IDWriteFontFamily> assetFamily;
			Require(static_cast<IDWriteFontCollection *>(collection.Get())->GetFontFamily(
			            0, assetFamily.GetAddressOf()), "Read asset font family");
			ComPtr<IDWriteLocalizedStrings> names;
			Require(assetFamily->GetFamilyNames(names.GetAddressOf()), "Read font family names");
			UINT32 length = 0;
			Require(names->GetStringLength(0, &length), "Read font family name length");
			family.resize(length + 1);
			Require(names->GetString(0, family.data(), length + 1), "Read font family name");
			family.resize(length);
		} else if (CompareStringOrdinal(family.c_str(), -1, L"M PLUS 1p", -1, TRUE) == CSTR_EQUAL) {
			impl_->LoadBundledFonts();
			collection = impl_->bundledFonts;
		}
		ComPtr<IDWriteTextFormat> format;
		Require(
		    impl_->writing->CreateTextFormat(
		        family.c_str(), collection.Get(),
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
