#include "audio/AudioInspector.h"

#include "assets/AssetDatabase.h"
#include "assets/AssetDragDrop.h"
#include "audio/WaveReader.h"
#include "graphics/ui/ImGuiSystem.h"
#include "history/EditorHistory.h"
#include "platform/windows/PathUtils.h"
#include "scene/SceneObject.h"
#include <algorithm>
#include <cmath>
#include <format>
#include <imgui.h>
#include <limits>

namespace {
/// <summary>
/// 音声の参照と、空間配置時のモノラル制約を割り当て前に確認する
/// </summary>
/// <param name="source">変更後の音源</param>
/// <param name="error">失敗理由。成功時は空</param>
/// <returns>true:割り当て可能 false:読み込み失敗または空間音声に未対応</returns>
bool ValidateClip(const AudioSourceComponent &source, std::string &error) {
	error.clear();
	if (source.clipPath.empty())
		return true;
	WaveData wave;
	if (!WaveReader::Load(source.clipPath, wave, error))
		return false;
	if (source.spatial && wave.channels != 1) {
		error = "Spatial audio requires a mono WAV clip.";
		return false;
	}

	return true;
}
} // namespace

bool AudioInspector::Draw(const ImGuiSystem &ui, SceneObjectId id,
                          SceneObject &object, EditorHistory &history,
                          const AssetDatabase *assets) {
	if (objectId_ != id) {
		Clear();
		objectId_ = id;
	}

	reveal_.clear();
	const auto scope = std::to_string(id);
	ImGui::PushID(scope.c_str());
	ImGui::PushID("AudioInspector");
	bool changed = false;

	bool sourceOpen = false;
	if (object.audioSource) {
		sourceOpen = ui.CollapsingHeader("Audio Source");
		ImGui::SetItemTooltip("Right-click for audio source actions.");
		if (ImGui::BeginPopupContextItem("AudioSourceActions")) {
			const bool reset = ui.MenuItem("Reset Audio Source");
			const bool remove = ui.MenuItem("Remove Audio Source");
			ImGui::EndPopup();
			if (reset || remove) {
				error_.clear();
				changed |= history.SetAudioSource(
				    id, reset ? std::optional{AudioSourceComponent{}} : std::nullopt);
				sourceOpen = false;
			}
		}
	}
	if (sourceOpen) {
		auto edited = *object.audioSource;
		const auto assign = [&](const std::filesystem::path &path) {
			auto value = *object.audioSource;
			value.clipPath = path;
			if (ValidateClip(value, error_))
				changed |= history.SetAudioSource(id, std::move(value));
		};
		const auto label =
		    std::format("{}###AudioClip",
		                edited.clipPath.empty()
		                    ? "None"
		                    : PathUtils::ToUtf8(edited.clipPath.filename()));
		if (ImGui::Button(label.c_str(),
		                  {ImGui::GetContentRegionAvail().x, 0}) &&
		    !edited.clipPath.empty())
			reveal_ = edited.clipPath;
		if (const auto dropped =
		        AssetDragDrop::AcceptTarget(AssetDragDrop::Kind::AUDIO);
		    !dropped.empty())
			assign(dropped);
		if (ImGui::BeginCombo(
		        "Audio Clip",
		        edited.clipPath.empty()
		            ? "None"
		            : PathUtils::ToUtf8(edited.clipPath.filename()).c_str())) {
			if (ImGui::Selectable("None", edited.clipPath.empty()))
				assign({});
			if (assets) {
				for (const auto &path : assets->GetAudioClips()) {
					const auto text = PathUtils::ToUtf8(
					    path.lexically_relative(assets->GetRootDirectory()));
					if (ImGui::Selectable(text.c_str(),
					                      path == edited.clipPath))
						assign(path);
				}
			}

			ImGui::EndCombo();
		}
		edited = *object.audioSource;
		const auto continuous = [&](bool valueChanged) {
			const bool begin =
			    !ImGui::IsItemActivated() || history.BeginAudioSource(id);
			if (begin && valueChanged && edited.IsValid() &&
			    edited != *object.audioSource &&
			    (history.IsEditing() || history.BeginAudioSource(id))) {
				object.audioSource = edited;
				changed = true;
			}
			if (ImGui::IsItemDeactivated() && !ImGui::IsAnyItemActive())
				history.CommitEdit();
		};
		continuous(ImGui::DragFloat("Volume", &edited.volume, .01f, 0,
		                            AudioPlaybackSettings::kMaximumVolume,
		                            "%.2f", ImGuiSliderFlags_AlwaysClamp));
		if (ImGui::Checkbox("Loop", &edited.loop))
			changed |= history.SetAudioSource(id, edited);
		if (ImGui::Checkbox("Play on Start", &edited.playOnStart))
			changed |= history.SetAudioSource(id, edited);
		if (ImGui::Checkbox("Spatial", &edited.spatial)) {
			if (!edited.spatial || ValidateClip(edited, error_))
				changed |= history.SetAudioSource(id, edited);
			else
				edited = *object.audioSource;
		}

		ui.BeginDisabled(!edited.spatial);
		continuous(ImGui::DragFloat("Min Distance", &edited.minDistance, .05f,
		                            0, std::nextafter(edited.maxDistance, 0.f),
		                            "%.3f", ImGuiSliderFlags_AlwaysClamp));
		continuous(ImGui::DragFloat(
		    "Max Distance", &edited.maxDistance, .1f,
		    (std::max)(.001f, std::nextafter(
		                          edited.minDistance,
		                          (std::numeric_limits<float>::infinity)())),
		    AudioEmitterSettings::kMaximumDistance, "%.3f",
		    ImGuiSliderFlags_AlwaysClamp));
		ui.EndDisabled();
		if (!error_.empty())
			ImGui::TextWrapped("%s", error_.c_str());
		if (ImGui::Button("Clear Audio Clip"))
			assign({});
	}

	bool listenerOpen = false;
	if (object.audioListener) {
		listenerOpen = ui.CollapsingHeader("Audio Listener");
		ImGui::SetItemTooltip("Right-click for audio listener actions.");
		if (ImGui::BeginPopupContextItem("AudioListenerActions")) {
			const bool remove = ui.MenuItem("Remove Audio Listener");
			ImGui::EndPopup();
			if (remove) {
				changed |= history.SetAudioListener(id, std::nullopt);
				listenerOpen = false;
			}
		}
	}
	if (listenerOpen) {
		auto listener = *object.audioListener;
		if (ui.Checkbox("Preferred Listener", listener.preferred))
			changed |= history.SetAudioListener(id, listener);
		ImGui::SetItemTooltip("Use this listener for spatial audio while it is active.");
	}

	ImGui::PopID();
	ImGui::PopID();
	return changed;
}

void AudioInspector::DrawReadOnly(const ImGuiSystem &ui,
                                  const SceneObject &object) const {
	if (object.audioSource && ui.CollapsingHeader("Audio Source")) {
		const auto &v = *object.audioSource;
		ui.Text(PathUtils::ToUtf8(v.clipPath));
		ui.Text(std::format("Volume: {:.3f} | Loop: {} | Play on Start: {}",
		                    v.volume, v.loop ? "Yes" : "No",
		                    v.playOnStart ? "Yes" : "No"));
		ui.Text(std::format("Spatial: {} | Distance: {:.3f} - {:.3f}",
		                    v.spatial ? "Yes" : "No", v.minDistance,
		                    v.maxDistance));
	}

	if (object.audioListener)
		ui.CollapsingHeader("Audio Listener");
}

const std::filesystem::path &AudioInspector::GetRevealRequest() const {
	return reveal_;
}

void AudioInspector::Clear() {
	objectId_ = 0;
	reveal_.clear();
	error_.clear();
}
