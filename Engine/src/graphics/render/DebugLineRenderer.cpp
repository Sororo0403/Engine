#include "graphics/render/DebugLineRenderer.h"

#include "core/Logger.h"
#include "graphics/render/DebugLineBatch.h"
#include "graphics/render/DebugLineVertex.h"

#include <cstddef>
#include <format>
#include <iterator>

bool DebugLineRenderer::Initialize(ID3D12Device *device, IDxcBlob *vertexShader,
                                   IDxcBlob *pixelShader,
                                   DXGI_FORMAT renderTargetFormat,
                                   DXGI_FORMAT depthStencilFormat) {
	if (!device || !vertexShader || !pixelShader || rootSignature_ ||
	    pipelineState_) {
		Logger::Error("DebugLineRenderer::Initialize: "
		              "引数が無効、または初期化済みです。");
		return false;
	}

	if (!CreateRootSignature(device) ||
	    !CreatePipelineState(device, vertexShader, pixelShader,
	                         renderTargetFormat, depthStencilFormat)) {
		Finalize();
		return false;
	}

	Logger::Info("DebugLineRendererを初期化しました。");
	return true;
}

void DebugLineRenderer::Draw(ID3D12GraphicsCommandList *commandList,
                             const DebugLineBatch &batch,
                             const DirectX::XMFLOAT4X4 &view,
                             const DirectX::XMFLOAT4X4 &projection) const {
	if (batch.GetVertexCount() == 0) {
		return;
	}

	if (!commandList || !rootSignature_ || !pipelineState_) {
		Logger::Error(
		    "DebugLineRenderer::Draw: 引数が無効、または未初期化です。");
		return;
	}

	using namespace DirectX;

	Constants constants{};

	XMStoreFloat4x4(&constants.viewProjection,
	                XMLoadFloat4x4(&view) * XMLoadFloat4x4(&projection));

	commandList->SetGraphicsRootSignature(rootSignature_.Get());
	commandList->SetPipelineState(pipelineState_.Get());

	commandList->SetGraphicsRoot32BitConstants(0, kConstantCount, &constants,
	                                           0);

	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);

	const auto &vertexBufferView = batch.GetVertexBufferView();

	commandList->IASetVertexBuffers(0, 1, &vertexBufferView);

	// グリッドや選択ボックスの全頂点をまとめて描画する
	commandList->DrawInstanced(batch.GetVertexCount(), 1, 0, 0);
}

void DebugLineRenderer::Finalize() {
	pipelineState_.Reset();
	rootSignature_.Reset();
}

bool DebugLineRenderer::CreateRootSignature(ID3D12Device *device) {
	D3D12_ROOT_PARAMETER parameter{};
	parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	parameter.Constants.ShaderRegister = 0;
	parameter.Constants.RegisterSpace = 0;
	parameter.Constants.Num32BitValues = kConstantCount;
	parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

	D3D12_ROOT_SIGNATURE_DESC desc{};
	desc.NumParameters = 1;
	desc.pParameters = &parameter;
	desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	Microsoft::WRL::ComPtr<ID3DBlob> serialized;
	Microsoft::WRL::ComPtr<ID3DBlob> errors;

	HRESULT hr = D3D12SerializeRootSignature(
	    &desc, D3D_ROOT_SIGNATURE_VERSION_1, serialized.GetAddressOf(),
	    errors.GetAddressOf());

	if (errors && errors->GetBufferSize() > 1) {
		const auto message =
		    std::format("線描画用ルートシグネチャの診断: {}",
		                static_cast<const char *>(errors->GetBufferPointer()));

		if (FAILED(hr)) {
			Logger::Error(message);
		} else {
			Logger::Warning(message);
		}
	}

	if (FAILED(hr)) {
		Logger::Error(
		    std::format("線描画用ルートシグネチャの変換に失敗しました。"
		                "HRESULT=0x{:08X}",
		                static_cast<unsigned long>(hr)));
		return false;
	}

	hr = device->CreateRootSignature(
	    0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
	    IID_PPV_ARGS(rootSignature_.GetAddressOf()));

	if (FAILED(hr)) {
		Logger::Error(
		    std::format("線描画用ルートシグネチャの作成に失敗しました。"
		                "HRESULT=0x{:08X}",
		                static_cast<unsigned long>(hr)));
		return false;
	}

	return true;
}

bool DebugLineRenderer::CreatePipelineState(ID3D12Device *device,
                                            IDxcBlob *vertexShader,
                                            IDxcBlob *pixelShader,
                                            DXGI_FORMAT renderTargetFormat,
                                            DXGI_FORMAT depthStencilFormat) {
	D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
	desc.pRootSignature = rootSignature_.Get();

	desc.VS = {
	    vertexShader->GetBufferPointer(),
	    vertexShader->GetBufferSize(),
	};

	desc.PS = {
	    pixelShader->GetBufferPointer(),
	    pixelShader->GetBufferSize(),
	};

	desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	desc.RasterizerState.DepthClipEnable = TRUE;

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

	// モデルの深度を参照し、線の深度は保存しない
	desc.DepthStencilState.DepthEnable = TRUE;
	desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	desc.DepthStencilState.StencilEnable = FALSE;
	desc.DepthStencilState.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
	desc.DepthStencilState.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;

	D3D12_DEPTH_STENCILOP_DESC stencil{};
	stencil.StencilFailOp = D3D12_STENCIL_OP_KEEP;
	stencil.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
	stencil.StencilPassOp = D3D12_STENCIL_OP_KEEP;
	stencil.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;

	desc.DepthStencilState.FrontFace = stencil;
	desc.DepthStencilState.BackFace = stencil;

	const D3D12_INPUT_ELEMENT_DESC inputElements[] = {
	    {
	        "POSITION",
	        0,
	        DXGI_FORMAT_R32G32B32_FLOAT,
	        0,
	        static_cast<UINT>(offsetof(DebugLineVertex, position)),
	        D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
	        0,
	    },
	    {
	        "COLOR",
	        0,
	        DXGI_FORMAT_R32G32B32A32_FLOAT,
	        0,
	        static_cast<UINT>(offsetof(DebugLineVertex, color)),
	        D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
	        0,
	    },
	};

	desc.InputLayout = {
	    inputElements,
	    static_cast<UINT>(std::size(inputElements)),
	};

	desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;

	desc.NumRenderTargets = 1;
	desc.RTVFormats[0] = renderTargetFormat;
	desc.DSVFormat = depthStencilFormat;
	desc.SampleMask = 0xffffffffu;
	desc.SampleDesc.Count = 1;

	const HRESULT hr = device->CreateGraphicsPipelineState(
	    &desc, IID_PPV_ARGS(pipelineState_.GetAddressOf()));

	if (FAILED(hr)) {
		Logger::Error(
		    std::format("線描画用PSOの作成に失敗しました。HRESULT=0x{:08X}",
		                static_cast<unsigned long>(hr)));
		return false;
	}

	return true;
}
