#include "history/EditorHistory.h"

#include "core/Logger.h"
#include "math/TransformUtils.h"
#include "scene/Scene.h"

#include <cassert>
#include <cmath>
#include <exception>
#include <format>
#include <limits>
#include <set>
#include <stdexcept>
#include <type_traits>
#include <utility>

EditorHistory::EditorHistory(Scene &scene) noexcept : scene_(scene) {}

SceneObjectId EditorHistory::CreateObject(std::string name,
                                          std::filesystem::path modelPath) {
	CommitEdit();

	SceneObject object;
	object.name = std::move(name);
	object.modelPath = std::move(modelPath);

	return CreateObjectWithHistory(std::move(object));
}

bool EditorHistory::DeleteObject(SceneObjectId objectId) {
	return DeleteObjects(std::span<const SceneObjectId>{&objectId, 1});
}

SceneObjectId EditorHistory::DuplicateObject(SceneObjectId objectId) {
	auto createdIds =
	    DuplicateObjects(std::span<const SceneObjectId>{&objectId, 1});

	return createdIds.empty() ? 0 : createdIds.front();
}

bool EditorHistory::BeginRename(SceneObjectId objectId) {
	const SceneObject *object = scene_.FindObject(objectId);

	if (!object) {
		return false;
	}

	CommitEdit();

	PendingEdit edit{
	    objectId,
	    object->name,
	};

	pending_ = std::move(edit);
	return true;
}

bool EditorHistory::DeleteObjects(std::span<const SceneObjectId> objectIds) {
	CommitEdit();

	if (objectIds.empty()) {
		return false;
	}

	std::set<SceneObjectId> seen;

	std::vector<CommandGroup::Command> commands;
	commands.reserve(objectIds.size());

	for (const SceneObjectId id : objectIds) {
		if (id == 0 || !seen.insert(id).second) {
			return false;
		}

		const SceneObject *object = scene_.FindObject(id);

		if (!object) {
			return false;
		}

		// 削除前の全データを保持する
		commands.emplace_back(ObjectCommand{
		    id,
		    *object,
		    ObjectCommand::Kind::DELETE,
		});
	}

	CommandGroup group{std::move(commands)};

	// シーンを変更する前に、履歴を記録する領域を確保する
	const std::uint64_t next = PrepareAppend();

	if (!group.Redo(scene_)) {
		return false;
	}

	AppendPrepared(Command{std::move(group)}, next,
	               objectIds.size() == 1 ? "Delete Object" : "Delete Objects");
	return true;
}

std::vector<SceneObjectId>
EditorHistory::DuplicateObjects(std::span<const SceneObjectId> objectIds) {
	CommitEdit();

	if (objectIds.empty()) {
		return {};
	}

	std::set<SceneObjectId> seen;

	std::vector<SceneObject> copies;
	copies.reserve(objectIds.size());

	for (const SceneObjectId id : objectIds) {
		if (id == 0 || !seen.insert(id).second) {
			return {};
		}

		const SceneObject *source = scene_.FindObject(id);

		if (!source) {
			return {};
		}

		SceneObject object = *source;

		if (object.name.empty()) {
			object.name = "Object";
		}

		object.name += " Copy";

		copies.push_back(std::move(object));
	}

	return CreateObjects(std::move(copies));
}

std::vector<SceneObjectId>
EditorHistory::CreateObjects(std::vector<SceneObject> objects) {
	CommitEdit();
	if (objects.empty()) {
		return {};
	}
	for (auto &object : objects) {
		if (!TransformUtils::IsValid(object.transform)) {
			return {};
		}
		if (object.name.empty()) {
			object.name = "Object";
		}
	}
	std::vector<SceneObjectId> createdIds;
	createdIds.reserve(objects.size());

	std::vector<CommandGroup::Command> commands;
	commands.reserve(objects.size());

	const std::uint64_t next = PrepareAppend();

	const bool created = scene_.ApplyTransaction(
	    [&objects, &createdIds, &commands](Scene &working) {
		    for (auto &object : objects) {
			    const SceneObjectId id = working.CreateObject(object);

			    commands.emplace_back(ObjectCommand{
			        id,
			        std::move(object),
			        ObjectCommand::Kind::CREATE,
			    });

			    createdIds.push_back(id);
		    }

		    return true;
	    });

	if (!created) {
		return {};
	}

	// 操作後は、確保済みのデータを移動して履歴へ記録する
	AppendPrepared(Command{CommandGroup{std::move(commands)}}, next,
	               createdIds.size() == 1 ? "Create Object" : "Create Objects");

	return createdIds;
}

bool EditorHistory::BeginTransforms(std::span<const SceneObjectId> objectIds) {
	CommitEdit();

	if (objectIds.empty()) {
		return false;
	}

	std::set<SceneObjectId> seen;
	std::vector<TransformSnapshot> snapshots;
	snapshots.reserve(objectIds.size());

	for (const SceneObjectId id : objectIds) {
		if (id == 0 || !seen.insert(id).second) {
			return false;
		}

		const SceneObject *object = scene_.FindObject(id);
		if (!object || !TransformUtils::IsValid(object->transform)) {
			return false;
		}

		snapshots.push_back(TransformSnapshot{id, object->transform});
	}

	pendingTransforms_ = std::move(snapshots);
	return true;
}

bool EditorHistory::UpdateTransforms(
    const Transform &beforePivot, const Transform &afterPivot) {
	if (!pendingTransforms_ || !TransformUtils::IsValid(beforePivot) ||
	    !TransformUtils::IsValid(afterPivot)) {
		return false;
	}

	// ピボットの各軸の倍率を確認し、軸別の拡縮や符号反転を受け入れない
	if (beforePivot.scale.x <= 0.0f || beforePivot.scale.y <= 0.0f ||
	    beforePivot.scale.z <= 0.0f || afterPivot.scale.x <= 0.0f ||
	    afterPivot.scale.y <= 0.0f || afterPivot.scale.z <= 0.0f) {
		return false;
	}
	const double ratioX = static_cast<double>(afterPivot.scale.x) /
	                      beforePivot.scale.x;
	const double ratioY = static_cast<double>(afterPivot.scale.y) /
	                      beforePivot.scale.y;
	const double ratioZ = static_cast<double>(afterPivot.scale.z) /
	                      beforePivot.scale.z;
	const float scaleRatio = static_cast<float>(ratioX);
	const double tolerance = 0.00001 * ratioX;
	if (!std::isfinite(scaleRatio) || scaleRatio <= 0.0f ||
	    std::abs(ratioY - ratioX) > tolerance ||
	    std::abs(ratioZ - ratioX) > tolerance) {
		return false;
	}
	const bool scaled = scaleRatio != 1.0f;

	using namespace DirectX;
	const XMVECTOR beforeRotation =
	    XMQuaternionNormalize(XMLoadFloat4(&beforePivot.rotation));
	const XMVECTOR afterRotation =
	    XMQuaternionNormalize(XMLoadFloat4(&afterPivot.rotation));
	const XMVECTOR deltaRotation = XMQuaternionNormalize(
	    XMQuaternionMultiply(XMQuaternionConjugate(beforeRotation),
	                         afterRotation));
	const XMVECTOR beforeCenter = XMLoadFloat3(&beforePivot.translation);
	const XMVECTOR afterCenter = XMLoadFloat3(&afterPivot.translation);

	Transform beforeOrientation;
	beforeOrientation.rotation = beforePivot.rotation;
	Transform afterOrientation;
	afterOrientation.rotation = afterPivot.rotation;
	const bool rotated =
	    !TransformUtils::AreEqual(beforeOrientation, afterOrientation);

	const auto makeEdited = [&](const TransformSnapshot &snapshot) {
		Transform edited = snapshot.before;
		if (scaled) {
			// 元の軸ごとの比率と符号を保ったまま、同じ倍率を適用する
			edited.scale = {
			    snapshot.before.scale.x * scaleRatio,
			    snapshot.before.scale.y * scaleRatio,
			    snapshot.before.scale.z * scaleRatio,
			};
		}
		if (!rotated && !scaled) {
			// 移動だけの場合は、中心との減算・加算による位置の誤差を避ける
			edited.translation = {
			    snapshot.before.translation.x +
			        (afterPivot.translation.x - beforePivot.translation.x),
			    snapshot.before.translation.y +
			        (afterPivot.translation.y - beforePivot.translation.y),
			    snapshot.before.translation.z +
			        (afterPivot.translation.z - beforePivot.translation.z),
			};
			return edited;
		}

		// 開始時点の中心からの距離にも、同じ拡縮・回転を適用する
		XMVECTOR relativePosition = XMVectorSubtract(
		    XMLoadFloat3(&snapshot.before.translation), beforeCenter);
		if (scaled) {
			relativePosition = XMVectorScale(relativePosition, scaleRatio);
		}
		if (rotated) {
			relativePosition = XMVector3Rotate(relativePosition, deltaRotation);
		}
		XMStoreFloat3(
		    &edited.translation,
		    XMVectorAdd(afterCenter, relativePosition));

		if (rotated) {
			const XMVECTOR originalRotation =
			    XMQuaternionNormalize(XMLoadFloat4(&snapshot.before.rotation));
			// DirectXMathでは第1引数の回転の後に第2引数の回転を適用する
			XMStoreFloat4(
			    &edited.rotation,
			    XMQuaternionNormalize(
			        XMQuaternionMultiply(originalRotation, deltaRotation)));
		}
		return edited;
	};

	// 全件の検証が終わるまで、現在のシーンを変更しない
	for (const auto &snapshot : *pendingTransforms_) {
		if (!scene_.Contains(snapshot.objectId) ||
		    !TransformUtils::IsValid(makeEdited(snapshot))) {
			return false;
		}
	}

	for (const auto &snapshot : *pendingTransforms_) {
		scene_.FindObject(snapshot.objectId)->transform = makeEdited(snapshot);
	}
	return true;
}

bool EditorHistory::ResetTransforms(std::span<const SceneObjectId> objectIds) {
	CommitEdit();
	if (objectIds.empty()) {
		return false;
	}
	std::set<SceneObjectId> seen;
	std::vector<CommandGroup::Command> commands;
	commands.reserve(objectIds.size());
	const Transform reset;
	for (const SceneObjectId id : objectIds) {
		const SceneObject *object = scene_.FindObject(id);
		if (id == 0 || !seen.insert(id).second || !object ||
		    !TransformUtils::IsValid(object->transform)) {
			return false;
		}
		if (!TransformUtils::AreEqual(object->transform, reset)) {
			commands.emplace_back(TransformCommand{id, object->transform, reset});
		}
	}
	if (commands.empty()) {
		return true;
	}
	CommandGroup group{std::move(commands)};
	const std::uint64_t next = PrepareAppend();
	if (!group.Redo(scene_)) {
		return false;
	}
	AppendPrepared(Command{std::move(group)}, next,
	               objectIds.size() == 1 ? "Reset Transform" : "Reset Transforms");
	return true;
}

bool EditorHistory::HasPendingChanges() const {
	if (pendingTransforms_) {
		for (const auto &snapshot : *pendingTransforms_) {
			const SceneObject *object = scene_.FindObject(snapshot.objectId);
			if (!object ||
			    !TransformUtils::AreEqual(snapshot.before, object->transform)) {
				return true;
			}
		}
		return false;
	}

	if (!pending_) {
		return false;
	}

	const SceneObject *object = scene_.FindObject(pending_->objectId);

	if (!object) {
		return true;
	}

	return std::visit(
	    [object](const auto &before) {
		    using Before = std::decay_t<decltype(before)>;

		    if constexpr (std::is_same_v<Before, Transform>) {
			    return !TransformUtils::AreEqual(before, object->transform);
		    } else {
			    return before != object->name;
		    }
	    },
	    pending_->before);
}

SceneObjectId EditorHistory::CreateObjectWithHistory(SceneObject object) {
	if (object.name.empty()) {
		object.name = "Object";
	}

	// シーン変更後の履歴記録で、メモリ確保を発生させない
	const std::uint64_t next = PrepareAppend();

	// 履歴用のデータを保持したまま、完成した状態の物体を作成する
	const SceneObjectId id = scene_.CreateObject(object);

	AppendPrepared(Command{ObjectCommand{
	                   id,
	                   std::move(object),
	                   ObjectCommand::Kind::CREATE,
	               }},
	               next, "Create Object");

	return id;
}

bool EditorHistory::BeginTransform(SceneObjectId objectId,
                                   const Transform &before) {
	if (!scene_.Contains(objectId) || !TransformUtils::IsValid(before)) {
		return false;
	}

	PendingEdit edit{
	    objectId,
	    before,
	};

	CommitEdit();
	pending_ = std::move(edit);

	return true;
}

void EditorHistory::CommitEdit() {
	if (pendingTransforms_) {
		std::vector<CommandGroup::Command> commands;
		commands.reserve(pendingTransforms_->size());

		for (const auto &snapshot : *pendingTransforms_) {
			const SceneObject *object = scene_.FindObject(snapshot.objectId);
			if (!object || !TransformUtils::IsValid(object->transform)) {
				Invalidate();
				return;
			}
			if (TransformUtils::AreEqual(snapshot.before, object->transform)) {
				continue;
			}

			commands.emplace_back(TransformCommand{
			    snapshot.objectId, snapshot.before, object->transform});
		}

		// 元の位置へ戻った場合は履歴へ追加しない
		if (commands.empty()) {
			pendingTransforms_.reset();
			return;
		}

		Command command{CommandGroup{std::move(commands)}};
		const std::uint64_t next = PrepareAppend();
		AppendPrepared(std::move(command), next,
		               pendingTransforms_->size() == 1 ? "Transform Object" : "Transform Objects");
		pendingTransforms_.reset();
		return;
	}

	if (!pending_) {
		return;
	}

	SceneObject *object = scene_.FindObject(pending_->objectId);

	if (!object) {
		Invalidate();
		return;
	}

	if (std::holds_alternative<Transform>(pending_->before) &&
	    !TransformUtils::IsValid(object->transform)) {
		Invalidate();
		return;
	}

	// 空の名前で確定した場合は、編集開始時点の名前へ戻す
	if (std::holds_alternative<std::string>(pending_->before) &&
	    object->name.empty()) {
		auto &before = std::get<std::string>(pending_->before);

		object->name.swap(before);
		pending_.reset();
		return;
	}

	if (!HasPendingChanges()) {
		pending_.reset();
		return;
	}

	const SceneObjectId objectId = pending_->objectId;

	// 記録内容の作成に成功してから、履歴へ追加する
	Command command = std::visit(
	    [objectId, object](const auto &before) -> Command {
		    using Before = std::decay_t<decltype(before)>;

		    if constexpr (std::is_same_v<Before, Transform>) {
			    return Command{TransformCommand{
			        objectId,
			        before,
			        object->transform,
			    }};
		    } else {
			    return Command{RenameCommand{
			        objectId,
			        before,
			        object->name,
			    }};
		    }
	    },
	    pending_->before);

	const std::uint64_t next = PrepareAppend();

	AppendPrepared(std::move(command), next,
	               std::holds_alternative<Transform>(pending_->before)
	                   ? "Transform Object" : "Rename Object");
	pending_.reset();
}

bool EditorHistory::IsEditing() const {
	return pending_.has_value() || pendingTransforms_.has_value();
}

bool EditorHistory::CanUndo() const { return !IsEditing() && cursor_ > 0; }

bool EditorHistory::CanRedo() const {
	return !IsEditing() && cursor_ < entries_.size();
}

std::string_view EditorHistory::GetUndoDescription() const {
	return CanUndo() ? entries_[cursor_ - 1].description : std::string_view{};
}

std::string_view EditorHistory::GetRedoDescription() const {
	return CanRedo() ? entries_[cursor_].description : std::string_view{};
}

bool EditorHistory::Undo() {
	if (!CanUndo()) {
		return false;
	}

	try {
		const Entry &entry = entries_[cursor_ - 1];

		const bool succeeded = std::visit(
		    [this](const auto &command) { return command.Undo(scene_); },
		    entry.command);

		if (!succeeded) {
			Invalidate();
			return false;
		}

		currentState_ = entry.beforeState;
		--cursor_;

		return true;
	} catch (const std::exception &error) {
		// 復元時の確保失敗では、再試行できるよう履歴を保持する
		Logger::Error(std::format("EditorHistory::Undo: {}", error.what()));
		return false;
	}
}

bool EditorHistory::Redo() {
	if (!CanRedo()) {
		return false;
	}

	try {
		const Entry &entry = entries_[cursor_];

		const bool succeeded = std::visit(
		    [this](const auto &command) { return command.Redo(scene_); },
		    entry.command);

		if (!succeeded) {
			Invalidate();
			return false;
		}

		currentState_ = entry.afterState;
		++cursor_;

		return true;
	} catch (const std::exception &error) {
		Logger::Error(std::format("EditorHistory::Redo: {}", error.what()));
		return false;
	}
}

bool EditorHistory::IsModified() const {
	return currentState_ != savedState_ || HasPendingChanges();
}

void EditorHistory::MarkSaved() {
	CommitEdit();
	savedState_ = currentState_;
}

void EditorHistory::Invalidate() {
	pending_.reset();
	pendingTransforms_.reset();

	entries_.clear();
	cursor_ = 0;

	currentState_ = AllocateState();
}

void EditorHistory::Reset() {
	pending_.reset();
	pendingTransforms_.reset();

	entries_.clear();
	cursor_ = 0;

	currentState_ = 0;
	savedState_ = 0;
	nextState_ = 1;
}

std::uint64_t EditorHistory::PrepareAppend() {
	// シーン変更後の記録では、メモリ確保を発生させない
	static_assert(std::is_nothrow_move_constructible_v<Entry>);

	if (cursor_ == entries_.capacity()) {
		const std::size_t capacity = entries_.capacity();
		const std::size_t maximum = entries_.max_size();

		if (capacity == maximum) {
			throw std::length_error(
			    "EditorHistory: 履歴件数の上限に達しました。");
		}

		const std::size_t nextCapacity =
		    capacity == 0 ? 1
		                  : (capacity > maximum / 2 ? maximum : capacity * 2);

		entries_.reserve(nextCapacity);
	}

	return AllocateState();
}

void EditorHistory::AppendPrepared(Command command,
                                   std::uint64_t nextState,
                                   std::string_view description) noexcept {
	assert(cursor_ < entries_.capacity());

	// Undo後の新しい編集では、Redo側の履歴を取り除く
	entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(cursor_),
	               entries_.end());

	entries_.push_back(Entry{
	    std::move(command),
	    currentState_,
	    nextState,
	    description,
	});

	++cursor_;
	currentState_ = nextState;
}

std::uint64_t EditorHistory::AllocateState() {
	if (nextState_ == (std::numeric_limits<std::uint64_t>::max)()) {
		throw std::overflow_error(
		    "EditorHistory: 状態番号の上限に達しました。");
	}

	return nextState_++;
}
