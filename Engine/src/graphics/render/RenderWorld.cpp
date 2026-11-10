#include "graphics/render/RenderWorld.h"
#include "core/Logger.h"
#include "graphics/lighting/DirectionalLight.h"
#include "graphics/lighting/FogSettings.h"
#include "graphics/lighting/PointLights.h"
#include "graphics/lighting/SpotLights.h"
#include "graphics/camera/Camera.h"
#include "graphics/material/Material.h"
#include "graphics/mesh/MeshInstance.h"
#include "graphics/model/Model.h"
#include "graphics/model/ModelInstance.h"
#include "graphics/render/D3D12Graphics.h"
#include "graphics/render/DebugLineBatch.h"
#include "graphics/render/DebugLineRenderer.h"
#include "graphics/render/MeshRenderer.h"
#include "graphics/render/ShadowMap.h"
#include "math/MathUtils.h"
#include "math/Ray.h"
#include "math/TransformUtils.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <exception>
#include <format>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

/// <summary>
/// ワールドを識別する番号を発行する。メインスレッドから呼び出す
/// </summary>
/// <returns>ほかのワールドで使用していない番号</returns>
std::uint64_t AllocateWorldId() {
	static std::uint64_t nextWorldId = 1;

	if (nextWorldId == (std::numeric_limits<std::uint64_t>::max)()) {
		throw std::overflow_error(
		    "RenderWorld: ワールド番号の上限に達しました。");
	}

	return nextWorldId++;
}

} // namespace

/// <summary>
/// 描画物体の格納先と、カメラ・ライトの実体を保持する
/// </summary>
struct RenderWorld::Impl {
	/// <summary>
	/// 共有モデルを使用する物体の情報
	/// </summary>
	struct Object {
		~Object() {
			// モデルへの共有参照を解放する前に終了する
			instance_.Finalize();
		}

		ModelHandle model_;
		Transform transform_{};
		std::optional<DirectX::XMFLOAT4X4> worldMatrix_;
		bool matrixHasSrt_ = true;
		bool castsShadow_ = true;
		ColorTint tint_{};
		ModelInstance instance_;
	};

	/// <summary>
	/// 物体の格納先と、その位置の世代番号
	/// </summary>
	struct Slot {
		std::unique_ptr<Object> object_;
		std::uint64_t generation_ = 1;
	};

	explicit Impl(D3D12Graphics &graphics)
	    : graphics_(graphics), worldId_(AllocateWorldId()) {
	}

	bool Contains(RenderObjectHandle handle) const {
		if (handle.worldId_ != worldId_ || handle.index_ >= slots_.size()) {
			return false;
		}

		const Slot &slot = slots_[handle.index_];

		return slot.object_ != nullptr &&
		       slot.generation_ == handle.generation_;
	}

	Object &GetObject(RenderObjectHandle handle) {
		if (!Contains(handle)) {
			throw std::out_of_range("RenderWorld: 物体のハンドルが無効です。");
		}

		return *slots_[handle.index_].object_;
	}

	const Object &GetObject(RenderObjectHandle handle) const {
		if (!Contains(handle)) {
			throw std::out_of_range("RenderWorld: 物体のハンドルが無効です。");
		}

		return *slots_[handle.index_].object_;
	}

	/// <summary>
	/// SRTまたは行列指定から、有効なワールド行列を取り出す
	/// </summary>
	/// <param name="object">確認する物体</param>
	/// <param name="matrix">成功時だけ置き換える行列</param>
	/// <returns>true:成功 false:変形が無効</returns>
	bool GetWorldMatrix(const Object &object,
	                    DirectX::XMFLOAT4X4 &matrix) const {
		DirectX::XMFLOAT4X4 candidate;

		if (object.worldMatrix_) {
			candidate = *object.worldMatrix_;
		} else {
			if (!TransformUtils::IsValid(object.transform_)) {
				return false;
			}

			DirectX::XMStoreFloat4x4(
			    &candidate, TransformUtils::MakeWorldMatrix(object.transform_));
		}

		DirectX::XMFLOAT4X4 inverse;

		if (!TransformUtils::TryInverseWorldMatrix(candidate, inverse)) {
			return false;
		}

		matrix = candidate;
		return true;
	}

	void ReleaseSlot(Slot &slot) {
		if (!slot.object_) {
			return;
		}

		slot.object_.reset();

		// 上限に達したスロットは、以降再利用しない
		if (slot.generation_ != (std::numeric_limits<std::uint64_t>::max)()) {
			++slot.generation_;
		}
	}

	// 参照先はRenderWorldより長く保持する
	D3D12Graphics &graphics_;

	const std::uint64_t worldId_;

	std::vector<Slot> slots_;

	std::vector<DebugLine> debugLines_;
	DebugLineBatch debugLineBatch_;
	bool debugLinesUpdated_ = true;

	Camera camera_;
	DirectionalLight light_;
	PointLights pointLights_;
	SpotLights spotLights_;
	std::vector<PointLightData> pointLightData_;
	std::vector<SpotLightData> spotLightData_;
	DirectionalLightsData lightData_{};
	FogSettings fog_;
	ShadowSettings shadowSettings_;
	ShadowMap shadows_;

	bool initialized_ = false;
	bool prepared_ = false;
};

bool RenderWorld::GetCameraMatrices(DirectX::XMFLOAT4X4 &view,
                                    DirectX::XMFLOAT4X4 &projection) const {
	if (!impl_->initialized_ || !impl_->prepared_) {
		return false;
	}

	view = impl_->camera_.GetViewMatrix();
	projection = impl_->camera_.GetProjectionMatrix();

	return true;
}

bool RenderWorld::GetObjectBounds(
    RenderObjectHandle handle,
    std::array<DirectX::XMFLOAT3, 8> &corners) const {
	if (!impl_->initialized_ || !impl_->Contains(handle)) {
		return false;
	}

	const Impl &impl = *impl_;
	const auto &object = impl.GetObject(handle);

	DirectX::XMFLOAT4X4 worldMatrix;

	if (!object.model_.model_ || !impl.GetWorldMatrix(object, worldMatrix)) {
		return false;
	}

	std::array<DirectX::XMFLOAT3, 8> transformed{};

	if (!object.instance_.GetBoundsCorners(transformed)) {
		return false;
	}

	using namespace DirectX;

	const XMMATRIX world = XMLoadFloat4x4(&worldMatrix);

	for (auto &corner : transformed) {
		XMStoreFloat3(&corner,
		              XMVector3TransformCoord(XMLoadFloat3(&corner), world));

		if (!MathUtils::IsFinite(corner)) {
			return false;
		}
	}

	corners = transformed;
	return true;
}

bool RenderWorld::AddDebugLine(const DebugLine &line) {
	if (!impl_->initialized_ || !impl_->prepared_ ||
	    !MathUtils::IsFinite(line.start) || !MathUtils::IsFinite(line.end) ||
	    !MathUtils::IsFinite(line.color)) {
		return false;
	}

	impl_->debugLines_.push_back(line);
	impl_->debugLinesUpdated_ = false;

	return true;
}

bool RenderWorld::UpdateDebugLines() {
	if (!impl_->initialized_ || !impl_->prepared_) {
		Logger::Error(
		    "RenderWorld::UpdateDebugLines: 描画データが更新されていません。");
		return false;
	}

	impl_->debugLinesUpdated_ = false;

	if (!impl_->debugLineBatch_.Update(impl_->graphics_.GetDevice(),
	                                   impl_->debugLines_)) {
		return false;
	}

	impl_->debugLinesUpdated_ = true;
	return true;
}

RenderWorld::RenderWorld(D3D12Graphics &graphics)
    : impl_(std::make_unique<Impl>(graphics)) {
}

RenderWorld::~RenderWorld() {
	// 呼び出し側でGPUの利用完了を保証してから破棄する
	Clear();
	impl_->light_.Finalize();
	impl_->pointLights_.Finalize();
	impl_->spotLights_.Finalize();
}

bool RenderWorld::Initialize() {
	if (impl_->initialized_) {
		Logger::Error("RenderWorld::Initialize: 初期化済みです。");
		return false;
	}

	auto *device = impl_->graphics_.GetDevice();

	if (!device) {
		Logger::Error("RenderWorld::Initialize: 描画基盤が未初期化です。");
		return false;
	}

	if (!impl_->light_.Initialize(device) ||
	    !impl_->pointLights_.Initialize(device) ||
	    !impl_->spotLights_.Initialize(device) ||
	    !impl_->shadows_.Initialize(device)) {
		return false;
	}

	impl_->initialized_ = true;
	return true;
}

RenderObjectHandle RenderWorld::CreateObject(const ModelHandle &model) {
	if (!impl_->initialized_) {
		Logger::Error("RenderWorld::CreateObject: 未初期化です。");
		return {};
	}

	if (!model.model_ || model.graphics_ != &impl_->graphics_) {
		Logger::Error("RenderWorld::CreateObject: "
		              "モデルが無効、または描画基盤が異なります。");
		return {};
	}

	try {
		auto object = std::make_unique<Impl::Object>();

		// ModelInstanceが参照するモデルの寿命を保証する
		object->model_ = model;

		if (!object->instance_.Initialize(impl_->graphics_.GetDevice(),
		                                  *model.model_)) {
			return {};
		}

		// 削除済みで、世代番号が上限に達していない位置を探す
		std::size_t index = 0;

		for (; index < impl_->slots_.size(); ++index) {
			const auto &slot = impl_->slots_[index];

			if (!slot.object_ &&
			    slot.generation_ !=
			        (std::numeric_limits<std::uint64_t>::max)()) {
				break;
			}
		}

		if (index == impl_->slots_.size()) {
			impl_->slots_.emplace_back();
		}

		auto &slot = impl_->slots_[index];
		slot.object_ = std::move(object);

		RenderObjectHandle handle;
		handle.worldId_ = impl_->worldId_;
		handle.index_ = index;
		handle.generation_ = slot.generation_;

		impl_->prepared_ = false;
		return handle;
	} catch (const std::exception &error) {
		Logger::Error(std::format(
		    "RenderWorldの物体作成中に例外が発生しました: {}", error.what()));

		return {};
	}
}

bool RenderWorld::Contains(RenderObjectHandle handle) const {
	return impl_->Contains(handle);
}

bool RenderWorld::DestroyObject(RenderObjectHandle handle) {
	if (!Contains(handle)) {
		return false;
	}

	impl_->ReleaseSlot(impl_->slots_[handle.index_]);

	impl_->prepared_ = false;
	return true;
}

Transform &RenderWorld::GetTransform(RenderObjectHandle handle) {
	auto &object = impl_->GetObject(handle);

	if (object.worldMatrix_ && !object.matrixHasSrt_) {
		throw std::logic_error(
		    "RenderWorld: "
		    "せん断を含む変形はGetWorldMatrixで取得してください。");
	}

	object.worldMatrix_.reset();
	impl_->prepared_ = false;
	return object.transform_;
}

const Transform &RenderWorld::GetTransform(RenderObjectHandle handle) const {
	const auto &object = impl_->GetObject(handle);

	if (object.worldMatrix_ && !object.matrixHasSrt_) {
		throw std::logic_error(
		    "RenderWorld: "
		    "せん断を含む変形はGetWorldMatrixで取得してください。");
	}

	return object.transform_;
}

bool RenderWorld::SetTransform(RenderObjectHandle handle,
                               const Transform &transform) {
	if (!impl_->Contains(handle) || !TransformUtils::IsValid(transform)) {
		return false;
	}

	auto &object = impl_->GetObject(handle);
	object.transform_ = transform;
	object.worldMatrix_.reset();
	object.matrixHasSrt_ = true;
	impl_->prepared_ = false;
	return true;
}

bool RenderWorld::SetWorldMatrix(RenderObjectHandle handle,
                                 const DirectX::XMFLOAT4X4 &matrix) {
	DirectX::XMFLOAT4X4 inverse;

	if (!impl_->Contains(handle) ||
	    !TransformUtils::TryInverseWorldMatrix(matrix, inverse)) {
		return false;
	}

	auto &object = impl_->GetObject(handle);
	Transform transform;
	const bool hasSrt = TransformUtils::TryFromWorldMatrix(matrix, transform);

	if (hasSrt) {
		object.transform_ = transform;
	}

	object.worldMatrix_ = matrix;
	object.matrixHasSrt_ = hasSrt;
	impl_->prepared_ = false;
	return true;
}

bool RenderWorld::GetWorldMatrix(RenderObjectHandle handle,
                                 DirectX::XMFLOAT4X4 &matrix) const {
	return impl_->initialized_ && impl_->Contains(handle) &&
	       impl_->GetWorldMatrix(impl_->GetObject(handle), matrix);
}

std::size_t RenderWorld::GetMaterialCount(RenderObjectHandle handle) const {
	const Impl &impl = *impl_;
	return impl.GetObject(handle).instance_.GetMaterialCount();
}

bool RenderWorld::SetColorTint(RenderObjectHandle handle,
                               const ColorTint &tint) {
	if (!impl_->Contains(handle) || !tint.IsValid()) {
		return false;
	}

	impl_->GetObject(handle).tint_ = tint;
	impl_->prepared_ = false;
	return true;
}

ColorTint RenderWorld::GetColorTint(RenderObjectHandle handle) const {
	const Impl &impl = *impl_;
	return impl.GetObject(handle).tint_;
}

MaterialData &RenderWorld::GetMaterial(RenderObjectHandle handle,
                                       std::size_t materialIndex) {
	auto &object = impl_->GetObject(handle);
	auto &material = object.instance_.GetMaterialData(materialIndex);

	impl_->prepared_ = false;
	return material;
}

const MaterialData &RenderWorld::GetMaterial(RenderObjectHandle handle,
                                             std::size_t materialIndex) const {
	const Impl &impl = *impl_;

	return impl.GetObject(handle).instance_.GetMaterialData(materialIndex);
}

void RenderWorld::SetDirectionalLights(const DirectionalLightsData &lights) {
	impl_->lightData_ = lights;
	impl_->prepared_ = false;
}

void RenderWorld::SetFog(const FogSettings &fog) {
	if (fog.IsValid()) {
		impl_->fog_ = fog;
	}
}

void RenderWorld::MoveCamera(const DirectX::XMFLOAT3 &offset) {
	if (!MathUtils::IsFinite(offset)) {
		throw std::invalid_argument(
		    "RenderWorld::MoveCamera: 移動量が無効です。");
	}

	impl_->camera_.Translate(offset);
	impl_->prepared_ = false;
}

void RenderWorld::Clear() {
	impl_->debugLines_.clear();
	impl_->debugLineBatch_.Clear();
	impl_->debugLinesUpdated_ = true;

	for (auto &slot : impl_->slots_) {
		impl_->ReleaseSlot(slot);
	}

	// スロットと世代番号は残し、古いハンドルを再び有効にしない
	impl_->camera_ = Camera{};
	impl_->lightData_ = {};
	impl_->fog_ = {};
	impl_->pointLightData_.clear();
	impl_->spotLightData_.clear();
	impl_->prepared_ = false;
}

bool RenderWorld::SetCameraPose(const CameraPose &pose) {
	if (!impl_->camera_.SetPose(pose)) {
		return false;
	}

	impl_->prepared_ = false;
	return true;
}

bool RenderWorld::Update(int width, int height) {
	impl_->prepared_ = false;

	impl_->debugLines_.clear();
	impl_->debugLineBatch_.Clear();
	impl_->debugLinesUpdated_ = true;

	if (!impl_->initialized_) {
		Logger::Error("RenderWorld::Update: 未初期化です。");
		return false;
	}

	if (!impl_->camera_.Update(width, height)) {
		Logger::Error("RenderWorld::Update: 描画領域のサイズが無効です。");
		return false;
	}

	if (!impl_->light_.Update(impl_->lightData_) ||
	    !impl_->pointLights_.Update(impl_->pointLightData_) ||
	    !impl_->spotLights_.Update(impl_->spotLightData_)) {
		return false;
	}

	for (const auto &slot : impl_->slots_) {
		if (!slot.object_) {
			continue;
		}

		auto &object = *slot.object_;

		DirectX::XMFLOAT4X4 worldMatrix;

		if (!impl_->GetWorldMatrix(object, worldMatrix) ||
		    !object.instance_.Update(impl_->camera_, worldMatrix,
		                             object.tint_)) {
			return false;
		}
	}

	std::vector<const MaterialResource *> materials;
	for (const auto &slot : impl_->slots_)
		if (slot.object_)
			for (const auto &mesh : slot.object_->instance_.GetMeshes())
				materials.push_back(mesh.GetMaterial()->GetResource().get());
	const auto shadowIndex = impl_->lightData_.shadowIndex;
	const auto &shadowLight = impl_->lightData_.lights[shadowIndex < 0 ? 0 : shadowIndex];
	if (!impl_->shadows_.Update(impl_->camera_.GetViewMatrix(),
	                            shadowLight.direction,
	                            impl_->shadowSettings_, materials))
		return false;
	impl_->prepared_ = true;
	return true;
}

void RenderWorld::Draw(MeshRenderer &renderer,
                       DebugLineRenderer &debugRenderer) const {
	if (!impl_->prepared_ || !impl_->debugLinesUpdated_) {
		Logger::Error(
		    "RenderWorld::Draw: 物体または線の更新が完了していません。");
		return;
	}

	// 次の描画では、再びUpdateを必要とする
	impl_->prepared_ = false;

	auto *commandList = impl_->graphics_.GetCommandList();

	if (!commandList) {
		Logger::Error("RenderWorld::Draw: コマンドリストがありません。");
		return;
	}

	const auto &settings = impl_->fog_;
	const MeshFogData fog{impl_->camera_.GetViewOrigin(), settings.start,
	                      settings.color, settings.end,
	                      settings.enabled ? settings.strength : 0.0f};
	for (const auto &slot : impl_->slots_) {
		if (!slot.object_) {
			continue;
		}

		slot.object_->instance_.Draw(commandList, renderer, impl_->light_,
		                             impl_->pointLights_, impl_->spotLights_,
		                             impl_->shadows_, fog);
	}

	// モデルの深度を使い、登録された線をまとめて描画する
	debugRenderer.Draw(commandList, impl_->debugLineBatch_,
	                   impl_->camera_.GetViewMatrix(),
	                   impl_->camera_.GetProjectionMatrix());

	impl_->debugLines_.clear();
	impl_->debugLineBatch_.Clear();
	impl_->debugLinesUpdated_ = true;
}

bool RenderWorld::Uses(const D3D12Graphics &graphics) const {
	return &impl_->graphics_ == &graphics;
}

bool RenderWorld::CreatePickingRay(float imageX, float imageY, Ray &ray) const {
	return impl_->initialized_ && impl_->prepared_ &&
	       impl_->camera_.CreatePickingRay(imageX, imageY, ray);
}

RenderObjectHandle
RenderWorld::Pick(float imageX, float imageY, float *distance,
                  std::span<const RenderObjectHandle> excluded) const {
	if (!impl_->initialized_ || !impl_->prepared_) {
		return {};
	}

	Ray worldRay;

	if (!CreatePickingRay(imageX, imageY, worldRay)) {
		return {};
	}

	using namespace DirectX;

	const XMVECTOR worldOrigin = XMLoadFloat3(&worldRay.origin);
	const XMVECTOR worldDirection = XMLoadFloat3(&worldRay.direction);

	float nearestDistance = worldRay.maxDistance;
	RenderObjectHandle nearestHandle;

	for (std::size_t index = 0; index < impl_->slots_.size(); ++index) {
		const auto &slot = impl_->slots_[index];

		if (!slot.object_) {
			continue;
		}

		RenderObjectHandle candidate;
		candidate.worldId_ = impl_->worldId_;
		candidate.index_ = index;
		candidate.generation_ = slot.generation_;

		if (std::find(excluded.begin(), excluded.end(), candidate) !=
		    excluded.end()) {
			continue;
		}

		const auto &object = *slot.object_;

		XMFLOAT4X4 worldMatrix, inverse;

		if (!object.model_.model_ ||
		    !impl_->GetWorldMatrix(object, worldMatrix) ||
		    !TransformUtils::TryInverseWorldMatrix(worldMatrix, inverse)) {
			continue;
		}

		const XMMATRIX inverseWorld = XMLoadFloat4x4(&inverse);

		// レイをモデルのローカル座標へ変換する
		const XMVECTOR localOrigin =
		    XMVector3TransformCoord(worldOrigin, inverseWorld);

		const XMVECTOR localDirection =
		    XMVector3TransformNormal(worldDirection, inverseWorld);

		const float directionLength =
		    XMVectorGetX(XMVector3Length(localDirection));

		if (!std::isfinite(directionLength) || directionLength <= 0.0f) {
			continue;
		}

		Ray localRay;
		XMStoreFloat3(&localRay.origin, localOrigin);
		XMStoreFloat3(&localRay.direction,
		              XMVectorScale(localDirection, 1.0f / directionLength));

		float localDistance = 0.0f;

		if (!object.instance_.Raycast(localRay, localDistance)) {
			continue;
		}

		// ローカル座標の距離を、ワールド座標の距離へ戻す
		const float worldDistance = localDistance / directionLength;

		if (!std::isfinite(worldDistance) || worldDistance < 0.0f ||
		    worldDistance >= nearestDistance) {
			continue;
		}

		nearestDistance = worldDistance;

		nearestHandle = candidate;
	}

	if (distance && Contains(nearestHandle)) {
		*distance = nearestDistance;
	}

	return nearestHandle;
}

bool RenderWorld::SetCameraSettings(const CameraSettings &settings) {
	if (!impl_->camera_.SetSettings(settings)) {
		return false;
	}

	impl_->prepared_ = false;
	return true;
}

bool RenderWorld::SetMaterial(RenderObjectHandle handle, std::size_t slot,
                              const MaterialHandle &material) {
	if (!impl_->Contains(handle) ||
	    (material && material.device_ != impl_->graphics_.GetDevice())) {
		return false;
	}

	if (!impl_->GetObject(handle).instance_.SetMaterial(slot,
	                                                    material.resource_)) {
		return false;
	}

	impl_->prepared_ = false;
	return true;
}

bool RenderWorld::SetPointLights(std::span<const PointLightData> lights) {
	if (!impl_->initialized_ ||
	    lights.size() > PointLightData::kMaximumLights ||
	    !std::all_of(lights.begin(), lights.end(),
	                 [](const auto &light) { return light.IsValid(); })) {
		return false;
	}

	std::vector<PointLightData> next(lights.begin(), lights.end());
	impl_->pointLightData_.swap(next);
	impl_->prepared_ = false;
	return true;
}

bool RenderWorld::SetSpotLights(std::span<const SpotLightData> lights) {
	if (!impl_->initialized_ || lights.size() > SpotLightData::kMaximumLights ||
	    !std::all_of(lights.begin(), lights.end(),
	                 [](const auto &light) { return light.IsValid(); })) {
		return false;
	}

	std::vector<SpotLightData> next(lights.begin(), lights.end());
	impl_->spotLightData_.swap(next);
	impl_->prepared_ = false;
	return true;
}

bool RenderWorld::SetShadows(const ShadowSettings &settings) {
	if (!settings.IsValid())
		return false;
	impl_->shadowSettings_ = settings;
	return true;
}

bool RenderWorld::SetShadowCaster(RenderObjectHandle handle, bool enabled) {
	if (!impl_->initialized_ || !impl_->Contains(handle))
		return false;
	auto &object = impl_->GetObject(handle);
	if (object.castsShadow_ != enabled) {
		object.castsShadow_ = enabled;
		impl_->prepared_ = false;
	}
	return true;
}

void RenderWorld::DrawShadows(MeshRenderer &renderer) const {
	if (!impl_->prepared_)
		return;
	auto *commands = impl_->graphics_.GetCommandList();
	if (!impl_->shadows_.Begin(commands))
		return;
	for (const auto &slot : impl_->slots_) {
		if (slot.object_ && slot.object_->castsShadow_)
			for (const auto &mesh : slot.object_->instance_.GetMeshes())
				renderer.DrawShadow(commands, mesh, impl_->shadows_);
	}

	impl_->shadows_.End(commands);
}

bool RenderWorld::SetAnimationPose(RenderObjectHandle handle,
                                   const ModelAnimationPose *pose) {
	if (!impl_->initialized_ || !impl_->Contains(handle))
		return false;
	if (!impl_->GetObject(handle).instance_.SetAnimationPose(pose))
		return false;
	impl_->prepared_ = false;
	return true;
}
