#include "graphics/ShaderCompiler.h"

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

void ShaderCompiler::Finalize() {
	includeHandler_.Reset();
	compiler_.Reset();
	utils_.Reset();
}
