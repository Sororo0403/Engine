#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include "audio/AudioSystem.h"

#include "SpatialAudio.h"
#include <array>
#include <cmath>
#include <directxtk12/Audio.h>
#include <exception>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
/// <summary>
/// メインスレッド上で音声基盤を識別する番号を発行する
/// </summary>
/// <returns>ほかの基盤で使用していない番号</returns>
std::uint64_t AllocateSystemId() {
	static std::uint64_t next = 1;

	if (next == (std::numeric_limits<std::uint64_t>::max)()) {
		throw std::overflow_error("Audio system IDs are exhausted.");
	}

	return next++;
}

/// <summary>
/// 音量として使用できる有限値か確認する
/// </summary>
/// <param name="volume">確認する値</param>
/// <returns>true:0から上限 false:非有限または範囲外</returns>
bool ValidVolume(float volume) {
	return std::isfinite(volume) && volume >= 0 &&
	       volume <= AudioPlaybackSettings::kMaximumVolume;
}
} // namespace

/// <summary>
/// デバイスと再生世代を保持する。再生を破棄するまでサンプルの共有参照を解放しない
/// </summary>
struct AudioSystem::Impl {
	struct Voice {
		~Voice() {
			// DestroyVoiceが完了するまで、別スレッドが参照するサンプルを保持する。
			if (source)
				source->DestroyVoice();
		}

		IXAudio2SourceVoice *source = nullptr;
		std::shared_ptr<const WaveData> data;
		AudioPlaybackSettings settings;
		bool paused = false;
	};

	struct Slot {
		std::unique_ptr<Voice> voice;
		std::uint64_t generation = 1;
	};

	const std::uint64_t id = AllocateSystemId();
	std::unique_ptr<DirectX::AudioEngine> engine;
	std::vector<Slot> slots;
	AudioListenerPose listener{};
	float masterVolume = 1;
	bool suspended = false;
	std::uint64_t deviceRevision = 0;

	Voice *Find(AudioPlaybackHandle handle) const {
		if (handle.system_ != id || handle.index_ >= slots.size())
			return nullptr;
		const auto &slot = slots[handle.index_];
		return slot.generation == handle.generation_ ? slot.voice.get()
		                                             : nullptr;
	}

	void Release(Slot &slot) {
		if (!slot.voice)
			return;
		slot.voice.reset();
		if (slot.generation != (std::numeric_limits<std::uint64_t>::max)())
			++slot.generation;
	}

	void CollectFinished() {
		for (auto &slot : slots) {
			if (!slot.voice)
				continue;
			XAUDIO2_VOICE_STATE state{};
			slot.voice->source->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
			if (state.BuffersQueued == 0)
				Release(slot);
		}
	}

	bool ApplySpatial(Voice &voice) const {
		if (!voice.settings.spatial)
			return true;
		const auto channels = engine->GetOutputChannels();
		if (!channels || channels > 64)
			return false;
		std::array<float, 64> matrix{};
		return SpatialAudio::Calculate(listener, voice.settings.emitter,
		                               engine->GetChannelMask(),
		                               std::span(matrix).first(channels)) &&
		       SUCCEEDED(voice.source->SetOutputMatrix(
		           engine->GetMasterVoice(), 1, channels, matrix.data()));
	}
};

AudioSystem::AudioSystem() : impl_(std::make_unique<Impl>()) {
}

AudioSystem::~AudioSystem() {
	Finalize();
}

bool AudioSystem::Initialize(std::string &error) {
	error.clear();

	try {
		if (impl_->engine)
			throw std::logic_error("The audio system is already initialized.");
		auto engine = std::make_unique<DirectX::AudioEngine>();
		engine->SetMaxVoicePool(0, kMaximumVoices);
		engine->SetMasterVolume(impl_->masterVolume);
		impl_->engine = std::move(engine);
		++impl_->deviceRevision;
		return true;
	} catch (const std::exception &exception) {
		error = exception.what();
		return false;
	}
}

bool AudioSystem::Update(std::string &error) {
	error.clear();

	try {
		if (!impl_->engine)
			throw std::logic_error(
			    "Initialize the audio system before updating it.");
		if (!impl_->engine->Update()) {
			StopAll();
			if (impl_->engine->IsCriticalError())
				throw std::runtime_error(
				    "The audio output device was lost. Restart the application "
				    "to resume playback.");
			return true;
		}

		impl_->CollectFinished();

		for (auto &slot : impl_->slots) {
			if (slot.voice && !impl_->ApplySpatial(*slot.voice)) {
				throw std::runtime_error(
				    "The spatial audio output matrix could not be updated.");
			}
		}

		return true;
	} catch (const std::exception &exception) {
		error = exception.what();
		return false;
	}
}

void AudioSystem::Finalize() {
	StopAll();
	if (impl_->engine)
		++impl_->deviceRevision;
	impl_->engine.reset();
	impl_->suspended = false;
	impl_->listener = AudioListenerPose{};
	impl_->masterVolume = 1;
}

bool AudioSystem::IsAvailable() const {
	return impl_->engine && impl_->engine->IsAudioDevicePresent() &&
	       !impl_->engine->IsCriticalError();
}

AudioPlaybackHandle AudioSystem::Play(const AudioClipHandle &clip,
                                      const AudioPlaybackSettings &settings,
                                      std::string &error) {
	error.clear();

	try {
		if (!IsAvailable())
			throw std::runtime_error("No audio output device is available.");
		if (!clip || !settings.IsValid() ||
		    (settings.spatial && (clip.GetChannels() != 1 ||
		                          !SpatialAudio::CanCalculate(
		                              impl_->listener, settings.emitter)))) {
			throw std::invalid_argument(
			    "Use a valid audio clip and playback settings. Spatial audio "
			    "requires mono samples and a finite listener distance.");
		}

		impl_->CollectFinished();
		std::size_t index = 0;
		while (index < impl_->slots.size() &&
		       (impl_->slots[index].voice ||
		        impl_->slots[index].generation ==
		            (std::numeric_limits<std::uint64_t>::max)()))
			++index;
		if (index >= kMaximumVoices)
			throw std::runtime_error(
			    "At most 64 audio voices can play or remain paused at once.");
		auto voice = std::make_unique<Impl::Voice>();
		voice->data = clip.data_;
		voice->settings = settings;
		voice->paused = settings.startPaused;
		WAVEFORMATEX format{};
		format.wFormatTag = voice->data->format == WaveSampleFormat::FLOAT32
		                        ? WAVE_FORMAT_IEEE_FLOAT
		                        : WAVE_FORMAT_PCM;
		format.nChannels = voice->data->channels;
		format.nSamplesPerSec = voice->data->sampleRate;
		format.wBitsPerSample =
		    static_cast<WORD>(voice->data->GetBytesPerSample() * 8);
		format.nBlockAlign = static_cast<WORD>(
		    format.nChannels * voice->data->GetBytesPerSample());
		format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
		if (FAILED(impl_->engine->GetInterface()->CreateSourceVoice(
		        &voice->source, &format)) ||
		    FAILED(voice->source->SetVolume(settings.volume)) ||
		    !impl_->ApplySpatial(*voice)) {
			throw std::runtime_error(
			    "The native audio source could not be created or configured.");
		}

		XAUDIO2_BUFFER buffer{};
		buffer.Flags = XAUDIO2_END_OF_STREAM;
		buffer.AudioBytes = static_cast<UINT32>(voice->data->samples.size());
		buffer.pAudioData = voice->data->samples.data();
		buffer.LoopCount = settings.loop ? XAUDIO2_LOOP_INFINITE : 0;

		if (FAILED(voice->source->SubmitSourceBuffer(&buffer)) ||
		    (!settings.startPaused && FAILED(voice->source->Start()))) {
			throw std::runtime_error(
			    "The native audio source could not start playback.");
		}

		if (index == impl_->slots.size())
			impl_->slots.emplace_back();
		auto &slot = impl_->slots[index];
		slot.voice = std::move(voice);
		AudioPlaybackHandle handle;
		handle.system_ = impl_->id;
		handle.index_ = index;
		handle.generation_ = slot.generation;
		return handle;
	} catch (const std::exception &exception) {
		error = exception.what();
		return {};
	}
}

AudioPlaybackState AudioSystem::GetState(AudioPlaybackHandle handle) const {
	const auto *voice = impl_->Find(handle);
	if (!voice)
		return AudioPlaybackState::STOPPED;
	XAUDIO2_VOICE_STATE state{};
	voice->source->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
	return state.BuffersQueued == 0 ? AudioPlaybackState::STOPPED
	       : voice->paused          ? AudioPlaybackState::PAUSED
	                                : AudioPlaybackState::PLAYING;
}

bool AudioSystem::Pause(AudioPlaybackHandle handle) {
	auto *voice = impl_->Find(handle);
	if (!voice || GetState(handle) == AudioPlaybackState::STOPPED)
		return false;
	if (voice->paused)
		return true;
	if (FAILED(voice->source->Stop()))
		return false;
	voice->paused = true;
	return true;
}

bool AudioSystem::Resume(AudioPlaybackHandle handle) {
	auto *voice = impl_->Find(handle);
	if (!voice || GetState(handle) == AudioPlaybackState::STOPPED)
		return false;
	if (!voice->paused)
		return true;
	if (FAILED(voice->source->Start()))
		return false;
	voice->paused = false;
	return true;
}

bool AudioSystem::Stop(AudioPlaybackHandle handle) {
	if (!impl_->Find(handle))
		return false;
	impl_->Release(impl_->slots[handle.index_]);
	return true;
}

void AudioSystem::StopAll() {
	for (auto &slot : impl_->slots)
		impl_->Release(slot);
}

bool AudioSystem::SetVolume(AudioPlaybackHandle handle, float volume) {
	auto *voice = impl_->Find(handle);
	if (!voice || !ValidVolume(volume) ||
	    GetState(handle) == AudioPlaybackState::STOPPED ||
	    FAILED(voice->source->SetVolume(volume)))
		return false;
	voice->settings.volume = volume;
	return true;
}

bool AudioSystem::SetMasterVolume(float volume) {
	if (!impl_->engine || !ValidVolume(volume))
		return false;
	try {
		impl_->engine->SetMasterVolume(volume);
		impl_->masterVolume = volume;
		return true;
	} catch (const std::exception &) {
		return false;
	}
}

bool AudioSystem::SetSpatialState(const AudioListenerPose &listener,
                                  std::span<const AudioEmitterChange> changes) {
	if (!listener.IsValid() || changes.size() > kMaximumVoices)
		return false;
	std::array<Impl::Voice *, kMaximumVoices> voices{};

	for (std::size_t i = 0; i < changes.size(); ++i) {
		voices[i] = impl_->Find(changes[i].handle);
		if (!voices[i] || !voices[i]->settings.spatial)
			return false;
		for (std::size_t j = 0; j < i; ++j)
			if (voices[j] == voices[i])
				return false;
	}

	for (const auto &slot : impl_->slots) {
		if (!slot.voice || !slot.voice->settings.spatial)
			continue;
		const AudioEmitterSettings *emitter = &slot.voice->settings.emitter;
		for (std::size_t i = 0; i < changes.size(); ++i)
			if (voices[i] == slot.voice.get())
				emitter = &changes[i].emitter;
		if (!SpatialAudio::CanCalculate(listener, *emitter))
			return false;
	}

	impl_->listener = listener;
	for (std::size_t i = 0; i < changes.size(); ++i)
		voices[i]->settings.emitter = changes[i].emitter;
	return true;
}

bool AudioSystem::SetSuspended(bool suspended) {
	if (!impl_->engine)
		return false;
	if (impl_->suspended == suspended)
		return true;
	try {
		if (suspended)
			impl_->engine->Suspend();
		else
			impl_->engine->Resume();
		impl_->suspended = suspended;
		return true;
	} catch (const std::exception &) {
		return false;
	}
}

std::uint64_t AudioSystem::GetDeviceRevision() const {
	return impl_->deviceRevision;
}
