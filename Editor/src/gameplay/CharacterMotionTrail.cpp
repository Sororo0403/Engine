#include "gameplay/CharacterMotionTrail.h"

#include "gameplay/EditorPlaySession.h"
#include "math/MathUtils.h"
#include "scene/SceneRenderer.h"

void CharacterMotionTrail::Clear() noexcept {
	lines_.clear();
	previous_.reset();
	character_ = 0;
	frame_ = 0;
	respawns_ = 0;
}

void CharacterMotionTrail::Record(const EditorPlaySession &session) {
	const auto state = session.GetCharacterState();

	if (!state || !MathUtils::IsFinite(state->position)) {
		Clear();
		return;
	}

	const auto frame = session.GetSimulationFrameCount();

	if (state->objectId != character_ || frame == 0 || frame < frame_) {
		Clear();
	}

	if (previous_ && frame == frame_) {
		return;
	}

	const auto position = state->position;
	const bool moved =
	    previous_ && (position.x != previous_->x ||
	                  position.y != previous_->y || position.z != previous_->z);

	if (moved && state->respawnCount == respawns_) {
		const DirectX::XMFLOAT4 color =
		    state->grounded ? DirectX::XMFLOAT4{.3f, 1, .45f, 1}
		                    : DirectX::XMFLOAT4{1, .6f, .15f, 1};
		lines_.push_back({*previous_, position, color});

		if (lines_.size() > kMaximumSegments) {
			lines_.pop_front();
		}
	}

	previous_ = position;
	character_ = state->objectId;
	frame_ = frame;
	respawns_ = state->respawnCount;
}

std::size_t CharacterMotionTrail::GetLineCount() const noexcept {
	return lines_.size();
}

const DebugLine *
CharacterMotionTrail::GetLine(std::size_t index) const noexcept {
	return index < lines_.size() ? &lines_[index] : nullptr;
}

bool CharacterMotionTrail::Update(SceneRenderer &renderer) const {
	for (const auto &line : lines_) {
		if (!renderer.AddDebugLine(line)) {
			return false;
		}
	}

	return true;
}
