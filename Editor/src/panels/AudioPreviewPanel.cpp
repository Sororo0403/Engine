#include "panels/AudioPreviewPanel.h"

#include "graphics/ui/ImGuiSystem.h"
#include "platform/windows/PathUtils.h"
#include <format>
#include <imgui.h>

AudioPreviewPanel::AudioPreviewPanel(AudioClipManager &clips,
                                     AudioSystem &audio)
    : preview_(clips, audio) {
}

void AudioPreviewPanel::Open(const std::filesystem::path &path) {
	if (path.empty()) {
		Clear();
		return;
	}

	auto selected = path;
	auto label = PathUtils::ToUtf8(path);

	if (path_ != path) {
		preview_.Clear();
	}

	path_.swap(selected);
	label_.swap(label);
	error_.clear();
	open_ = focusRequested_ = reloadRequested_ = true;
}

void AudioPreviewPanel::Draw(const ImGuiSystem &ui) {
	if (!open_) {
		return;
	}

	if (focusRequested_) {
		ImGui::SetNextWindowCollapsed(false);
		ImGui::SetNextWindowFocus();
		focusRequested_ = false;
	}

	ImGui::SetNextWindowSize({460, 360}, ImGuiCond_FirstUseEver);
	const bool visible = ImGui::Begin("Audio Preview", &open_);

	if (!open_) {
		Clear();
		ImGui::End();
		return;
	}

	if (!visible) {
		ImGui::End();
		return;
	}

	ui.Text(label_);

	if (ui.Button("Reload Audio")) {
		reloadRequested_ = true;
	}

	ui.SameLine();

	if (ui.Button("Close Audio Preview")) {
		Clear();
		ImGui::End();
		return;
	}

	if (reloadRequested_) {
		reloadRequested_ = false;
		preview_.Load(path_, error_);
	}

	const auto &clip = preview_.GetClip();

	if (clip) {
		ui.Text(std::format("{} | {} Hz | {:.3f} s",
		                    clip.GetChannels() == 1 ? "Mono" : "Stereo",
		                    clip.GetSampleRate(), clip.GetDurationSeconds()));
	}

	auto state = preview_.GetState();
	ui.BeginDisabled(!clip);
	ui.BeginDisabled(state != AudioPlaybackState::STOPPED);

	if (ui.Button("Play Preview")) {
		preview_.Play(error_);
	}

	bool loop = preview_.GetLoop();

	if (ImGui::Checkbox("Loop Preview", &loop)) {
		preview_.SetLoop(loop);
	}

	ui.EndDisabled();
	ui.BeginDisabled(state == AudioPlaybackState::STOPPED);

	if (ui.Button(state == AudioPlaybackState::PAUSED ? "Resume Preview"
	                                                  : "Pause Preview")) {
		const bool success = state == AudioPlaybackState::PAUSED
		                         ? preview_.Resume()
		                         : preview_.Pause();
		error_ = success ? "" : "Could not change preview playback state.";
	}

	ui.SameLine();

	if (ui.Button("Stop Preview")) {
		Stop();
	}

	ui.SameLine();

	if (ui.Button("Restart Preview")) {
		preview_.Play(error_);
	}

	ui.EndDisabled();
	float volume = preview_.GetVolume();

	if (ImGui::DragFloat("Preview Volume", &volume, .01f, 0,
	                     AudioPlaybackSettings::kMaximumVolume, "%.2f",
	                     ImGuiSliderFlags_AlwaysClamp)) {
		if (!preview_.SetVolume(volume)) {
			error_ = "Could not update preview volume.";
		}
	}

	ui.EndDisabled();
	state = preview_.GetState();
	ui.Text(std::format("Preview State: {}",
	                    state == AudioPlaybackState::PLAYING  ? "Playing"
	                    : state == AudioPlaybackState::PAUSED ? "Paused"
	                                                          : "Stopped"));
	ui.Text(std::format("Position: {:.3f} / {:.3f} s",
	                    preview_.GetPositionSeconds(),
	                    clip.GetDurationSeconds()));

	if (!error_.empty()) {
		ImGui::TextWrapped("%s", error_.c_str());
	}

	ImGui::End();
}

void AudioPreviewPanel::Stop() {
	preview_.Stop();
	error_.clear();
}

void AudioPreviewPanel::Clear() {
	preview_.Clear();
	path_.clear();
	label_.clear();
	error_.clear();
	open_ = focusRequested_ = reloadRequested_ = false;
}
