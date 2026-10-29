#include "graphics/TriangleRenderer.h"
#include "core/Logger.h"
#include "graphics/Vertex.h"

#include <cstddef>
#include <cstring>
#include <format>
#include <iterator>

bool TriangleRenderer::Initialize(ID3D12Device *device, IDxcBlob *vertexShader,
                                  IDxcBlob *pixelShader) {
	if (!device || !vertexShader || !pixelShader || rootSignature_ ||
	    pipelineState_) {
		Logger::Error(
		    "TriangleRenderer::Initialize: 引数が無効、または初期化済みです。");
		return false;
	}

	if (!CreateRootSignature(device) ||
	    !CreatePipelineState(device, vertexShader, pixelShader) ||
	    !CreateVertexBuffer(device) || !CreateConstantBuffer(device)) {
		Finalize();
		return false;
	}

	Logger::Info("三角形のルートシグネチャとPSOを作成しました。");
	return true;
}

void TriangleRenderer::Update() {
	if (!mappedWorld_) {
		Logger::Error("TriangleRenderer::Update: 定数バッファが未初期化です。");
		return;
	}

	using namespace DirectX;

	const XMMATRIX scale = XMMatrixScaling(
	    transform_.scale.x, transform_.scale.y, transform_.scale.z);

	const XMMATRIX rotation = XMMatrixRotationRollPitchYaw(
	    transform_.rotation.x, transform_.rotation.y, transform_.rotation.z);

	const XMMATRIX translation =
	    XMMatrixTranslation(transform_.translation.x, transform_.translation.y,
	                        transform_.translation.z);

	// 拡大縮小 → 回転 → 移動の順で適用する
	const XMMATRIX world = scale * rotation * translation;

	XMStoreFloat4x4(mappedWorld_, world);
}

void TriangleRenderer::Draw(ID3D12GraphicsCommandList *commandList) {
	commandList->SetGraphicsRootSignature(rootSignature_.Get());

	commandList->SetGraphicsRootConstantBufferView(
	    0, constantBuffer_->GetGPUVirtualAddress());

	commandList->SetPipelineState(pipelineState_.Get());

	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// スロット0へ頂点バッファを設定する
	commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);

	commandList->DrawInstanced(3, 1, 0, 0);
}

void TriangleRenderer::Finalize() {
	if (constantBuffer_ && mappedWorld_) {
		constantBuffer_->Unmap(0, nullptr);
	}

	mappedWorld_ = nullptr;
	constantBuffer_.Reset();

	vertexBuffer_.Reset();
	vertexBufferView_ = {};

	pipelineState_.Reset();
	rootSignature_.Reset();
}

void TriangleRenderer::SetTransform(const Transform &transform) {
	transform_ = transform;
}

bool TriangleRenderer::CreateRootSignature(ID3D12Device *device) {
	// VSのb0へ定数バッファを渡す
	D3D12_ROOT_PARAMETER parameter{};
	parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	parameter.Descriptor.ShaderRegister = 0;
	parameter.Descriptor.RegisterSpace = 0;
	parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

	D3D12_ROOT_SIGNATURE_DESC desc{};
	desc.NumParameters = 1;
	desc.pParameters = &parameter;
	desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	Microsoft::WRL::ComPtr<ID3DBlob> serialized;
	Microsoft::WRL::ComPtr<ID3DBlob> errors;

	// 設定を、デバイスへ渡せるバイナリ形式に変換する
	HRESULT hr = D3D12SerializeRootSignature(
	    &desc, D3D_ROOT_SIGNATURE_VERSION_1, serialized.GetAddressOf(),
	    errors.GetAddressOf());

	if (errors && errors->GetBufferSize() > 1) {
		const std::string message =
		    std::format("ルートシグネチャの診断: {}",
		                static_cast<const char *>(errors->GetBufferPointer()));
		if (FAILED(hr)) {
			Logger::Error(message);
		} else {
			Logger::Warning(message);
		}
	}

	if (FAILED(hr)) {
		Logger::Error(std::format(
		    "D3D12SerializeRootSignatureに失敗しました。HRESULT=0x{:08X}",
		    static_cast<unsigned long>(hr)));
		return false;
	}

	// バイナリからルートシグネチャを作成する
	hr = device->CreateRootSignature(
	    0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
	    IID_PPV_ARGS(rootSignature_.GetAddressOf()));

	if (FAILED(hr)) {
		Logger::Error(
		    std::format("CreateRootSignatureに失敗しました。HRESULT=0x{:08X}",
		                static_cast<unsigned long>(hr)));
		return false;
	}

	return true;
}

bool TriangleRenderer::CreatePipelineState(ID3D12Device *device,
                                           IDxcBlob *vertexShader,
                                           IDxcBlob *pixelShader) {
	D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
	desc.pRootSignature = rootSignature_.Get();

	// 使用するシェーダー
	desc.VS = {vertexShader->GetBufferPointer(), vertexShader->GetBufferSize()};

	desc.PS = {pixelShader->GetBufferPointer(), pixelShader->GetBufferSize()};

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

	const D3D12_INPUT_ELEMENT_DESC inputElements[] = {
	    {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
	     static_cast<UINT>(offsetof(Vertex, position)),
	     D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
	    {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
	     static_cast<UINT>(offsetof(Vertex, color)),
	     D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};

	desc.InputLayout = {inputElements,
	                    static_cast<UINT>(std::size(inputElements))};

	desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

	// スワップチェーンと同じ画像形式
	desc.NumRenderTargets = 1;
	desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;

	desc.SampleMask = 0xffffffffu;
	desc.SampleDesc.Count = 1;

	HRESULT hr = device->CreateGraphicsPipelineState(
	    &desc, IID_PPV_ARGS(pipelineState_.GetAddressOf()));

	if (FAILED(hr)) {
		Logger::Error(std::format(
		    "CreateGraphicsPipelineStateに失敗しました。HRESULT=0x{:08X}",
		    static_cast<unsigned long>(hr)));
		return false;
	}

	return true;
}

bool TriangleRenderer::CreateVertexBuffer(ID3D12Device *device) {
	const Vertex vertices[] = {
	    {{0.0f, 0.5f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
	    {{0.5f, -0.5f, 0.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},
	    {{-0.5f, -0.5f, 0.0f}, {0.0f, 0.0f, 1.0f, 1.0f}}};

	// CPUから書き込めるメモリを使う
	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
	heapProperties.CreationNodeMask = 1;
	heapProperties.VisibleNodeMask = 1;

	// 頂点データを格納するバッファの設定
	D3D12_RESOURCE_DESC desc{};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Width = sizeof(vertices);
	desc.Height = 1;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.SampleDesc.Count = 1;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	HRESULT hr = device->CreateCommittedResource(
	    &heapProperties, D3D12_HEAP_FLAG_NONE, &desc,
	    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
	    IID_PPV_ARGS(vertexBuffer_.GetAddressOf()));

	if (FAILED(hr)) {
		Logger::Error(
		    std::format("頂点バッファの作成に失敗しました。HRESULT=0x{:08X}",
		                static_cast<unsigned long>(hr)));
		return false;
	}

	// CPUからバッファへアクセスするためのアドレスを取得する
	void *mapped = nullptr;

	// CPUから読み取る範囲はない
	const D3D12_RANGE readRange{0, 0};

	hr = vertexBuffer_->Map(0, &readRange, &mapped);

	if (FAILED(hr)) {
		Logger::Error(
		    std::format("頂点バッファのMapに失敗しました。HRESULT=0x{:08X}",
		                static_cast<unsigned long>(hr)));
		return false;
	}

	std::memcpy(mapped, vertices, sizeof(vertices));

	// バッファ全体に書き込んだことを伝える
	const D3D12_RANGE writtenRange{0, sizeof(vertices)};
	vertexBuffer_->Unmap(0, &writtenRange);

	// GPUへ渡す、バッファの読み方を設定する
	vertexBufferView_.BufferLocation = vertexBuffer_->GetGPUVirtualAddress();
	vertexBufferView_.SizeInBytes = static_cast<UINT>(sizeof(vertices));
	vertexBufferView_.StrideInBytes = static_cast<UINT>(sizeof(Vertex));

	Logger::Info("三角形の頂点バッファを作成しました。");

	return true;
}

bool TriangleRenderer::CreateConstantBuffer(ID3D12Device *device) {
	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
	heapProperties.CreationNodeMask = 1;
	heapProperties.VisibleNodeMask = 1;

	// 行列1個は64バイトだが、定数バッファ用に256バイト確保する
	D3D12_RESOURCE_DESC desc{};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Width = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
	desc.Height = 1;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.SampleDesc.Count = 1;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	HRESULT hr = device->CreateCommittedResource(
	    &heapProperties, D3D12_HEAP_FLAG_NONE, &desc,
	    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
	    IID_PPV_ARGS(constantBuffer_.GetAddressOf()));

	if (FAILED(hr)) {
		Logger::Error(
		    std::format("定数バッファの作成に失敗しました。HRESULT=0x{:08X}",
		                static_cast<unsigned long>(hr)));
		return false;
	}

	void *mapped = nullptr;
	const D3D12_RANGE readRange{0, 0};

	hr = constantBuffer_->Map(0, &readRange, &mapped);

	if (FAILED(hr)) {
		Logger::Error(
		    std::format("定数バッファのMapに失敗しました。HRESULT=0x{:08X}",
		                static_cast<unsigned long>(hr)));
		return false;
	}

	mappedWorld_ = static_cast<DirectX::XMFLOAT4X4 *>(mapped);

	// 最初は変形しない単位行列を入れる
	DirectX::XMStoreFloat4x4(mappedWorld_, DirectX::XMMatrixIdentity());

	Logger::Info("変換行列用の定数バッファを作成しました。");

	return true;
}
