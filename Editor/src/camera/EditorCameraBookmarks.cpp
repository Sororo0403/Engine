#include "camera/EditorCameraBookmarks.h"

bool EditorCameraBookmarks::Set(std::size_t slot,
                                const EditorCameraController::ViewState &state,
                                std::optional<CameraSettings> projection) {
	EditorCameraController validator;

	if (slot >= slots_.size() || (projection && !projection->IsValid()) ||
	    !validator.SetViewState(state)) {
		return false;
	}

	slots_[slot] = Bookmark{state, projection};
	return true;
}

const EditorCameraController::ViewState *
EditorCameraBookmarks::Get(std::size_t slot) const {
	return slot < slots_.size() && slots_[slot] ? &slots_[slot]->view : nullptr;
}

const CameraSettings *
EditorCameraBookmarks::GetProjection(std::size_t slot) const {
	return slot < slots_.size() && slots_[slot] && slots_[slot]->projection
	           ? &*slots_[slot]->projection
	           : nullptr;
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
		if (slot && (!validator.SetViewState(slot->view) ||
		             (slot->projection && !slot->projection->IsValid()))) {
			return false;
		}
	}

	return true;
}
