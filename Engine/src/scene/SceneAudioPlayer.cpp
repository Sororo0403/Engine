#include "scene/SceneAudioPlayer.h"

#include "audio/AudioClipManager.h"
#include "core/Logger.h"
#include "scene/Scene.h"
#include <exception>
#include <format>
#include <utility>

SceneAudioPlayer::SceneAudioPlayer(const Scene &scene, AudioClipManager &clips,
                                   AudioSystem &audio)
    : scene_(scene), clips_(clips), audio_(audio) {
}

SceneAudioPlayer::~SceneAudioPlayer() {
	Stop();
}

bool SceneAudioPlayer::Start(const CameraPose &camera, bool paused,
                             std::string &error, SceneObjectId *invalidObject,
                             const SceneAudioActions &actions) {
	SceneAudioFrame frame;
	if (!SceneAudio::Collect(scene_, camera, frame, error, invalidObject))
		return false;
	if (!Synchronize(frame, paused, true, error, invalidObject, actions))
		return false;
	started_ = true;
	return true;
}

bool SceneAudioPlayer::Update(const CameraPose &camera, bool paused,
                              std::string &error, SceneObjectId *invalidObject,
                              const SceneAudioActions &actions) {
	if (!started_) {
		error = "Start scene audio before updating it.";
		if (invalidObject)
			*invalidObject = 0;
		return false;
	}

	SceneAudioFrame frame;
	return SceneAudio::Collect(scene_, camera, frame, error, invalidObject) &&
	       Synchronize(frame, paused, false, error, invalidObject, actions);
}

bool SceneAudioPlayer::Synchronize(const SceneAudioFrame &frame, bool paused,
                                   bool restart, std::string &error,
                                   SceneObjectId *invalidObject,
                                   const SceneAudioActions &actions) {
	error.clear();
	if (invalidObject)
		*invalidObject = 0;
	SceneObjectId current = 0;

	try {
		if (!restart && actionRevision_ &&
		    actions.revision < *actionRevision_) {
			error =
			    "Use the latest simulation audio actions when synchronizing "
			    "scene audio.";
			return false;
		}

		std::map<SceneObjectId, SceneAudioAction::Operation> requested;

		if (restart || !actionRevision_ ||
		    actions.revision != *actionRevision_) {
			for (const auto &action : actions.commands) {
				current = action.target;
				const auto *target = scene_.FindObject(action.target);

				if (!action.IsValid() || !target || !target->audioSource) {
					error = "Scene audio actions require a valid operation and "
					        "an existing Audio Source target.";
					if (invalidObject) {
						*invalidObject = current;
					}

					return false;
				}

				requested[action.target] = action.operation;
			}
		}

		std::map<SceneObjectId, Entry> next;

		for (const auto &source : frame.sources) {
			current = source.id;
			const auto old = entries_.find(source.id);
			if (!restart && old != entries_.end() &&
			    old->second.component.clipPath == source.component.clipPath &&
			    old->second.component.loop == source.component.loop &&
			    old->second.component.spatial == source.component.spatial) {
				next.emplace(source.id, old->second);
				next.at(source.id).component = source.component;
				next.at(source.id).playback = source.playback;
				continue;
			}

			Entry entry;
			entry.component = source.component;
			entry.playback = source.playback;
			entry.clip = clips_.Load(source.component.clipPath, error);

			if (!entry.clip) {
				if (invalidObject)
					*invalidObject = current;
				error = std::format("Audio Source object ID {}: {}", current,
				                    error);
				return false;
			}

			if (source.component.spatial && entry.clip.GetChannels() != 1) {
				error = "Spatial Audio Source needs a mono WAV clip.";
				if (invalidObject)
					*invalidObject = current;
				return false;
			}

			entry.pending = source.component.playOnStart;
			next.emplace(source.id, std::move(entry));
		}

		// 先に最終要求を候補へ反映し、同じ更新内で停止する音を一瞬でも開始しない。
		for (const auto &[id, operation] : requested) {
			if (const auto found = next.find(id); found != next.end()) {
				found->second.handle = {};
				found->second.pending =
				    operation == SceneAudioAction::Operation::PLAY;
			}
		}

		// 新しい全参照を確認してから、無効化・削除・再設定された音だけを破棄する。
		for (const auto &[id, entry] : entries_) {
			const auto kept = next.find(id);
			if (kept == next.end() || kept->second.handle != entry.handle)
				audio_.Stop(entry.handle);
		}

		entries_.swap(next);
		const bool deviceChanged =
		    deviceRevision_ != audio_.GetDeviceRevision();
		std::vector<AudioEmitterChange> changes;

		for (const auto &source : frame.sources) {
			auto &entry = entries_.at(source.id);

			if (!audio_.IsAvailable()) {
				if (entry.handle) {
					audio_.Stop(entry.handle);
					entry.handle = {};
					entry.pending = true;
				}

				continue;
			}

			if (entry.handle &&
			    (deviceChanged || audio_.GetState(entry.handle) ==
			                          AudioPlaybackState::STOPPED)) {
				if (deviceChanged)
					entry.pending = true;
				audio_.Stop(entry.handle);
				entry.handle = {};
			}

			if (entry.handle && source.playback.spatial)
				changes.push_back({entry.handle, source.playback.emitter});
		}

		if (!audio_.SetSpatialState(frame.listener, changes)) {
			error = "The scene audio listener and emitter poses could not be "
			        "applied.";
			return false;
		}

		for (const auto &source : frame.sources) {
			current = source.id;
			auto &entry = entries_.at(source.id);

			if (entry.pending && audio_.IsAvailable()) {
				auto settings = source.playback;
				settings.startPaused = paused;
				entry.handle = audio_.Play(entry.clip, settings, error);

				if (!entry.handle) {
					if (invalidObject)
						*invalidObject = current;
					Stop();
					return false;
				}

				entry.pending = false;
				Logger::Info(std::format("Audio Source ID {} queued (loop={}, "
				                         "spatial={}, paused={}).",
				                         current, settings.loop,
				                         settings.spatial, paused));
			}

			// 音量だけの変更では再生位置・停止状態を維持する。自然終了との競合は許容する。
			if (entry.handle &&
			    !audio_.SetVolume(entry.handle, source.playback.volume) &&
			    audio_.GetState(entry.handle) != AudioPlaybackState::STOPPED) {
				error = "The scene Audio Source volume could not be applied.";
				if (invalidObject)
					*invalidObject = current;
				return false;
			}
			if (entry.handle && !(paused ? audio_.Pause(entry.handle)
			                             : audio_.Resume(entry.handle))) {
				// 自然終了が状態確認と操作の間に起きた場合は、その音だけ終了として扱う。
				if (audio_.GetState(entry.handle) !=
				    AudioPlaybackState::STOPPED) {
					error = "The scene Audio Source could not pause or resume.";
					if (invalidObject)
						*invalidObject = current;
					return false;
				}
			}
		}

		deviceRevision_ = audio_.GetDeviceRevision();
		paused_ = paused;
		actionRevision_ = actions.revision;
		return true;
	} catch (const std::exception &exception) {
		error = std::format("Scene audio object ID {}: {}", current,
		                    exception.what());
		if (invalidObject)
			*invalidObject = current;
		return false;
	}
}

void SceneAudioPlayer::Stop() {
	for (const auto &[id, entry] : entries_)
		audio_.Stop(entry.handle);
	entries_.clear();
	started_ = false;
	actionRevision_.reset();
}

AudioPlaybackState SceneAudioPlayer::GetState(SceneObjectId id) const {
	const auto found = entries_.find(id);
	return found == entries_.end() ? AudioPlaybackState::STOPPED
	                               : audio_.GetState(found->second.handle);
}

bool SceneAudioPlayer::PlaySource(SceneObjectId id, std::string &error) {
	error.clear();
	auto found = entries_.find(id);

	if (!started_ || found == entries_.end()) {
		error = "Choose an active configured Audio Source after starting scene "
		        "audio.";
		return false;
	}

	auto &entry = found->second;
	audio_.Stop(entry.handle);
	entry.handle = {};
	entry.pending = true;
	if (!audio_.IsAvailable())
		return true;
	auto settings = entry.playback;
	settings.startPaused = paused_;
	entry.handle = audio_.Play(entry.clip, settings, error);

	if (!entry.handle) {
		entry.pending = false;
		return false;
	}

	entry.pending = false;
	return true;
}

bool SceneAudioPlayer::StopSource(SceneObjectId id) {
	auto found = entries_.find(id);
	if (found == entries_.end())
		return false;
	audio_.Stop(found->second.handle);
	found->second.handle = {};
	found->second.pending = false;
	return true;
}
