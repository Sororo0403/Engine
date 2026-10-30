#pragma once

#include <Windows.h>
#include <dxcapi.h>
#include <string>
#include <wrl/client.h>

class ShaderCompiler {
  public:
	/// <summary>
	/// シェーダーコンパイラーを初期化する
	/// </summary>
	/// <returns>true:成功 false:失敗</returns>
	bool Initialize();

	/// <summary>
	/// HLSLファイルをコンパイルする
	/// </summary>
	/// <param name="filePath">HLSLファイルのパス</param>
	/// <param name="entryPoint">実行開始関数の名前</param>
	/// <param name="profile">シェーダーの種類とモデル</param>
	/// <returns>成功:コンパイル済みデータ 失敗:nullptr</returns>
	Microsoft::WRL::ComPtr<IDxcBlob> Compile(const std::wstring &filePath,
	                                         const std::wstring &entryPoint,
	                                         const std::wstring &profile);

	/// <summary>
	/// シェーダーコンパイラーを終了する
	/// </summary>
	void Finalize();

  private:
	Microsoft::WRL::ComPtr<IDxcUtils> utils_;
	Microsoft::WRL::ComPtr<IDxcCompiler3> compiler_;
	Microsoft::WRL::ComPtr<IDxcIncludeHandler> includeHandler_;
};
