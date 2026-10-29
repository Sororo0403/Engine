#include "graphics/ShaderCompiler.h"

#include <filesystem>
#include <iterator>

bool ShaderCompiler::Initialize() {
	if (utils_ || compiler_ || includeHandler_) {
		return false;
	}

	HRESULT hr = DxcCreateInstance(
		CLSID_DxcUtils,
		IID_PPV_ARGS(utils_.GetAddressOf())
	);

	if (FAILED(hr)) {
		Finalize();
		return false;
	}

	hr = DxcCreateInstance(
		CLSID_DxcCompiler,
		IID_PPV_ARGS(compiler_.GetAddressOf())
	);

	if (FAILED(hr)) {
		Finalize();
		return false;
	}

	hr = utils_->CreateDefaultIncludeHandler(
		includeHandler_.GetAddressOf()
	);

	if (FAILED(hr)) {
		Finalize();
		return false;
	}

	return true;
}

Microsoft::WRL::ComPtr<IDxcBlob> ShaderCompiler::Compile(
	const std::wstring &filePath,
	const std::wstring &entryPoint,
	const std::wstring &profile
) {
	if (!utils_ || !compiler_ || !includeHandler_) {
		return nullptr;
	}

	// HLSLファイルを読み込む
	Microsoft::WRL::ComPtr<IDxcBlobEncoding> source;

	HRESULT hr = utils_->LoadFile(
		filePath.c_str(),
		nullptr,
		source.GetAddressOf()
	);

	if (FAILED(hr)) {
		return nullptr;
	}

	// ソースファイルはUTF-8で保存する
	DxcBuffer buffer{};
	buffer.Ptr = source->GetBufferPointer();
	buffer.Size = source->GetBufferSize();
	buffer.Encoding = DXC_CP_UTF8;

	// 同じフォルダにある.hlsliを検索できるようにする
	std::wstring includeDirectory =
		std::filesystem::path(filePath).parent_path().wstring();

	if (includeDirectory.empty()) {
		includeDirectory = L".";
	}

	const wchar_t *arguments[] = {
		filePath.c_str(),
		L"-E", entryPoint.c_str(),
		L"-T", profile.c_str(),
		L"-I", includeDirectory.c_str(),
		L"-HV", L"2021",
#if defined(_DEBUG)
		L"-Zi",
		L"-Qembed_debug",
		L"-Od",
#else
		L"-O3",
#endif
	};

	Microsoft::WRL::ComPtr<IDxcResult> result;

	hr = compiler_->Compile(
		&buffer,
		arguments,
		static_cast<UINT32>(std::size(arguments)),
		includeHandler_.Get(),
		IID_PPV_ARGS(result.GetAddressOf())
	);

	if (FAILED(hr)) {
		return nullptr;
	}

	// 呼び出しの成功とは別に、HLSLのコンパイル結果を確認する
	HRESULT status = E_FAIL;
	hr = result->GetStatus(&status);

	if (FAILED(hr) || FAILED(status)) {
		return nullptr;
	}

	// コンパイル済みデータを取得する
	Microsoft::WRL::ComPtr<IDxcBlob> shader;

	hr = result->GetOutput(
		DXC_OUT_OBJECT,
		IID_PPV_ARGS(shader.GetAddressOf()),
		nullptr
	);

	if (FAILED(hr)) {
		return nullptr;
	}

	return shader;
}

void ShaderCompiler::Finalize() {
	includeHandler_.Reset();
	compiler_.Reset();
	utils_.Reset();
}
