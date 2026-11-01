#include "history/EditorHistory.h"

#include "core/Logger.h"
#include "math/TransformUtils.h"
#include "scene/Scene.h"

#include <cassert>
#include <exception>
#include <format>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>

EditorHistory::EditorHistory(Scene &scene) noexcept : scene_(scene) {}

SceneObjectId EditorHistory::CreateObject(std::string name,
                                          std::filesystem::path modelPath) {
	CommitTransform();

	SceneObject object;
	object.name = std::move(name);
	object.modelPath = std::move(modelPath);

	return CreateObjectWithHistory(std::move(object));
}

bool EditorHistory::DeleteObject(SceneObjectId objectId) {
	CommitTransform();

	const SceneObject *object = scene_.FindObject(objectId);

	if (!object) {
		return false;
	}

	// 削除する前に、名前・モデル参照・Transformをコピーする
	Command command{ObjectCommand{
	    objectId,
	    *object,
	    ObjectCommand::Kind::DELETE,
	}};

	const std::uint64_t next = PrepareAppend();

	const bool deleted = std::visit(
	    [this](const auto &operation) { return operation.Redo(scene_); },
	    command);

	if (!deleted) {
		return false;
	}

	AppendPrepared(std::move(command), next);
	return true;
}

SceneObjectId EditorHistory::DuplicateObject(SceneObjectId objectId) {
	CommitTransform();

	const SceneObject *source = scene_.FindObject(objectId);

	if (!source) {
		return 0;
	}

	// 複製元とは独立した物体データを作る
	SceneObject object = *source;

	if (object.name.empty()) {
		object.name = "Object";
	}

	object.name += " Copy";

	return CreateObjectWithHistory(std::move(object));
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
	               next);

	return id;
}

bool EditorHistory::BeginTransform(SceneObjectId objectId,
                                   const Transform &before) {
	if (!scene_.Contains(objectId) || !TransformUtils::IsValid(before)) {
		return false;
	}

	CommitTransform();

	pending_ = PendingTransform{objectId, before};
	return true;
}

void EditorHistory::CommitTransform() {
	if (!pending_) {
		return;
	}

	const SceneObject *object = scene_.FindObject(pending_->objectId);

	if (!object || !TransformUtils::IsValid(object->transform)) {
		Invalidate();
		return;
	}

	const TransformCommand command{
	    pending_->objectId,
	    pending_->before,
	    object->transform,
	};

	if (!command.HasChanges()) {
		pending_.reset();
		return;
	}

	const std::uint64_t next = PrepareAppend();

	AppendPrepared(Command{command}, next);
	pending_.reset();
}

bool EditorHistory::IsEditing() const { return pending_.has_value(); }

bool EditorHistory::CanUndo() const { return !pending_ && cursor_ > 0; }

bool EditorHistory::CanRedo() const {
	return !pending_ && cursor_ < entries_.size();
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
	if (currentState_ != savedState_) {
		return true;
	}

	if (!pending_) {
		return false;
	}

	const SceneObject *object = scene_.FindObject(pending_->objectId);

	return !object ||
	       !TransformUtils::AreEqual(pending_->before, object->transform);
}

void EditorHistory::MarkSaved() {
	CommitTransform();
	savedState_ = currentState_;
}

void EditorHistory::Invalidate() {
	pending_.reset();

	entries_.clear();
	cursor_ = 0;

	currentState_ = AllocateState();
}

void EditorHistory::Reset() {
	pending_.reset();

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
                                   std::uint64_t nextState) noexcept {
	assert(cursor_ < entries_.capacity());

	// Undo後の新しい編集では、Redo側の履歴を取り除く
	entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(cursor_),
	               entries_.end());

	entries_.push_back(Entry{
	    std::move(command),
	    currentState_,
	    nextState,
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
