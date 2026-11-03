#include "assets/TextureImporter.h"
#include "assets/AssetFolders.h"

#include "platform/windows/FileUtils.h"
#include "platform/windows/PathUtils.h"
#include <Windows.h>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <vector>
#include <wincodec.h>
#include <wrl/client.h>

void TextureImporter::ValidateImage(std::string_view bytes) {
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

	if (!width || !height || width > kMaximumDimension ||
	    height > kMaximumDimension) {
		throw std::runtime_error(
		    "Use an image with dimensions from 1 to 16384 pixels.");
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

bool TextureImporter::Import(const TextureImportOptions &options,
                             std::filesystem::path &imported,
                             std::string &error) {
	try {
		if (options.source.empty() ||
		    options.source.native().find(L'\0') != std::wstring::npos ||
		    !PathUtils::IsImageFile(options.source) ||
		    !PathUtils::IsValidFileName(options.fileName) ||
		    !PathUtils::HasExtension(
		        options.fileName,
		        PathUtils::ToUtf8(options.source.extension()))) {
			throw std::invalid_argument("Choose an image and a valid file name "
			                            "with the same extension.");
		}

		const auto folder =
		    AssetFolders::Resolve(options.assetRoot, options.folder);

		const auto destination = folder / options.fileName;
		if (std::filesystem::exists(destination)) {
			throw std::runtime_error(
			    "An asset with that name already exists. Choose another name.");
		}

		const auto source = PathUtils::ResolveFromExecutable(options.source);
		if (!std::filesystem::is_regular_file(source)) {
			throw std::invalid_argument(
			    "The source image is not a regular file.");
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

		ValidateImage(bytes);
		auto selected =
		    destination.lexically_relative(PathUtils::GetExecutableDirectory());
		if (selected.empty()) {
			throw std::runtime_error(
			    "Could not create the imported image reference.");
		}

		if (!FileUtils::WriteBytesAtomically(
		        destination,
		        std::as_bytes(std::span{bytes.data(), bytes.size()}), error,
		        false)) {
			return false;
		}

		imported.swap(selected);
		error.clear();
		return true;
	} catch (const std::exception &exception) {
		error = exception.what();
		return false;
	}
}
