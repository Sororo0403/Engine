#include "camera/EditorCameraBookmarks.h"

bool EditorCameraBookmarks::Set(
    std::size_t slot, const EditorCameraController::ViewState &state) {
	EditorCameraController validator;
	if (slot >= slots_.size() || !validator.SetViewState(state)) {
		return false;
	}
	slots_[slot] = state;
	return true;
}

const EditorCameraController::ViewState *
EditorCameraBookmarks::Get(std::size_t slot) const {
	return slot < slots_.size() && slots_[slot] ? &*slots_[slot] : nullptr;
}

bool EditorCameraBookmarks::Clear(std::size_t slot) {
	if (!Get(slot)) {
		return false;
	}
	slots_[slot].reset();
	return true;
}

bool EditorCameraBookmarks::IsValid() const {
	EditorCameraController validator;
	for (const auto &slot : slots_) {
		if (slot && !validator.SetViewState(*slot)) {
			return false;
		}
	}
	return true;
}
