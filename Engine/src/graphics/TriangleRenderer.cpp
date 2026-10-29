#include "graphics/TriangleRenderer.h"

bool TriangleRenderer::Initialize(
	ID3D12Device *device,
	IDxcBlob *vertexShader,
	IDxcBlob *pixelShader
) {
	if (!device || !vertexShader || !pixelShader ||
		rootSignature_ || pipelineState_) {
		return false;
	}

	if (!CreateRootSignature(device) ||
		!CreatePipelineState(device, vertexShader, pixelShader)) {
		Finalize();
		return false;
	}

	return true;
}

void TriangleRenderer::Draw(ID3D12GraphicsCommandList *commandList) {
	commandList->SetGraphicsRootSignature(rootSignature_.Get());
	commandList->SetPipelineState(pipelineState_.Get());

	commandList->IASetPrimitiveTopology(
		D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST
	);

	commandList->DrawInstanced(3, 1, 0, 0);
}

void TriangleRenderer::Finalize() {
	pipelineState_.Reset();
	rootSignature_.Reset();
}

bool TriangleRenderer::CreateRootSignature(ID3D12Device *device) {
	// 今回は定数やテクスチャを渡さないので空の構成
	D3D12_ROOT_SIGNATURE_DESC desc{};
	desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

	Microsoft::WRL::ComPtr<ID3DBlob> serialized;
	Microsoft::WRL::ComPtr<ID3DBlob> errors;

	// 設定を、デバイスへ渡せるバイナリ形式に変換する
	HRESULT hr = D3D12SerializeRootSignature(
		&desc,
		D3D_ROOT_SIGNATURE_VERSION_1,
		serialized.GetAddressOf(),
		errors.GetAddressOf()
	);

	if (errors) {
		OutputDebugStringA(
			static_cast<const char *>(errors->GetBufferPointer())
		);
	}

	if (FAILED(hr)) {
		return false;
	}

	// バイナリからルートシグネチャを作成する
	hr = device->CreateRootSignature(
		0,
		serialized->GetBufferPointer(),
		serialized->GetBufferSize(),
		IID_PPV_ARGS(rootSignature_.GetAddressOf())
	);

	return SUCCEEDED(hr);
}

bool TriangleRenderer::CreatePipelineState(
	ID3D12Device *device,
	IDxcBlob *vertexShader,
	IDxcBlob *pixelShader
) {
	D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
	desc.pRootSignature = rootSignature_.Get();

	// 使用するシェーダー
	desc.VS = {
		vertexShader->GetBufferPointer(),
		vertexShader->GetBufferSize()
	};

	desc.PS = {
		pixelShader->GetBufferPointer(),
		pixelShader->GetBufferSize()
	};

	// 三角形を塗りつぶす。表裏による除外は行わない
	desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	desc.RasterizerState.DepthClipEnable = TRUE;

	// 色の混合は無効。シェーダーの出力色をそのまま書く
	for (auto &target : desc.BlendState.RenderTarget) {
		target.BlendEnable = FALSE;
		target.LogicOpEnable = FALSE;
		target.SrcBlend = D3D12_BLEND_ONE;
		target.DestBlend = D3D12_BLEND_ZERO;
		target.BlendOp = D3D12_BLEND_OP_ADD;
		target.SrcBlendAlpha = D3D12_BLEND_ONE;
		target.DestBlendAlpha = D3D12_BLEND_ZERO;
		target.BlendOpAlpha = D3D12_BLEND_OP_ADD;
		target.LogicOp = D3D12_LOGIC_OP_NOOP;
		target.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	}

	// 今回は奥行き判定とステンシルを使わない
	desc.DepthStencilState.DepthEnable = FALSE;
	desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
	desc.DepthStencilState.StencilEnable = FALSE;

	// 頂点位置はVS内で作るため、入力レイアウトは空
	desc.InputLayout = {nullptr, 0};
	desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

	// スワップチェーンと同じ画像形式
	desc.NumRenderTargets = 1;
	desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;

	desc.SampleMask = 0xffffffffu;
	desc.SampleDesc.Count = 1;

	HRESULT hr = device->CreateGraphicsPipelineState(
		&desc,
		IID_PPV_ARGS(pipelineState_.GetAddressOf())
	);

	return SUCCEEDED(hr);
}
