#include "animation/SceneAnimations.h"
#include "math/TransformUtils.h"
#include "scene/Scene.h"
#include "scene/SceneActivity.h"
#include <algorithm>
#include <cmath>
#include <exception>
#include <stdexcept>
#include <utility>

bool SceneAnimations::Apply(State &state, Transform &transform) {
	Transform delta, result;
	if (!state.settings.Sample(state.cursor, delta))
		return false;
	const auto &base = state.base;
	result.translation = {base.translation.x + delta.translation.x,
	                      base.translation.y + delta.translation.y,
	                      base.translation.z + delta.translation.z};
	result.scale = {base.scale.x * delta.scale.x, base.scale.y * delta.scale.y,
	                base.scale.z * delta.scale.z};
	using namespace DirectX;
	XMStoreFloat4(&result.rotation, XMQuaternionNormalize(XMQuaternionMultiply(
	                                    XMLoadFloat4(&delta.rotation),
	                                    XMLoadFloat4(&base.rotation))));
	if (!TransformUtils::IsValid(result))
		return false;
	transform = state.last = result;
	state.applied = true;
	return true;
}

bool SceneAnimations::Rebase(State &state, const Transform &transform) {
	if (!TransformUtils::IsValid(transform))
		return false;
	if (!state.applied) {
		state.base = state.last = transform;
		return true;
	}

	if (TransformUtils::AreEqual(state.last, transform))
		return true;
	Transform delta, base;
	if (!state.settings.Sample(state.cursor, delta))
		return false;
	base.translation = {transform.translation.x - delta.translation.x,
	                    transform.translation.y - delta.translation.y,
	                    transform.translation.z - delta.translation.z};
	base.scale = {transform.scale.x / delta.scale.x,
	              transform.scale.y / delta.scale.y,
	              transform.scale.z / delta.scale.z};
	using namespace DirectX;
	XMStoreFloat4(&base.rotation,
	              XMQuaternionNormalize(XMQuaternionMultiply(
	                  XMQuaternionInverse(XMLoadFloat4(&delta.rotation)),
	                  XMLoadFloat4(&transform.rotation))));
	if (!TransformUtils::IsValid(base))
		return false;
	state.base = base;
	state.last = transform;
	return true;
}

bool SceneAnimations::Configure(Scene &scene, bool preserve, std::string &error,
                                SceneObjectId *invalidObject) {
	SceneObjectId current = 0;

	try {
		std::map<SceneObjectId, State> next;
		SceneActivity::States active;
		if (!SceneActivity::BuildStates(scene, active))
			throw std::runtime_error("Animation hierarchy is invalid.");
		for (const auto &[id, object] : scene.GetObjects()) {
			if (!object.transformAnimation)
				continue;
			current = id;
			const auto &settings = *object.transformAnimation;
			if (!settings.IsValid())
				throw std::runtime_error(
				    "Transform Animation settings are invalid.");
			State state;
			const auto old = states_.find(id);

			if (preserve && old != states_.end() &&
			    old->second.settings == settings) {
				state = old->second;
				if (!Rebase(state, object.transform))
					throw std::runtime_error("Animation origin is invalid.");
			} else {
				state.settings = settings;
				state.base = state.last = object.transform;
				state.playing = state.pendingStart = settings.playOnStart;
			}

			if (state.pendingStart && active.at(id)) {
				state.pendingStart = false;
				if (!Apply(state, scene.FindObject(id)->transform))
					throw std::runtime_error(
					    "Animation start pose is invalid.");
			}

			next.emplace(id, std::move(state));
		}

		states_ = std::move(next);
		error.clear();
		if (invalidObject)
			*invalidObject = 0;
		return true;
	} catch (const std::exception &e) {
		error = e.what();
		if (invalidObject)
			*invalidObject = current;
		return false;
	}
}

bool SceneAnimations::Advance(Scene &scene, float seconds, std::string &error) {
	try {
		if (!std::isfinite(seconds) || seconds < 0)
			throw std::runtime_error("Animation time is invalid.");
		if (seconds == 0) {
			error.clear();
			return true;
		}

		for (auto &[id, state] : states_) {
			auto *object = scene.FindObject(id);
			if (!object || !object->transformAnimation)
				throw std::runtime_error("Animation object is missing.");
			if (!SceneActivity::IsActive(scene, id) || !state.playing)
				continue;
			if (!Rebase(state, object->transform))
				throw std::runtime_error("Animation origin is invalid.");
			state.cursor +=
			    double((std::min)(seconds, .1f)) * state.settings.speed;
			const auto duration = state.settings.GetDuration();

			if (state.settings.mode ==
			    TransformAnimationComponent::Mode::ONCE) {
				if (state.cursor >= duration) {
					state.cursor = duration;
					state.playing = false;
				}
			} else
				state.cursor = std::fmod(
				    state.cursor,
				    state.settings.mode ==
				            TransformAnimationComponent::Mode::PING_PONG
				        ? duration * 2
				        : duration);
			if (!Apply(state, object->transform))
				throw std::runtime_error("Animated pose is invalid.");
		}

		error.clear();
		return true;
	} catch (const std::exception &e) {
		error = e.what();
		return false;
	}
}

bool SceneAnimations::Control(Scene &scene, SceneObjectId id,
                              Operation operation, double seconds, bool restart,
                              std::string &error) {
	try {
		auto found = states_.find(id);
		auto *object = scene.FindObject(id);
		if (found == states_.end() || !object || !object->transformAnimation)
			throw std::runtime_error(
			    "This object requires Transform Animation.");
		auto state = found->second;
		auto transform = object->transform;
		if (!Rebase(state, transform))
			throw std::runtime_error("Animation origin is invalid.");
		state.pendingStart = false;

		if (operation == Operation::PLAY) {
			if (!SceneActivity::IsActive(scene, id))
				throw std::runtime_error(
				    "Activate the object before playing its animation.");
			if (restart)
				state.cursor = 0;
			state.playing = true;
			if (!Apply(state, transform))
				throw std::runtime_error("Animation start pose is invalid.");
		} else if (operation == Operation::PAUSE)
			state.playing = false;
		else if (operation == Operation::STOP) {
			state.cursor = 0;
			state.playing = false;
			transform = state.last = state.base;
			state.applied = false;
		} else if (operation == Operation::SEEK) {
			if (!std::isfinite(seconds) || seconds < 0 ||
			    seconds > state.settings.GetDuration())
				throw std::runtime_error(
				    "Animation seek must be within the clip duration.");
			state.cursor = seconds;
			if (state.settings.mode ==
			        TransformAnimationComponent::Mode::ONCE &&
			    seconds == state.settings.GetDuration())
				state.playing = false;
			if (!Apply(state, transform))
				throw std::runtime_error("Animation seek pose is invalid.");
		} else
			throw std::runtime_error("Animation operation is invalid.");
		found->second = std::move(state);
		object->transform = transform;
		error.clear();
		return true;
	} catch (const std::exception &e) {
		error = e.what();
		return false;
	}
}

std::optional<AnimationPlaybackState>
SceneAnimations::GetState(SceneObjectId id) const {
	const auto found = states_.find(id);
	if (found == states_.end())
		return std::nullopt;
	return AnimationPlaybackState{
	    found->second.settings.GetClipTime(found->second.cursor),
	    found->second.playing};
}
