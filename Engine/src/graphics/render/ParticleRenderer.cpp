#include "graphics/render/ParticleRenderer.h"
#include "graphics/buffer/ConstantBuffer.h"
#include "graphics/material/MaterialManager.h"
#include "graphics/material/MaterialResource.h"
#include "graphics/render/D3D12Graphics.h"
#include "graphics/render/ShaderCompiler.h"
#include "math/MathUtils.h"
#include "math/TransformUtils.h"
#include "particles/SceneParticles.h"
#include "platform/windows/PathUtils.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <exception>
#include <format>
#include <map>
#include <stdexcept>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace {
/// <summary>
/// GPU作成の失敗を、呼び出し側で破棄する例外へ変換する
/// </summary>
/// <param name="result">GPU処理の結果</param>
void Require(HRESULT result) {
	if (FAILED(result))
		throw std::runtime_error(
		    std::format("Particle GPU operation failed: 0x{:08X}",
		                static_cast<unsigned long>(result)));
}
} // namespace

struct ParticleRenderer::Impl {
	struct Vertex {
		DirectX::XMFLOAT3 position;
		float size;
		DirectX::XMFLOAT4 color;
		float rotation;
		std::uint32_t circle;
		float padding[2]{};
	};

	struct CameraData {
		DirectX::XMFLOAT4X4 viewProjection;
		DirectX::XMFLOAT3 right;
		float padding0 = 0;
		DirectX::XMFLOAT3 up;
		float padding1 = 0;
	};

	struct Item {
		Vertex vertex;
		std::shared_ptr<const MaterialResource> image;
		bool additive = false;
		float depth = 0;
	};

	struct Run {
		std::shared_ptr<const MaterialResource> image;
		bool additive;
		UINT first, count;
	};

	Impl(D3D12Graphics &g, MaterialManager &m) : graphics(g), materials(m) {
	}

	D3D12Graphics &graphics;
	MaterialManager &materials;
	ComPtr<ID3D12RootSignature> root;
	ComPtr<ID3D12PipelineState> alpha, additive;
	ConstantBuffer<CameraData> camera;
	ComPtr<ID3D12Resource> buffer;
	Vertex *mapped = nullptr;
	std::size_t capacity = 0;
	D3D12_VERTEX_BUFFER_VIEW view{};
	std::map<std::filesystem::path, std::shared_ptr<const MaterialResource>>
	    prepared;
	std::size_t imageBytes = 0;
	std::vector<Run> runs;

	/// <summary>
	/// 初めて粒子があるフレームに、共通の頂点形式と透明・加算パイプラインを準備する
	/// </summary>
	void Initialize() {
		if (root)
			return;
		auto *device = graphics.GetDevice();
		if (!device)
			throw std::runtime_error("Particle renderer has no device.");
		ShaderCompiler compiler;
		if (!compiler.Initialize())
			throw std::runtime_error("Could not initialize particle shaders.");
		const auto folder = PathUtils::ResolveFromExecutable(
		    L"resources/engine/shaders/particles");
		const auto vs = compiler.Compile(
		               (folder / L"Particle.VS.hlsl").wstring(), L"main",
		               L"vs_6_0"),
		           ps = compiler.Compile(
		               (folder / L"Particle.PS.hlsl").wstring(), L"main",
		               L"ps_6_0");
		if (!vs || !ps)
			throw std::runtime_error("Could not compile particle shaders.");
		D3D12_DESCRIPTOR_RANGE range{};
		range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		range.NumDescriptors = 1;
		D3D12_ROOT_PARAMETER parameters[2]{};
		parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
		parameters[1].ParameterType =
		    D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		parameters[1].DescriptorTable = {1, &range};
		parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		D3D12_STATIC_SAMPLER_DESC sampler{};
		sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		sampler.AddressU = sampler.AddressV = sampler.AddressW =
		    D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		sampler.MaxLOD = D3D12_FLOAT32_MAX;
		sampler.MaxAnisotropy = 1;
		sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		D3D12_ROOT_SIGNATURE_DESC signature{};
		signature.NumParameters = 2;
		signature.pParameters = parameters;
		signature.NumStaticSamplers = 1;
		signature.pStaticSamplers = &sampler;
		signature.Flags =
		    D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
		ComPtr<ID3DBlob> bytes, errors;
		ComPtr<ID3D12RootSignature> candidateRoot;
		Require(D3D12SerializeRootSignature(
		    &signature, D3D_ROOT_SIGNATURE_VERSION_1, bytes.GetAddressOf(),
		    errors.GetAddressOf()));
		Require(device->CreateRootSignature(
		    0, bytes->GetBufferPointer(), bytes->GetBufferSize(),
		    IID_PPV_ARGS(candidateRoot.GetAddressOf())));
		const D3D12_INPUT_ELEMENT_DESC layout[]{
		    {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
		     static_cast<UINT>(offsetof(Vertex, position)),
		     D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
		    {"SIZE", 0, DXGI_FORMAT_R32_FLOAT, 0,
		     static_cast<UINT>(offsetof(Vertex, size)),
		     D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
		    {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
		     static_cast<UINT>(offsetof(Vertex, color)),
		     D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
		    {"ROTATION", 0, DXGI_FORMAT_R32_FLOAT, 0,
		     static_cast<UINT>(offsetof(Vertex, rotation)),
		     D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
		    {"CIRCLE", 0, DXGI_FORMAT_R32_UINT, 0,
		     static_cast<UINT>(offsetof(Vertex, circle)),
		     D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1}};
		D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
		desc.pRootSignature = candidateRoot.Get();
		desc.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
		desc.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
		desc.InputLayout = {layout, static_cast<UINT>(std::size(layout))};
		desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
		desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		desc.RasterizerState.DepthClipEnable = TRUE;
		desc.DepthStencilState.DepthEnable = TRUE;
		desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
		desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
		desc.DSVFormat = graphics.GetDepthStencilFormat();
		desc.NumRenderTargets = 1;
		desc.RTVFormats[0] = graphics.GetRenderTargetFormat();
		desc.SampleDesc.Count = 1;
		desc.SampleMask = UINT_MAX;
		desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

		for (auto &target : desc.BlendState.RenderTarget) {
			target.BlendEnable = TRUE;
			target.SrcBlend = D3D12_BLEND_SRC_ALPHA;
			target.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
			target.BlendOp = D3D12_BLEND_OP_ADD;
			target.SrcBlendAlpha = D3D12_BLEND_ONE;
			target.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
			target.BlendOpAlpha = D3D12_BLEND_OP_ADD;
			target.LogicOp = D3D12_LOGIC_OP_NOOP;
			target.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
		}

		ComPtr<ID3D12PipelineState> normal, plus;
		Require(device->CreateGraphicsPipelineState(
		    &desc, IID_PPV_ARGS(normal.GetAddressOf())));
		desc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
		Require(device->CreateGraphicsPipelineState(
		    &desc, IID_PPV_ARGS(plus.GetAddressOf())));
		if (!camera.Initialize(device))
			throw std::runtime_error(
			    "Could not initialize particle camera buffer.");
		alpha = std::move(normal);
		additive = std::move(plus);
		root = std::move(candidateRoot);
	}
};

ParticleRenderer::ParticleRenderer(D3D12Graphics &graphics,
                                   MaterialManager &materials)
    : impl_(std::make_unique<Impl>(graphics, materials)) {
}

ParticleRenderer::~ParticleRenderer() {
	Clear();
}

bool ParticleRenderer::PrepareImages(
    std::span<const std::filesystem::path> paths, std::string &error) {
	try {
		for (const auto &path : paths) {
			const auto key = path.empty()
			                     ? std::filesystem::path{}
			                     : PathUtils::ResolveFromExecutable(path);
			if (impl_->prepared.contains(key))
				continue;
			const auto handle = impl_->materials.LoadTexture(path);
			if (!handle)
				throw std::runtime_error("Could not prepare particle image.");
			const auto size = handle.resource_->GetTextureSize();
			const std::size_t bytes = std::size_t(size[0]) * size[1] * 4;
			constexpr std::size_t limit = 128 * 1024 * 1024;
			if (size[0] > 4096 || size[1] > 4096 ||
			    bytes > limit - impl_->imageBytes)
				throw std::runtime_error("Particle images exceed 4096 pixels "
				                         "or the 128 MiB budget.");
			impl_->prepared.emplace(key, handle.resource_);
			impl_->imageBytes += bytes;
		}

		error.clear();
		return true;
	} catch (const std::exception &e) {
		error = e.what();
		return false;
	}
}

bool ParticleRenderer::Update(std::span<const ParticleDrawBatch> batches,
                              const DirectX::XMFLOAT4X4 &view,
                              const DirectX::XMFLOAT4X4 &projection,
                              std::string &error) {
	impl_->runs.clear();

	try {
		std::vector<Impl::Item> items;
		std::vector<std::filesystem::path> paths;
		std::size_t count = 0;

		for (const auto &batch : batches) {
			if (batch.sprites.size() >
			    SceneParticles::kMaximumTotalParticles - count)
				throw std::runtime_error(
				    "Particle draw count exceeds the limit.");
			count += batch.sprites.size();
			if (!batch.sprites.empty())
				paths.push_back(batch.texture);
		}

		if (count == 0) {
			error.clear();
			return true;
		}

		if (!PrepareImages(paths, error))
			return false;
		impl_->Initialize();
		items.reserve(count);
		using namespace DirectX;
		XMFLOAT4X4 inverse;
		if (!TransformUtils::TryInverseWorldMatrix(view, inverse))
			throw std::runtime_error("Particle camera view is invalid.");
		for (const auto &batch : batches) {
			if (batch.sprites.empty())
				continue;
			const auto key =
			    batch.texture.empty()
			        ? std::filesystem::path{}
			        : PathUtils::ResolveFromExecutable(batch.texture);
			const auto image = impl_->prepared.at(key);

			for (const auto &sprite : batch.sprites) {
				const auto &c = sprite.color;
				if (!MathUtils::IsFinite(sprite.position) ||
				    !std::isfinite(sprite.size) || sprite.size < 0 ||
				    !std::isfinite(sprite.rotation) || !std::isfinite(c.x) ||
				    !std::isfinite(c.y) || !std::isfinite(c.z) ||
				    !std::isfinite(c.w) || c.x < 0 || c.x > 1 || c.y < 0 ||
				    c.y > 1 || c.z < 0 || c.z > 1 || c.w < 0 || c.w > 1)
					throw std::runtime_error("Particle sprite is invalid.");
				const float depth = XMVectorGetZ(XMVector3TransformCoord(
				    XMLoadFloat3(&sprite.position), XMLoadFloat4x4(&view)));
				if (!std::isfinite(depth))
					throw std::runtime_error("Particle depth is invalid.");
				items.push_back({{sprite.position,
				                  sprite.size,
				                  sprite.color,
				                  sprite.rotation,
				                  batch.texture.empty() ? 1u : 0u,
				                  {}},
				                 image,
				                 batch.additive,
				                 depth});
			}
		}

		std::stable_sort(
		    items.begin(), items.end(),
		    [](const auto &a, const auto &b) { return a.depth > b.depth; });
		if (items.size() > impl_->capacity) {
			const auto capacity =
			    (std::min)(SceneParticles::kMaximumTotalParticles,
			               (std::max)(items.size(), impl_->capacity * 2));
			D3D12_HEAP_PROPERTIES heap{};
			heap.Type = D3D12_HEAP_TYPE_UPLOAD;
			D3D12_RESOURCE_DESC desc{};
			desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
			desc.Width = capacity * sizeof(Impl::Vertex);
			desc.Height = 1;
			desc.DepthOrArraySize = 1;
			desc.MipLevels = 1;
			desc.SampleDesc.Count = 1;
			desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
			ComPtr<ID3D12Resource> buffer;
			Require(impl_->graphics.GetDevice()->CreateCommittedResource(
			    &heap, D3D12_HEAP_FLAG_NONE, &desc,
			    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
			    IID_PPV_ARGS(buffer.GetAddressOf())));
			void *data = nullptr;
			const D3D12_RANGE read{};
			Require(buffer->Map(0, &read, &data));
			impl_->buffer = std::move(buffer);
			impl_->mapped = static_cast<Impl::Vertex *>(data);
			impl_->capacity = capacity;
		}

		for (std::size_t i = 0; i < items.size(); ++i) {
			impl_->mapped[i] = items[i].vertex;
			if (impl_->runs.empty() ||
			    impl_->runs.back().image != items[i].image ||
			    impl_->runs.back().additive != items[i].additive)
				impl_->runs.push_back({items[i].image, items[i].additive,
				                       static_cast<UINT>(i), 1});
			else
				++impl_->runs.back().count;
		}

		impl_->view = {impl_->buffer->GetGPUVirtualAddress(),
		               static_cast<UINT>(items.size() * sizeof(Impl::Vertex)),
		               sizeof(Impl::Vertex)};
		Impl::CameraData camera;
		XMStoreFloat4x4(&camera.viewProjection,
		                XMLoadFloat4x4(&view) * XMLoadFloat4x4(&projection));
		camera.right = {inverse._11, inverse._12, inverse._13};
		camera.up = {inverse._21, inverse._22, inverse._23};
		if (!impl_->camera.Update(camera))
			throw std::runtime_error("Could not update particle camera.");
		error.clear();
		return true;
	} catch (const std::exception &e) {
		impl_->runs.clear();
		error = e.what();
		return false;
	}
}

void ParticleRenderer::Draw() const {
	if (impl_->runs.empty())
		return;
	auto *commands = impl_->graphics.GetCommandList();
	if (!commands)
		return;
	commands->SetGraphicsRootSignature(impl_->root.Get());
	commands->SetGraphicsRootConstantBufferView(
	    0, impl_->camera.GetGPUVirtualAddress());
	commands->IASetVertexBuffers(0, 1, &impl_->view);
	commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	for (const auto &run : impl_->runs) {
		auto *heap = run.image->GetDescriptorHeap();
		commands->SetDescriptorHeaps(1, &heap);
		commands->SetGraphicsRootDescriptorTable(1, run.image->GetTextureSrv());
		commands->SetPipelineState(run.additive ? impl_->additive.Get()
		                                        : impl_->alpha.Get());
		commands->DrawInstanced(6, run.count, 0, run.first);
	}
}

void ParticleRenderer::Clear() {
	impl_->runs.clear();
	impl_->prepared.clear();
	impl_->imageBytes = 0;
	impl_->buffer.Reset();
	impl_->mapped = nullptr;
	impl_->capacity = 0;
}
