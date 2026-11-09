#include "assets/ImageValidation.h"
#include "platform/windows/PathUtils.h"

#include <Windows.h>
#include <cstdint>
#include <format>
#include <fstream>
#include <stdexcept>
#include <string_view>
#include <vector>
#include <wincodec.h>
#include <wrl/client.h>

namespace {
/// <summary>
/// メモリ上の画像の先頭フレームをRGBAへ復号し、全行を確認する。失敗時は例外
/// </summary>
/// <param name="bytes">変更しない画像データ。読み取り時にサイズを確認しておく</param>
/// <param name="maximumDimension">各辺の上限</param>
void ValidateImage(std::string_view bytes, unsigned maximumDimension) {
	const auto check = [](HRESULT result) {
		if (FAILED(result)) {
			throw std::runtime_error("The image could not be decoded.");
		}
	};
	Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
	check(CoCreateInstance(CLSID_WICImagingFactory2, nullptr,
	                       CLSCTX_INPROC_SERVER,
	                       IID_PPV_ARGS(factory.GetAddressOf())));
	Microsoft::WRL::ComPtr<IWICStream> stream;
	check(factory->CreateStream(stream.GetAddressOf()));
	// WICのAPIは可変ポインターを要求するが、この読み込みでは内容を書き換えない。
	check(stream->InitializeFromMemory(
	    reinterpret_cast<BYTE *>(const_cast<char *>(bytes.data())),
	    static_cast<DWORD>(bytes.size())));
	Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
	check(factory->CreateDecoderFromStream(stream.Get(), nullptr,
	                                       WICDecodeMetadataCacheOnLoad,
	                                       decoder.GetAddressOf()));
	Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
	check(decoder->GetFrame(0, frame.GetAddressOf()));
	UINT width = 0, height = 0;
	check(frame->GetSize(&width, &height));

	if (!width || !height || width > maximumDimension ||
	    height > maximumDimension) {
		throw std::runtime_error(
		    std::format("Use an image with dimensions from 1 to {} pixels.",
		                maximumDimension));
	}

	Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
	check(factory->CreateFormatConverter(converter.GetAddressOf()));
	check(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
	                            WICBitmapDitherTypeNone, nullptr, 0,
	                            WICBitmapPaletteTypeCustom));
	const UINT stride = width * 4;
	std::vector<BYTE> row(stride);

	for (UINT y = 0; y < height; ++y) {
		WICRect rectangle{0, static_cast<INT>(y), static_cast<INT>(width), 1};
		check(converter->CopyPixels(&rectangle, stride, stride, row.data()));
	}
}

} // namespace

std::string ImageValidation::Read(const std::filesystem::path &path,
                                  unsigned maximumDimension) {
	if (maximumDimension < 1 || maximumDimension > kMaximumDimension)
		throw std::invalid_argument("Invalid image dimension limit.");
	if (path.empty() || path.native().find(L'\0') != std::wstring::npos) {
		throw std::invalid_argument("Choose a valid image path.");
	}

	const auto source = PathUtils::ResolveFromExecutable(path);

	if (!std::filesystem::is_regular_file(source)) {
		throw std::invalid_argument("The source image is not a regular file.");
	}

	std::ifstream file(source, std::ios::binary | std::ios::ate);
	const auto size = file.tellg();

	if (!file || size <= 0 ||
	    static_cast<std::uint64_t>(size) > kMaximumSourceBytes) {
		throw std::runtime_error(
		    "Choose a readable image no larger than 256 MiB.");
	}

	std::string bytes(static_cast<std::size_t>(size), '\0');
	file.seekg(0);
	file.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));

	if (!file || file.peek() != std::char_traits<char>::eof()) {
		throw std::runtime_error(
		    "The source image changed or could not be read completely.");
	}

	ValidateImage(bytes, maximumDimension);
	return bytes;
}
