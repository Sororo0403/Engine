#include "scene/AudioSourceComponent.h"

#include "platform/windows/PathUtils.h"
#include <string>

bool AudioSourceComponent::IsValid() const {
	AudioPlaybackSettings settings;
	settings.volume = volume;
	AudioEmitterSettings emitter;
	emitter.minDistance = minDistance;
	emitter.maxDistance = maxDistance;
	return settings.IsValid() && emitter.IsValid() &&
	       (clipPath.empty() ||
	        (PathUtils::HasExtension(clipPath, ".wav") &&
	         clipPath.native().find(L'\0') == std::wstring::npos &&
	         (!clipPath.has_root_path() || clipPath.is_absolute())));
}
