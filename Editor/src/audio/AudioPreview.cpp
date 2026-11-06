#include "audio/AudioPreview.h"

#include "audio/AudioClipManager.h"
#include <algorithm>
#include <cmath>

AudioPreview::AudioPreview(AudioClipManager &clips, AudioSystem &audio)
    : clips_(clips), audio_(audio) {
}

AudioPreview::~AudioPreview() {
	Stop();
}

bool AudioPreview::Load(const std::filesystem::path &path, std::string &error) {
	auto selected = path;
	auto next = clips_.Reload(path, error);

	if (!next) {
		return false;
	}

	Stop();
	clip_ = std::move(next);
	path_.swap(selected);
	return true;
}

bool AudioPreview::Play(std::string &error) {
	if (!clip_) {
		error = "Choose a WAV clip before starting a preview.";
		return false;
	}

	Stop();
	voice_ = audio_.Play(clip_, settings_, error);
	return static_cast<bool>(voice_);
}

bool AudioPreview::Pause() {
	return audio_.Pause(voice_);
}

bool AudioPreview::Resume() {
	return audio_.Resume(voice_);
}

void AudioPreview::Stop() {
	if (voice_) {
		audio_.Stop(voice_);
		voice_ = {};
	}
}

bool AudioPreview::SetVolume(float volume) {
	auto next = settings_;
	next.volume = volume;

	if (!next.IsValid() || (GetState() != AudioPlaybackState::STOPPED &&
	                        !audio_.SetVolume(voice_, volume))) {
		return false;
	}

	settings_ = next;
	return true;
}

bool AudioPreview::SetLoop(bool loop) {
	if (loop != settings_.loop && GetState() != AudioPlaybackState::STOPPED) {
		return false;
	}

	settings_.loop = loop;
	return true;
}

AudioPlaybackState AudioPreview::GetState() const {
	return audio_.GetState(voice_);
}

double AudioPreview::GetPositionSeconds() const {
	if (!clip_ || GetState() == AudioPlaybackState::STOPPED) {
		return 0;
	}

	const auto frame = audio_.GetPlayedFrames(voice_);
	const auto position = static_cast<double>(frame) / clip_.GetSampleRate();
	const auto duration = clip_.GetDurationSeconds();
	return settings_.loop ? std::fmod(position, duration)
	                      : (std::min)(position, duration);
}

float AudioPreview::GetVolume() const {
	return settings_.volume;
}

bool AudioPreview::GetLoop() const {
	return settings_.loop;
}

const AudioClipHandle &AudioPreview::GetClip() const {
	return clip_;
}

const std::filesystem::path &AudioPreview::GetPath() const {
	return path_;
}

void AudioPreview::Clear() {
	Stop();
	clip_ = {};
	path_.clear();
	settings_ = {};
}
