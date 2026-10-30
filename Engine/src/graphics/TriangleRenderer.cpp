#include "graphics/TriangleRenderer.h"
#include "core/Logger.h"
#include "graphics/Camera.h"
#include "graphics/Vertex.h"

#include <directxtk12/BufferHelpers.h>

#include <cstddef>
#include <cstdint>
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
	    !CreateVertexBuffer(device) || !CreateIndexBuffer(device) ||
	    !CreateConstantBuffer(device)) {
		Finalize();
		return false;
	}

	Logger::Info("三角形のルートシグネチャとPSOを作成しました。");
	return true;
}

void TriangleRenderer::Update(const Camera &camera) {
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

	const XMMATRIX view = XMLoadFloat4x4(&camera.GetViewMatrix());

	const XMMATRIX projection = XMLoadFloat4x4(&camera.GetProjectionMatrix());

	// 物体の変形 → カメラから見た座標 → 投影
	const XMMATRIX wvp = world * view * projection;

	XMStoreFloat4x4(mappedWorld_, wvp);
}

void TriangleRenderer::Draw(ID3D12GraphicsCommandList *commandList,
                            D3D12_GPU_DESCRIPTOR_HANDLE textureSrv) {
	commandList->SetGraphicsRootSignature(rootSignature_.Get());
	commandList->SetPipelineState(pipelineState_.Get());

	// ルートパラメーター0：変換行列
	commandList->SetGraphicsRootConstantBufferView(
	    0, constantBuffer_->GetGPUVirtualAddress());

	// ルートパラメーター1：テクスチャ
	commandList->SetGraphicsRootDescriptorTable(1, textureSrv);

	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);
	commandList->IASetIndexBuffer(&indexBufferView_);

	commandList->DrawIndexedInstanced(indexCount_, 1, 0, 0, 0);
}

void TriangleRenderer::Finalize() {
	if (constantBuffer_ && mappedWorld_) {
		constantBuffer_->Unmap(0, nullptr);
	}

	mappedWorld_ = nullptr;
	constantBuffer_.Reset();

	vertexBuffer_.Reset();
	vertexBufferView_ = {};

	indexBuffer_.Reset();
	indexBufferView_ = {};
	indexCount_ = 0;

	pipelineState_.Reset();
	rootSignature_.Reset();
}

void TriangleRenderer::SetTransform(const Transform &transform) {
	transform_ = transform;
}

bool TriangleRenderer::CreateRootSignature(ID3D12Device *device) {
	// テクスチャ1枚を、PSのt0へ渡す
	D3D12_DESCRIPTOR_RANGE range{};
	range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	range.NumDescriptors = 1;
	range.BaseShaderRegister = 0;
	range.RegisterSpace = 0;
	range.OffsetInDescriptorsFromTableStart =
	    D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	D3D12_ROOT_PARAMETER parameters[2]{};

	// ルートパラメーター0：VSのb0（変換行列）
	parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	parameters[0].Descriptor.ShaderRegister = 0;
	parameters[0].Descriptor.RegisterSpace = 0;
	parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

	// ルートパラメーター1：PSのt0（テクスチャ）
	parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	parameters[1].DescriptorTable.NumDescriptorRanges = 1;
	parameters[1].DescriptorTable.pDescriptorRanges = &range;
	parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	// PSのs0：画像の補間方法・範囲外の扱いを指定する
	D3D12_STATIC_SAMPLER_DESC sampler{};
	sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler.MaxAnisotropy = 1;
	sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
	sampler.MinLOD = 0.0f;
	sampler.MaxLOD = D3D12_FLOAT32_MAX;
	sampler.ShaderRegister = 0;
	sampler.RegisterSpace = 0;
	sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	D3D12_ROOT_SIGNATURE_DESC desc{};
	desc.NumParameters = 2;
	desc.pParameters = parameters;
	desc.NumStaticSamplers = 1;
	desc.pStaticSamplers = &sampler;
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

	// 保存されている深度より手前なら描画し、深度も更新する
	desc.DepthStencilState.DepthEnable = TRUE;
	desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
	desc.DepthStencilState.StencilEnable = FALSE;

	desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;

	const D3D12_INPUT_ELEMENT_DESC inputElements[] = {
	    {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
	     static_cast<UINT>(offsetof(Vertex, position)),
	     D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},

	    {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
	     static_cast<UINT>(offsetof(Vertex, color)),
	     D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},

	    {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0,
	     static_cast<UINT>(offsetof(Vertex, texcoord)),
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
	const DirectX::XMFLOAT4 white{1.0f, 1.0f, 1.0f, 1.0f};

	const Vertex vertices[] = {// 手前の面（Zが負）
	                           {{-0.5f, 0.5f, -0.5f}, white, {0.0f, 0.0f}},
	                           {{0.5f, 0.5f, -0.5f}, white, {1.0f, 0.0f}},
	                           {{0.5f, -0.5f, -0.5f}, white, {1.0f, 1.0f}},
	                           {{-0.5f, -0.5f, -0.5f}, white, {0.0f, 1.0f}},

	                           // 奥の面（Zが正）
	                           {{0.5f, 0.5f, 0.5f}, white, {0.0f, 0.0f}},
	                           {{-0.5f, 0.5f, 0.5f}, white, {1.0f, 0.0f}},
	                           {{-0.5f, -0.5f, 0.5f}, white, {1.0f, 1.0f}},
	                           {{0.5f, -0.5f, 0.5f}, white, {0.0f, 1.0f}},

	                           // 左の面
	                           {{-0.5f, 0.5f, 0.5f}, white, {0.0f, 0.0f}},
	                           {{-0.5f, 0.5f, -0.5f}, white, {1.0f, 0.0f}},
	                           {{-0.5f, -0.5f, -0.5f}, white, {1.0f, 1.0f}},
	                           {{-0.5f, -0.5f, 0.5f}, white, {0.0f, 1.0f}},

	                           // 右の面
	                           {{0.5f, 0.5f, -0.5f}, white, {0.0f, 0.0f}},
	                           {{0.5f, 0.5f, 0.5f}, white, {1.0f, 0.0f}},
	                           {{0.5f, -0.5f, 0.5f}, white, {1.0f, 1.0f}},
	                           {{0.5f, -0.5f, -0.5f}, white, {0.0f, 1.0f}},

	                           // 上の面
	                           {{-0.5f, 0.5f, 0.5f}, white, {0.0f, 0.0f}},
	                           {{0.5f, 0.5f, 0.5f}, white, {1.0f, 0.0f}},
	                           {{0.5f, 0.5f, -0.5f}, white, {1.0f, 1.0f}},
	                           {{-0.5f, 0.5f, -0.5f}, white, {0.0f, 1.0f}},

	                           // 下の面
	                           {{-0.5f, -0.5f, -0.5f}, white, {0.0f, 0.0f}},
	                           {{0.5f, -0.5f, -0.5f}, white, {1.0f, 0.0f}},
	                           {{0.5f, -0.5f, 0.5f}, white, {1.0f, 1.0f}},
	                           {{-0.5f, -0.5f, 0.5f}, white, {0.0f, 1.0f}}};

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

bool TriangleRenderer::CreateIndexBuffer(ID3D12Device *device) {
	const std::uint16_t indices[] = {// 手前
	                                 0, 1, 2, 0, 2, 3,

	                                 // 奥
	                                 4, 5, 6, 4, 6, 7,

	                                 // 左
	                                 8, 9, 10, 8, 10, 11,

	                                 // 右
	                                 12, 13, 14, 12, 14, 15,

	                                 // 上
	                                 16, 17, 18, 16, 18, 19,

	                                 // 下
	                                 20, 21, 22, 20, 22, 23};

	const HRESULT hr = DirectX::CreateUploadBuffer(
	    device, indices, std::size(indices), indexBuffer_.GetAddressOf());

	if (FAILED(hr)) {
		Logger::Error(std::format(
		    "インデックスバッファの作成に失敗しました。HRESULT=0x{:08X}",
		    static_cast<unsigned long>(hr)));
		return false;
	}

	indexBufferView_.BufferLocation = indexBuffer_->GetGPUVirtualAddress();

	indexBufferView_.SizeInBytes = static_cast<UINT>(sizeof(indices));

	// インデックス1個を16ビットの符号なし整数として読む
	indexBufferView_.Format = DXGI_FORMAT_R16_UINT;

	indexCount_ = static_cast<UINT>(std::size(indices));

	Logger::Info("インデックスバッファを作成しました。");
	return true;
}
