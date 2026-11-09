#include "scene/SceneAudio.h"
#include "audio/AudioSystem.h"

#include "../audio/SpatialAudio.h"
#include "graphics/camera/Camera.h"
#include "scene/Scene.h"
#include "scene/SceneActivity.h"
#include "scene/SceneHierarchy.h"
#include <exception>
#include <stdexcept>
#include <utility>

namespace {
/// <summary>
/// 親子合成後の位置と、拡縮を含まない回転から受信姿勢を作る
/// </summary>
/// <param name="world">有効なワールド変形</param>
/// <returns>左手座標系の受信姿勢</returns>
AudioListenerPose FromWorld(const SceneWorldTransform &world) {
	using namespace DirectX;
	AudioListenerPose result;
	result.position = world.GetPosition();
	const auto rotation = XMLoadFloat4(&world.rotation);
	XMStoreFloat3(&result.forward,
	              XMVector3Rotate(XMVectorSet(0, 0, 1, 0), rotation));
	XMStoreFloat3(&result.up,
	              XMVector3Rotate(XMVectorSet(0, 1, 0, 0), rotation));
	return result;
}
} // namespace

bool SceneAudio::Collect(const Scene &scene, const CameraPose &camera,
                         SceneAudioFrame &frame, std::string &error,
                         SceneObjectId *invalidObject) {
	error.clear();
	if (invalidObject)
		*invalidObject = 0;
	SceneObjectId current = 0;

	try {
		SceneHierarchy::WorldTransforms transforms;
		SceneActivity::States active;

		if (!SceneHierarchy::BuildWorldTransforms(scene, transforms) ||
		    !SceneActivity::BuildStates(scene, active)) {
			throw std::runtime_error(
			    "Audio source or listener hierarchy is invalid.");
		}

		SceneAudioFrame next;

		for (const auto &[id, object] : scene.GetObjects()) {
			if (!active.at(id))
				continue;
			current = id;

			if (object.audioListener) {
				if (next.listenerObject)
					throw std::runtime_error(
					    "Only one Audio Listener may be active at once.");
				next.listenerObject = id;
				next.listener = FromWorld(transforms.at(id));
			}

			if (!object.audioSource)
				continue;
			const auto &source = *object.audioSource;
			if (!source.IsValid())
				throw std::runtime_error(
				    "Audio Source settings or WAV reference are invalid.");
			if (source.clipPath.empty())
				continue;
			SceneAudioSource item;
			item.id = id;
			item.component = source;
			item.playback.volume = source.volume;
			item.playback.loop = source.loop;
			item.playback.spatial = source.spatial;
			item.playback.emitter = {transforms.at(id).GetPosition(),
			                         source.minDistance, source.maxDistance};
			next.sources.push_back(std::move(item));
		}

		current = next.listenerObject;

		if (!next.listenerObject && !next.sources.empty()) {
			Camera checked;
			if (!checked.SetPose(camera))
				throw std::runtime_error(
				    "The fallback audio listener camera is invalid.");
			using namespace DirectX;
			next.listener.position = camera.position;
			const auto front = XMVector3Normalize(XMVectorSubtract(
			    XMLoadFloat3(&camera.target), XMLoadFloat3(&camera.position)));
			const auto right = XMVector3Normalize(
			    XMVector3Cross(XMLoadFloat3(&camera.up), front));
			XMStoreFloat3(&next.listener.forward, front);
			XMStoreFloat3(&next.listener.up, XMVector3Cross(front, right));
		}

		if (!next.listener.IsValid())
			throw std::runtime_error("Audio Listener world pose is invalid.");
		std::size_t automatic = 0;

		for (const auto &source : next.sources) {
			current = source.id;
			if (source.component.playOnStart &&
			    ++automatic > AudioSystem::kMaximumVoices)
				throw std::runtime_error(
				    "At most 64 Audio Sources may Play on Start at once.");
			if (source.playback.spatial &&
			    !SpatialAudio::CanCalculate(next.listener,
			                                source.playback.emitter)) {
				throw std::runtime_error("Audio Source distance from the "
				                         "listener cannot be represented.");
			}
		}

		frame = std::move(next);
		return true;
	} catch (const std::exception &exception) {
		error = exception.what();
		if (invalidObject)
			*invalidObject = current;
		return false;
	}
}
