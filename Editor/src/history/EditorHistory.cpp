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
	CommitEdit();

	SceneObject object;
	object.name = std::move(name);
	object.modelPath = std::move(modelPath);

	return CreateObjectWithHistory(std::move(object));
}

bool EditorHistory::DeleteObject(SceneObjectId objectId) {
	CommitEdit();

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
	CommitEdit();

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

bool EditorHistory::HasPendingChanges() const {
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
	               next);

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

	AppendPrepared(std::move(command), next);
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
	return currentState_ != savedState_ || HasPendingChanges();
}

void EditorHistory::MarkSaved() {
	CommitEdit();
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
