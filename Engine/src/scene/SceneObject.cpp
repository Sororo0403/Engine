#include "scene/SceneObject.h"
#include "math/TransformUtils.h"
#include "platform/windows/StringConversion.h"

bool SceneObject::IsValidTag(std::string_view tag) {
	return tag.empty() ||
	       (tag.size() <= 64 && tag.find('\0') == std::string_view::npos &&
	        !StringConversion::Utf8ToWide(tag).empty());
}

bool SceneObject::operator==(const SceneObject &other) const {
	return name == other.name && tag == other.tag && active == other.active &&
	       parentId == other.parentId &&
	       TransformUtils::AreEqual(transform, other.transform) &&
	       mesh == other.mesh && meshRenderer == other.meshRenderer &&
	       colorTint == other.colorTint && collider == other.collider &&
	       rigidBody == other.rigidBody && character == other.character &&
	       camera == other.camera && followCamera == other.followCamera &&
	       respawn == other.respawn && triggerActions == other.triggerActions &&
	       pointLight == other.pointLight && spotLight == other.spotLight &&
	       audioSource == other.audioSource &&
	       audioListener == other.audioListener &&
	       prefabLink == other.prefabLink && motionPath == other.motionPath &&
	       transformAnimation == other.transformAnimation &&
	       animator == other.animator &&
	       particleEmitter == other.particleEmitter && script == other.script &&
	       canvas == other.canvas && rectTransform == other.rectTransform &&
	       text == other.text && image == other.image && button == other.button;
}
