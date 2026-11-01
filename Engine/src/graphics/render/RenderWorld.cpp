#include "graphics/render/RenderWorld.h"
#include "core/Logger.h"
#include "graphics/DirectionalLight.h"
#include "graphics/camera/Camera.h"
#include "graphics/model/Model.h"
#include "graphics/model/ModelInstance.h"
#include "graphics/render/D3D12Graphics.h"
#include "graphics/render/DebugLineBatch.h"
#include "graphics/render/DebugLineRenderer.h"
#include "graphics/render/MeshRenderer.h"
#include "math/MathUtils.h"
#include "math/Ray.h"
#include "math/TransformUtils.h"

#include <cmath>
#include <cstdint>
#include <exception>
#include <format>
#include <limits>
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
	    : graphics_(graphics), worldId_(AllocateWorldId()) {}

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
	DirectionalLightData lightData_{};

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

	if (!object.model_.model_ || !TransformUtils::IsValid(object.transform_)) {
		return false;
	}

	std::array<DirectX::XMFLOAT3, 8> transformed{};

	if (!object.model_.model_->GetBoundsCorners(transformed)) {
		return false;
	}

	using namespace DirectX;

	const XMMATRIX world = TransformUtils::MakeWorldMatrix(object.transform_);

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
    : impl_(std::make_unique<Impl>(graphics)) {}

RenderWorld::~RenderWorld() {
	// 呼び出し側でGPUの利用完了を保証してから破棄する
	Clear();
	impl_->light_.Finalize();
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

	if (!impl_->light_.Initialize(device)) {
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

	impl_->prepared_ = false;
	return object.transform_;
}

const Transform &RenderWorld::GetTransform(RenderObjectHandle handle) const {
	const Impl &impl = *impl_;
	return impl.GetObject(handle).transform_;
}

std::size_t RenderWorld::GetMaterialCount(RenderObjectHandle handle) const {
	const Impl &impl = *impl_;
	return impl.GetObject(handle).instance_.GetMaterialCount();
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

DirectionalLightData &RenderWorld::GetLight() {
	impl_->prepared_ = false;
	return impl_->lightData_;
}

const DirectionalLightData &RenderWorld::GetLight() const {
	return impl_->lightData_;
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
	impl_->lightData_ = DirectionalLightData{};
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

	if (!impl_->light_.Update(impl_->lightData_)) {
		return false;
	}

	for (const auto &slot : impl_->slots_) {
		if (!slot.object_) {
			continue;
		}

		auto &object = *slot.object_;

		object.instance_.SetTransform(object.transform_);

		if (!object.instance_.Update(impl_->camera_)) {
			return false;
		}
	}

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

	for (const auto &slot : impl_->slots_) {
		if (!slot.object_) {
			continue;
		}

		slot.object_->instance_.Draw(commandList, renderer, impl_->light_);
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

RenderObjectHandle RenderWorld::Pick(float imageX, float imageY) const {
	if (!impl_->initialized_ || !impl_->prepared_) {
		return {};
	}

	Ray worldRay;

	if (!impl_->camera_.CreatePickingRay(imageX, imageY, worldRay)) {
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

		const auto &object = *slot.object_;

		if (!object.model_.model_ ||
		    !TransformUtils::IsValid(object.transform_)) {
			continue;
		}

		const XMMATRIX world =
		    TransformUtils::MakeWorldMatrix(object.transform_);

		XMVECTOR determinant;
		const XMMATRIX inverseWorld = XMMatrixInverse(&determinant, world);

		const float determinantValue = XMVectorGetX(determinant);

		if (!std::isfinite(determinantValue) || determinantValue == 0.0f) {
			continue;
		}

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

		if (!object.model_.model_->Raycast(localRay, localDistance)) {
			continue;
		}

		// ローカル座標の距離を、ワールド座標の距離へ戻す
		const float worldDistance = localDistance / directionLength;

		if (!std::isfinite(worldDistance) || worldDistance < 0.0f ||
		    worldDistance >= nearestDistance) {
			continue;
		}

		nearestDistance = worldDistance;

		nearestHandle.worldId_ = impl_->worldId_;
		nearestHandle.index_ = index;
		nearestHandle.generation_ = slot.generation_;
	}

	return nearestHandle;
}
