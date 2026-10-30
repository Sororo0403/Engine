#include "graphics/render/ShaderCompiler.h"
#include "core/Logger.h"
#include "platform/windows/StringConversion.h"

#include <filesystem>
#include <format>
#include <iterator>

bool ShaderCompiler::Initialize() {
	if (utils_ || compiler_ || includeHandler_) {
		Logger::Error("ShaderCompiler::Initialize: 初期化済みです。");
		return false;
	}

	HRESULT hr =
	    DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(utils_.GetAddressOf()));

	if (FAILED(hr)) {
		Logger::Error(std::format(
		    "DxcCreateInstance(DxcUtils)に失敗しました。HRESULT=0x{:08X}",
		    static_cast<unsigned long>(hr)));
		Finalize();
		return false;
	}

	hr = DxcCreateInstance(CLSID_DxcCompiler,
	                       IID_PPV_ARGS(compiler_.GetAddressOf()));

	if (FAILED(hr)) {
		Logger::Error(std::format(
		    "DxcCreateInstance(DxcCompiler)に失敗しました。HRESULT=0x{:08X}",
		    static_cast<unsigned long>(hr)));
		Finalize();
		return false;
	}

	hr = utils_->CreateDefaultIncludeHandler(includeHandler_.GetAddressOf());

	if (FAILED(hr)) {
		Logger::Error(std::format(
		    "CreateDefaultIncludeHandlerに失敗しました。HRESULT=0x{:08X}",
		    static_cast<unsigned long>(hr)));
		Finalize();
		return false;
	}

	Logger::Info("DXCを初期化しました。");
	return true;
}

Microsoft::WRL::ComPtr<IDxcBlob>
ShaderCompiler::Compile(const std::wstring &filePath,
                        const std::wstring &entryPoint,
                        const std::wstring &profile) {
	if (!utils_ || !compiler_ || !includeHandler_) {
		Logger::Error("ShaderCompiler::Compile: DXCが初期化されていません。");
		return nullptr;
	}

	if (filePath.empty() || entryPoint.empty() || profile.empty()) {
		Logger::Error(
		    "ShaderCompiler::Compile: "
		    "パス、エントリーポイント、プロファイルを指定してください。");
		return nullptr;
	}

	const std::string context = std::format(
	    "{} (entry={}, profile={})", StringConversion::WideToUtf8(filePath),
	    StringConversion::WideToUtf8(entryPoint),
	    StringConversion::WideToUtf8(profile));

	// HLSLファイルを読み込む
	Microsoft::WRL::ComPtr<IDxcBlobEncoding> source;

	HRESULT hr =
	    utils_->LoadFile(filePath.c_str(), nullptr, source.GetAddressOf());

	if (FAILED(hr)) {
		Logger::Error(std::format(
		    "HLSLファイルを読み込めませんでした: {} HRESULT=0x{:08X}", context,
		    static_cast<unsigned long>(hr)));
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
	    L"-E",
	    entryPoint.c_str(),
	    L"-T",
	    profile.c_str(),
	    L"-I",
	    includeDirectory.c_str(),
	    L"-HV",
	    L"2021",
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
	    &buffer, arguments, static_cast<UINT32>(std::size(arguments)),
	    includeHandler_.Get(), IID_PPV_ARGS(result.GetAddressOf()));

	if (FAILED(hr)) {
		Logger::Error(
		    std::format("IDxcCompiler3::Compileの呼び出しに失敗しました: {} "
		                "HRESULT=0x{:08X}",
		                context, static_cast<unsigned long>(hr)));
		return nullptr;
	}

	// 呼び出しの成功とは別に、HLSLのコンパイル結果を確認する
	HRESULT status = E_FAIL;
	hr = result->GetStatus(&status);

	if (FAILED(hr)) {
		Logger::Error(std::format(
		    "IDxcResult::GetStatusに失敗しました: {} HRESULT=0x{:08X}", context,
		    static_cast<unsigned long>(hr)));
		return nullptr;
	}

	// コンパイル失敗時の詳細と、成功時の警告をどちらも残す。
	Microsoft::WRL::ComPtr<IDxcBlobUtf8> messages;
	hr = result->GetOutput(DXC_OUT_ERRORS,
	                       IID_PPV_ARGS(messages.GetAddressOf()), nullptr);

	if (FAILED(hr)) {
		Logger::Error(std::format(
		    "DXCの診断メッセージを取得できませんでした: {} HRESULT=0x{:08X}",
		    context, static_cast<unsigned long>(hr)));
		return nullptr;
	}

	if (messages && messages->GetStringLength() > 0) {
		std::string_view diagnostics(messages->GetStringPointer(),
		                             messages->GetStringLength());
		while (!diagnostics.empty() &&
		       (diagnostics.back() == '\n' || diagnostics.back() == '\r')) {
			diagnostics.remove_suffix(1);
		}

		const std::string message =
		    std::format("HLSL診断: {}\n{}", context, diagnostics);
		if (FAILED(status)) {
			Logger::Error(message);
		} else {
			Logger::Warning(message);
		}
	}

	if (FAILED(status)) {
		Logger::Error(
		    std::format("HLSLのコンパイルに失敗しました: {} HRESULT=0x{:08X}",
		                context, static_cast<unsigned long>(status)));
		return nullptr;
	}

	// コンパイル済みデータを取得する
	Microsoft::WRL::ComPtr<IDxcBlob> shader;

	hr = result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(shader.GetAddressOf()),
	                       nullptr);

	if (FAILED(hr)) {
		Logger::Error(std::format("コンパイル済みシェーダーを取得できませんでし"
		                          "た: {} HRESULT=0x{:08X}",
		                          context, static_cast<unsigned long>(hr)));
		return nullptr;
	}

	Logger::Info(std::format("シェーダーをコンパイルしました: {}", context));
	return shader;
}

void ShaderCompiler::Finalize() {
	includeHandler_.Reset();
	compiler_.Reset();
	utils_.Reset();
}
