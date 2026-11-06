#include "graphics/ui/CanvasRenderer.h"
#include "graphics/material/Texture.h"
#include "graphics/render/D3D12Graphics.h"
#include "graphics/render/ShaderCompiler.h"
#include "platform/windows/PathUtils.h"
#include "platform/windows/TextRasterizer.h"
#include "scene/Scene.h"
#include "ui/CanvasLayout.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <exception>
#include <format>
#include <map>
#include <set>
#include <stdexcept>
#include <vector>
#include <wincodec.h>

using Microsoft::WRL::ComPtr;

namespace {
constexpr std::size_t kMaximumBytes = 128 * 1024 * 1024;
constexpr UINT kDescriptorCount =
    static_cast<UINT>(CanvasLayout::kMaximumElements * 2);

/// <summary>
/// 描画準備のHRESULTを例外へ変換する
/// </summary>
/// <param name="result">GPUまたはCOMの戻り値</param>
/// <param name="operation">失敗した処理の名前</param>
void Require(HRESULT result, const char *operation) {
	if (FAILED(result))
		throw std::runtime_error(
		    std::format("{} failed: 0x{:08X}", operation,
		                static_cast<unsigned long>(result)));
}
} // namespace

struct CanvasRenderer::Impl {
	explicit Impl(D3D12Graphics &value) : graphics(value) {
	}

	struct Command {
		std::shared_ptr<Texture> texture;
		UIRect rect;
		std::array<float, 4> color;
	};

	struct TextEntry {
		TextComponent settings;
		int width = 0, height = 0;
		float scale = 1;
		std::shared_ptr<Texture> texture;
	};

	struct ImageEntry {
		std::filesystem::file_time_type modified;
		std::uintmax_t size = 0;
		std::shared_ptr<Texture> texture;
	};

	D3D12Graphics &graphics;
	TextRasterizer rasterizer;
	ComPtr<IWICImagingFactory> imaging;
	ComPtr<ID3D12RootSignature> root;
	ComPtr<ID3D12PipelineState> pipeline;
	ComPtr<ID3D12DescriptorHeap> heap;
	std::shared_ptr<Texture> white;
	std::map<SceneObjectId, TextEntry> texts;
	std::map<std::filesystem::path, ImageEntry> images;
	std::vector<Command> commands;
	int width = 0, height = 0;
	bool prepared = false;
	Failure failure = Failure::FAIL;

	/// <summary>
	/// UIに固有のシェーダー・合成方法・画像ディスクリプターを初回だけ作る
	/// </summary>
	void Initialize() {
		if (pipeline)
			return;
		auto *device = graphics.GetDevice();
		if (!device)
			throw std::runtime_error("Canvas renderer has no GPU device.");
		ShaderCompiler compiler;
		if (!compiler.Initialize())
			throw std::runtime_error("Could not initialize UI shaders.");
		const auto shaderRoot =
		    PathUtils::ResolveFromExecutable(L"resources/engine/shaders/ui");
		const auto vs = compiler.Compile(
		    (shaderRoot / L"Canvas.VS.hlsl").wstring(), L"main", L"vs_6_0");
		const auto ps = compiler.Compile(
		    (shaderRoot / L"Canvas.PS.hlsl").wstring(), L"main", L"ps_6_0");
		if (!vs || !ps)
			throw std::runtime_error("Could not compile UI shaders.");
		D3D12_DESCRIPTOR_RANGE range{};
		range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		range.NumDescriptors = 1;
		D3D12_ROOT_PARAMETER parameters[2]{};
		parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
		parameters[0].Constants.Num32BitValues = 8;
		parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		parameters[1].ParameterType =
		    D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		parameters[1].DescriptorTable = {1, &range};
		parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		D3D12_STATIC_SAMPLER_DESC sampler{};
		sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		sampler.AddressU = sampler.AddressV = sampler.AddressW =
		    D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		sampler.MaxLOD = D3D12_FLOAT32_MAX;
		sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		D3D12_ROOT_SIGNATURE_DESC signature{};
		signature.NumParameters = 2;
		signature.pParameters = parameters;
		signature.NumStaticSamplers = 1;
		signature.pStaticSamplers = &sampler;
		ComPtr<ID3DBlob> serialized, errors;
		Require(D3D12SerializeRootSignature(
		            &signature, D3D_ROOT_SIGNATURE_VERSION_1,
		            serialized.GetAddressOf(), errors.GetAddressOf()),
		        "Serialize Canvas root signature");
		ComPtr<ID3D12RootSignature> nextRoot;
		Require(
		    device->CreateRootSignature(0, serialized->GetBufferPointer(),
		                                serialized->GetBufferSize(),
		                                IID_PPV_ARGS(nextRoot.GetAddressOf())),
		    "Create Canvas root signature");
		D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
		desc.pRootSignature = nextRoot.Get();
		desc.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
		desc.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
		desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
		desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		desc.RasterizerState.DepthClipEnable = TRUE;
		auto &blend = desc.BlendState.RenderTarget[0];
		blend.BlendEnable = TRUE;
		blend.SrcBlend = D3D12_BLEND_SRC_ALPHA;
		blend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
		blend.BlendOp = D3D12_BLEND_OP_ADD;
		blend.SrcBlendAlpha = D3D12_BLEND_ONE;
		blend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
		blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
		blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
		desc.DepthStencilState.DepthEnable = FALSE;
		desc.DepthStencilState.StencilEnable = FALSE;
		desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
		desc.SampleMask = UINT_MAX;
		desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		desc.NumRenderTargets = 1;
		desc.RTVFormats[0] = graphics.GetRenderTargetFormat();
		desc.SampleDesc.Count = 1;
		ComPtr<ID3D12PipelineState> nextPipeline;
		Require(device->CreateGraphicsPipelineState(
		            &desc, IID_PPV_ARGS(nextPipeline.GetAddressOf())),
		        "Create Canvas pipeline");
		D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
		heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		heapDesc.NumDescriptors = kDescriptorCount;
		heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		ComPtr<ID3D12DescriptorHeap> nextHeap;
		Require(device->CreateDescriptorHeap(
		            &heapDesc, IID_PPV_ARGS(nextHeap.GetAddressOf())),
		        "Create Canvas image heap");
		root = std::move(nextRoot);
		pipeline = std::move(nextPipeline);
		heap = std::move(nextHeap);
	}

	/// <summary>
	/// 復号する前に画像サイズを確認し、巨大なUI素材を拒否する
	/// </summary>
	/// <param name="path">画像の絶対参照</param>
	/// <returns>RGBAの必要バイト数。無効な画像は例外</returns>
	std::size_t ImageBytes(const std::filesystem::path &path) {
		if (!imaging)
			Require(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
			                         CLSCTX_INPROC_SERVER,
			                         IID_PPV_ARGS(imaging.GetAddressOf())),
			        "Create UI image factory");
		ComPtr<IWICBitmapDecoder> decoder;
		Require(imaging->CreateDecoderFromFilename(
		            path.c_str(), nullptr, GENERIC_READ,
		            WICDecodeMetadataCacheOnDemand, decoder.GetAddressOf()),
		        "Open UI image");
		ComPtr<IWICBitmapFrameDecode> frame;
		Require(decoder->GetFrame(0, frame.GetAddressOf()),
		        "Read UI image frame");
		UINT w = 0, h = 0;
		Require(frame->GetSize(&w, &h), "Read UI image size");
		if (!w || !h || w > 4096 || h > 4096)
			throw std::runtime_error(
			    "UI images support dimensions from 1 to 4096 pixels.");
		return static_cast<std::size_t>(w) * h * 4;
	}
};

CanvasRenderer::CanvasRenderer(D3D12Graphics &graphics, Failure failure)
    : impl_(std::make_unique<Impl>(graphics)) {
	impl_->failure = failure;
}

CanvasRenderer::~CanvasRenderer() = default;

bool CanvasRenderer::Update(const Scene &scene, int width, int height,
                            std::string &error,
                            std::span<const UIButtonVisual> visuals) {
	auto &p = *impl_;
	p.prepared = false;
	SceneObjectId current = 0;
	try {
		std::map<SceneObjectId, UIButtonVisual::State> states;
		if (visuals.size() > CanvasLayout::kMaximumElements)
			throw std::runtime_error("Too many Button visual states.");
		for (const auto &visual : visuals) {
			const auto *object = scene.FindObject(visual.object);
			if (!object || !object->button ||
			    visual.state < UIButtonVisual::State::NORMAL ||
			    visual.state > UIButtonVisual::State::DISABLED ||
			    !states.emplace(visual.object, visual.state).second)
				throw std::runtime_error("Invalid Button visual state.");
		}
		const auto color = [&](const SceneObject &object, SceneObjectId id,
		                       const std::array<float, 4> &base, bool text) {
			auto result = base;
			if (!object.button ||
			    (text && object.image && object.image->enabled &&
			     object.image->color[3] > 0))
				return result;
			const auto &button = *object.button;
			const auto found = states.find(id);
			const auto state = (!button.enabled || !button.interactable)
			                       ? UIButtonVisual::State::DISABLED
			                   : found == states.end()
			                       ? UIButtonVisual::State::NORMAL
			                       : found->second;
			const auto &tint =
			    state == UIButtonVisual::State::DISABLED  ? button.disabledColor
			    : state == UIButtonVisual::State::PRESSED ? button.pressedColor
			    : state == UIButtonVisual::State::HIGHLIGHTED
			        ? button.highlightedColor
			        : button.normalColor;
			for (std::size_t i = 0; i < 4; ++i)
				result[i] *= tint[i];
			return result;
		};
		std::vector<UIElement> elements;
		if (!CanvasLayout::Collect(scene, width, height, elements, error,
		                           nullptr, p.failure == Failure::SKIP_ELEMENT))
			return false;
		std::vector<Impl::Command> commands;
		std::map<SceneObjectId, Impl::TextEntry> texts;
		std::map<std::filesystem::path, Impl::ImageEntry> images;
		std::set<const Texture *> counted;
		std::size_t bytes = 0;
		const auto account = [&](const std::shared_ptr<Texture> &texture) {
			if (counted.insert(texture.get()).second) {
				const auto size =
				    static_cast<std::size_t>(texture->GetWidth()) *
				    texture->GetHeight() * 4;
				if (size > kMaximumBytes - bytes)
					throw std::runtime_error("UI textures exceed 128 MiB.");
				bytes += size;
			}
		};
		for (const auto &element : elements) {
			try {
				current = element.object;
				const auto &r = element.rect;
				if (r.x + r.width <= 0 || r.y + r.height <= 0 ||
				    r.x >= static_cast<float>(width) ||
				    r.y >= static_cast<float>(height))
					continue;
				const auto &object = *scene.FindObject(element.object);
				if (object.image && object.image->enabled &&
				    object.image->color[3] > 0) {
					const auto &image = *object.image;
					std::shared_ptr<Texture> texture;
					if (image.texture.empty()) {
						if (!p.white) {
							auto white = std::make_shared<Texture>();
							const std::array<std::uint8_t, 4> pixel{255, 255,
							                                        255, 255};
							if (!white->CreateFromRGBA(
							        p.graphics.GetDevice(),
							        p.graphics.GetCommandQueue(), 1, 1, pixel))
								throw std::runtime_error(
								    "Could not create solid UI image.");
							p.white = std::move(white);
						}
						texture = p.white;
					} else {
						const auto path =
						    PathUtils::ResolveFromExecutable(image.texture)
						        .lexically_normal();
						auto found = images.find(path);
						if (found == images.end()) {
							const auto modified =
							    std::filesystem::last_write_time(path);
							const auto size = std::filesystem::file_size(path);
							auto cached = p.images.find(path);
							Impl::ImageEntry entry;
							if (cached != p.images.end() &&
							    cached->second.modified == modified &&
							    cached->second.size == size)
								entry = cached->second;
							else {
								const auto needed = p.ImageBytes(path);
								if (needed > kMaximumBytes - bytes)
									throw std::runtime_error(
									    "UI images exceed 128 MiB.");
								entry.modified = modified;
								entry.size = size;
								entry.texture = std::make_shared<Texture>();
								if (!entry.texture->Load(
								        p.graphics.GetDevice(),
								        p.graphics.GetCommandQueue(), path))
									throw std::runtime_error(
									    "Could not load UI image.");
							}
							found =
							    images.emplace(path, std::move(entry)).first;
						}
						texture = found->second.texture;
					}
					account(texture);
					auto bounds = r;
					if (image.preserveAspect && !image.texture.empty()) {
						const float scale =
						    (std::min)(r.width / static_cast<float>(
						                             texture->GetWidth()),
						               r.height / static_cast<float>(
						                              texture->GetHeight()));
						bounds.width =
						    static_cast<float>(texture->GetWidth()) * scale;
						bounds.height =
						    static_cast<float>(texture->GetHeight()) * scale;
						bounds.x += (r.width - bounds.width) * .5f;
						bounds.y += (r.height - bounds.height) * .5f;
					}
					commands.push_back(
					    {std::move(texture), bounds,
					     color(object, current, image.color, false)});
				}
				if (object.text && object.text->enabled &&
				    !object.text->text.empty() && object.text->color[3] > 0) {
					if (r.width > 4096 || r.height > 4096)
						throw std::runtime_error(
						    "UI text rectangles support up to "
						    "4096 pixels per side.");
					const int w = (std::max)(1, static_cast<int>(
					                                std::ceil(r.width))),
					          h = (std::max)(1, static_cast<int>(
					                                std::ceil(r.height)));
					auto settings = *object.text;
					settings.color = {1, 1, 1, 1};
					Impl::TextEntry entry;
					const auto cached = p.texts.find(current);
					if (cached != p.texts.end() &&
					    cached->second.settings == settings &&
					    cached->second.width == w &&
					    cached->second.height == h &&
					    cached->second.scale == element.scale)
						entry = cached->second;
					else {
						if (static_cast<std::size_t>(w) * h * 4 >
						    kMaximumBytes - bytes)
							throw std::runtime_error(
							    "UI text exceeds 128 MiB.");
						std::vector<std::uint8_t> pixels;
						if (!p.rasterizer.Rasterize(
						        settings, w, h, element.scale, pixels, error))
							throw std::runtime_error(error);
						entry = {settings, w, h, element.scale,
						         std::make_shared<Texture>()};
						if (!entry.texture->CreateFromRGBA(
						        p.graphics.GetDevice(),
						        p.graphics.GetCommandQueue(),
						        static_cast<UINT>(w), static_cast<UINT>(h),
						        pixels))
							throw std::runtime_error(
							    "Could not upload UI text.");
					}
					account(entry.texture);
					commands.push_back(
					    {entry.texture, r,
					     color(object, current, object.text->color, true)});
					texts.emplace(current, std::move(entry));
				}
			} catch (const std::bad_alloc &) {
				throw;
			} catch (const std::exception &) {
				if (p.failure == Failure::FAIL)
					throw;
			}
		}
		if (!commands.empty()) {
			p.Initialize();
			auto *device = p.graphics.GetDevice();
			auto descriptor = p.heap->GetCPUDescriptorHandleForHeapStart();
			const UINT stride = device->GetDescriptorHandleIncrementSize(
			    D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			for (const auto &command : commands) {
				D3D12_SHADER_RESOURCE_VIEW_DESC view{};
				view.Format = command.texture->GetResource()->GetDesc().Format;
				view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
				view.Shader4ComponentMapping =
				    D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
				view.Texture2D.MipLevels =
				    command.texture->GetResource()->GetDesc().MipLevels;
				device->CreateShaderResourceView(command.texture->GetResource(),
				                                 &view, descriptor);
				descriptor.ptr += stride;
			}
		}
		p.commands.swap(commands);
		p.texts.swap(texts);
		p.images.swap(images);
		p.width = width;
		p.height = height;
		p.prepared = true;
		error.clear();
		return true;
	} catch (const std::exception &exception) {
		error =
		    std::format("Canvas object ID {}: {}", current, exception.what());
		return false;
	}
}

void CanvasRenderer::Draw() const {
	const auto &p = *impl_;
	if (!p.prepared || p.commands.empty())
		return;
	auto *list = p.graphics.GetCommandList();
	if (!list)
		return;
	ID3D12DescriptorHeap *heaps[]{p.heap.Get()};
	list->SetDescriptorHeaps(1, heaps);
	list->SetGraphicsRootSignature(p.root.Get());
	list->SetPipelineState(p.pipeline.Get());
	list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	auto descriptor = p.heap->GetGPUDescriptorHandleForHeapStart();
	const UINT stride =
	    p.graphics.GetDevice()->GetDescriptorHandleIncrementSize(
	        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	for (const auto &command : p.commands) {
		const auto &r = command.rect;
		const auto &color = command.color;
		const float constants[]{2 * r.x / static_cast<float>(p.width) - 1,
		                        1 - 2 * r.y / static_cast<float>(p.height),
		                        2 * r.width / static_cast<float>(p.width),
		                        -2 * r.height / static_cast<float>(p.height),
		                        color[0],
		                        color[1],
		                        color[2],
		                        color[3]};
		list->SetGraphicsRoot32BitConstants(0, 8, constants, 0);
		list->SetGraphicsRootDescriptorTable(1, descriptor);
		list->DrawInstanced(6, 1, 0, 0);
		descriptor.ptr += stride;
	}
}
