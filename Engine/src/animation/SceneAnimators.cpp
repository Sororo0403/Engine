#include "animation/SceneAnimators.h"
#include "graphics/model/ModelLoader.h"
#include "platform/windows/PathUtils.h"
#include "scene/Scene.h"
#include "scene/SceneActivity.h"
#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <exception>
#include <format>
#include <stdexcept>
#include <utility>

bool SceneAnimators::PathLess::operator()(
    const std::filesystem::path &a, const std::filesystem::path &b) const {
	return CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) ==
	       CSTR_LESS_THAN;
}

bool SceneAnimators::Apply(State &state, std::string &error) {
	if (!state.applied) {
		state.pose.reset();
		return true;
	}
	if (state.fadeSource && state.fadeElapsed == 0) {
		state.pose = state.fadeSource;
		return true;
	}
	const float weight =
	    state.fadeSource
	        ? static_cast<float>(state.fadeElapsed / state.fadeDuration)
	        : 1.0f;
	auto pose = std::make_shared<ModelAnimationPose>();
	if (!state.resource->sampler.Sample(state.clip, state.cursor, false, *pose,
	                                    error, state.fadeSource.get(), weight))
		return false;
	state.pose = std::move(pose);
	return true;
}

bool SceneAnimators::Configure(
    const Scene &scene, bool preserve, std::string &error,
    SceneObjectId *invalidObject, const LoadModel &load,
    std::span<const std::shared_ptr<const Scene>> prefabs, bool allowLoad) {
	error.clear();
	SceneObjectId current = 0;
	try {
		auto models = preserve ? models_ : decltype(models_){};
		std::map<SceneObjectId, State> next;
		std::size_t totalVertices = 0, totalKeys = 0;
		for (const auto &[path, resource] : models) {
			(void)path;
			totalVertices += resource->vertices;
			totalKeys += resource->keys;
		}
		const auto resourceFor = [&](const SceneObject &object) {
			if (!object.HasMesh())
				throw std::runtime_error(
				    "Animator requires a Mesh with a model.");
			const auto path =
			    PathUtils::ResolveFromExecutable(object.mesh->modelPath)
			        .lexically_normal();
			if (auto at = models.find(path); at != models.end())
				return std::pair{path, at->second};
			if (!allowLoad)
				throw std::runtime_error(
				    "Animator model must be prepared before the callback.");
			if (models.size() >= kMaximumModels)
				throw std::runtime_error(
				    "Animator model cache exceeds 128 assets.");
			auto resource = std::make_shared<Resource>();
			if (load)
				resource->data = load(path, error);
			else {
				ModelData model;
				if (ModelLoader::Load(path, model))
					resource->data = std::move(model.animation);
			}
			if (!resource->data || resource->data->clips.empty())
				throw std::runtime_error(
				    error.empty()
				        ? "Animator needs a model containing an animation clip."
				        : error);
			if (!resource->sampler.Initialize(resource->data, error))
				throw std::runtime_error(error);
			for (const auto &part : resource->data->meshes)
				resource->vertices += part.mesh.vertices.size();
			for (const auto &clip : resource->data->clips)
				for (const auto &track : clip.tracks)
					resource->keys += track.positions.size() +
					                  track.rotations.size() +
					                  track.scales.size();
			if (resource->vertices > kMaximumVertices - totalVertices ||
			    resource->keys > kMaximumKeys - totalKeys)
				throw std::runtime_error(
				    "Animator model cache exceeds four million vertices or two "
				    "million keys.");
			totalVertices += resource->vertices;
			totalKeys += resource->keys;
			std::shared_ptr<const Resource> shared = resource;
			models.emplace(path, shared);
			return std::pair{path, shared};
		};
		const auto clipFor = [](const Resource &resource,
		                        std::string_view name) {
			if (name.empty())
				return std::size_t{0};
			const auto &clips = resource.data->clips;
			for (std::size_t i = 0; i < clips.size(); ++i)
				if (clips[i].name == name)
					return i;
			throw std::runtime_error(
			    std::format("Animator clip was not found: {}", name));
		};
		SceneActivity::States active;
		if (!SceneActivity::BuildStates(scene, active))
			throw std::runtime_error("Animator hierarchy is invalid.");
		for (const auto &[id, object] : scene.GetObjects()) {
			if (!object.animator)
				continue;
			current = id;
			const auto &settings = *object.animator;
			if (!settings.IsValid())
				throw std::runtime_error("Animator settings are invalid.");
			const auto [path, resource] = resourceFor(object);
			const auto initialClip = clipFor(*resource, settings.clip);
			State state;
			const auto old = states_.find(id);
			if (preserve && old != states_.end() &&
			    old->second.settings == settings &&
			    !PathLess{}(old->second.path, path) &&
			    !PathLess{}(path, old->second.path))
				state = old->second;
			else {
				state.settings = settings;
				state.path = path;
				state.resource = resource;
				state.clip = initialClip;
				state.speed = settings.speed;
				state.loop = settings.loop;
				state.playing = state.pendingStart = settings.playOnStart;
			}
			if (state.pendingStart && active.at(id)) {
				state.pendingStart = false;
				state.applied = true;
				if (!Apply(state, error))
					throw std::runtime_error(error);
			}
			next.emplace(id, std::move(state));
		}
		current = 0;
		for (const auto &prefab : prefabs) {
			if (!prefab)
				throw std::runtime_error("Animator Prefab data is missing.");
			for (const auto &[id, object] : prefab->GetObjects()) {
				(void)id;
				if (!object.animator)
					continue;
				if (!object.animator->IsValid())
					throw std::runtime_error(
					    "Prefab Animator settings are invalid.");
				const auto [path, resource] = resourceFor(object);
				(void)path;
				(void)clipFor(*resource, object.animator->clip);
			}
		}
		models_ = std::move(models);
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

bool SceneAnimators::Advance(const Scene &scene, float seconds,
                             std::string &error) {
	try {
		if (!std::isfinite(seconds) || seconds < 0)
			throw std::runtime_error("Animator time is invalid.");
		if (seconds == 0) {
			error.clear();
			return true;
		}
		SceneActivity::States active;
		if (!SceneActivity::BuildStates(scene, active))
			throw std::runtime_error("Animator hierarchy is invalid.");
		for (auto &[id, state] : states_) {
			const auto *object = scene.FindObject(id);
			if (!object || !object->animator)
				throw std::runtime_error("Animator object is missing.");
			if (!active.at(id) || !state.playing)
				continue;
			state.pendingStart = false;
			state.applied = true;
			const double duration =
			    state.resource->data->clips[state.clip].duration;
			const double step = (std::min)(seconds, .1f);
			if (state.fadeSource) {
				state.fadeElapsed += step;
				if (state.fadeElapsed >= state.fadeDuration)
					state.fadeSource.reset();
			}
			state.cursor += step * state.speed;
			if (state.loop)
				state.cursor = std::fmod(state.cursor, duration);
			else if (state.cursor >= duration) {
				state.cursor = duration;
				state.playing = bool(state.fadeSource);
			}
			if (!Apply(state, error))
				throw std::runtime_error(error);
		}
		error.clear();
		return true;
	} catch (const std::exception &e) {
		error = e.what();
		return false;
	}
}

bool SceneAnimators::Control(const Scene &scene, SceneObjectId id,
                             Operation operation, std::string_view clip,
                             double number, bool flag, std::string &error) {
	try {
		auto found = states_.find(id);
		const auto *object = scene.FindObject(id);
		if (found == states_.end() || !object || !object->animator)
			throw std::runtime_error(
			    "This object requires a prepared Animator.");
		auto state = found->second;
		if (operation != Operation::SPEED && operation != Operation::LOOP)
			state.pendingStart = false;
		if (operation == Operation::PLAY ||
		    operation == Operation::CROSS_FADE) {
			const bool fade = operation == Operation::CROSS_FADE;
			if (fade && (!std::isfinite(number) || number < 0 || number > 10 ||
			             clip.empty()))
				throw std::runtime_error(
				    "Animator cross-fade requires a clip name and a duration "
				    "from 0 to 10 seconds.");
			if (!SceneActivity::IsActive(scene, id))
				throw std::runtime_error(
				    "Activate the object before playing its Animator.");
			if (!AnimatorComponent::IsValidClip(clip))
				throw std::runtime_error("Animator clip name is invalid.");
			std::size_t index = state.clip;
			if (!clip.empty()) {
				const auto &clips = state.resource->data->clips;
				index = 0;
				while (index < clips.size() && clips[index].name != clip)
					++index;
				if (index == clips.size())
					throw std::runtime_error("Animator clip was not found.");
			}
			if (fade && number > 0 && (index != state.clip || !state.applied)) {
				state.fadeSource = state.pose;
				if (!state.fadeSource) {
					auto bind = std::make_shared<ModelAnimationPose>();
					if (!state.resource->sampler.Sample(std::nullopt, 0, false,
					                                    *bind, error))
						return false;
					state.fadeSource = std::move(bind);
				}
				state.fadeElapsed = 0;
				state.fadeDuration = number;
			} else if ((!fade && (flag || index != state.clip)) ||
			           (fade && number == 0)) {
				state.fadeSource.reset();
			}
			if ((!fade && flag) || index != state.clip || (fade && number == 0))
				state.cursor = 0;
			state.clip = index;
			state.playing = state.applied = true;
		} else if (operation == Operation::PAUSE)
			state.playing = false;
		else if (operation == Operation::STOP) {
			state.cursor = 0;
			state.fadeSource.reset();
			state.playing = state.applied = false;
		} else if (operation == Operation::SEEK) {
			const double duration =
			    state.resource->data->clips[state.clip].duration;
			if (!std::isfinite(number) || number < 0 || number > duration)
				throw std::runtime_error(
				    "Animator seek must be inside the clip duration.");
			state.fadeSource.reset();
			state.cursor = number;
			state.applied = true;
			if (!state.loop && number == duration)
				state.playing = false;
		} else if (operation == Operation::SPEED) {
			if (!std::isfinite(number) || number <= 0 ||
			    number > AnimatorComponent::kMaximumSpeed)
				throw std::runtime_error(
				    "Animator speed must be greater than zero and at most 16.");
			const float speed = static_cast<float>(number);
			if (!std::isfinite(speed) || speed <= 0)
				throw std::runtime_error(
				    "Animator speed is too small to represent.");
			state.speed = speed;
		} else if (operation == Operation::LOOP)
			state.loop = flag;
		else
			throw std::runtime_error("Animator operation is invalid.");
		if (!Apply(state, error))
			return false;
		found->second = std::move(state);
		error.clear();
		return true;
	} catch (const std::exception &e) {
		error = e.what();
		return false;
	}
}

std::optional<AnimatorPlaybackState>
SceneAnimators::GetState(SceneObjectId id) const {
	const auto at = states_.find(id);
	if (at == states_.end())
		return std::nullopt;
	const auto &s = at->second;
	const auto &clip = s.resource->data->clips[s.clip];
	return AnimatorPlaybackState{clip.name, s.cursor,  clip.duration, s.speed,
	                             s.loop,    s.playing, s.applied};
}

bool SceneAnimators::Snapshot(const Scene &scene,
                              std::vector<AnimatorDrawPose> &poses,
                              std::string &error) const {
	try {
		SceneActivity::States active;
		if (!SceneActivity::BuildStates(scene, active))
			throw std::runtime_error("Animator hierarchy is invalid.");
		std::vector<AnimatorDrawPose> next;
		for (const auto &[id, state] : states_) {
			if (!scene.Contains(id))
				throw std::runtime_error("Animator object is missing.");
			if (active.at(id) && state.applied && state.pose)
				next.push_back({id, state.resource->data, state.pose});
		}
		poses = std::move(next);
		error.clear();
		return true;
	} catch (const std::exception &e) {
		error = e.what();
		return false;
	}
}
