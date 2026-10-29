#pragma once

#include <Windows.h>
#include <dxcapi.h>
#include <wrl/client.h>

class ShaderCompiler {
public:
	/// <summary>
	/// シェーダーコンパイラーを初期化する
	/// </summary>
	/// <returns>true:成功 false:失敗</returns>
	bool Initialize();

	/// <summary>
	/// シェーダーコンパイラーを終了する
	/// </summary>
	void Finalize();

private:
	Microsoft::WRL::ComPtr<IDxcUtils> utils_;
	Microsoft::WRL::ComPtr<IDxcCompiler3> compiler_;
	Microsoft::WRL::ComPtr<IDxcIncludeHandler> includeHandler_;
};
