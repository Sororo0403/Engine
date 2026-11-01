#include "history/EditorHistory.h"

#include "math/TransformUtils.h"
#include "scene/Scene.h"

#include <limits>
#include <stdexcept>

EditorHistory::EditorHistory(Scene &scene) noexcept : scene_(scene) {}

bool EditorHistory::BeginTransform(SceneObjectId objectId,
                                   const Transform &before) {
	if (!scene_.Contains(objectId) || !TransformUtils::IsValid(before)) {
		return false;
	}

	// 別の編集が残っていた場合は、その編集を先に確定する
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

	const std::uint64_t next = AllocateState();

	// 確保に成功してからRedo側の履歴を取り除く
	entries_.reserve(cursor_ + 1);

	entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(cursor_),
	               entries_.end());

	entries_.push_back(Entry{
	    command,
	    currentState_,
	    next,
	});

	++cursor_;
	currentState_ = next;

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

	const Entry &entry = entries_[cursor_ - 1];

	if (!entry.command.Undo(scene_)) {
		Invalidate();
		return false;
	}

	currentState_ = entry.beforeState;
	--cursor_;

	return true;
}

bool EditorHistory::Redo() {
	if (!CanRedo()) {
		return false;
	}

	const Entry &entry = entries_[cursor_];

	if (!entry.command.Redo(scene_)) {
		Invalidate();
		return false;
	}

	currentState_ = entry.afterState;
	++cursor_;

	return true;
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

	// 保存地点とは異なる、新しい状態として扱う
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

std::uint64_t EditorHistory::AllocateState() {
	if (nextState_ == (std::numeric_limits<std::uint64_t>::max)()) {
		throw std::overflow_error(
		    "EditorHistory: 状態番号の上限に達しました。");
	}

	return nextState_++;
}
