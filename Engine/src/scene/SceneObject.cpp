#include "scene/SceneObject.h"
#include "math/TransformUtils.h"
#include "scene/ObjectTag.h"

bool SceneObject::IsValidTag(std::string_view tag) {
	return ObjectTag::IsValid(tag);
}

bool SceneObject::operator==(const SceneObject &other) const {
	return meshRenderer == other.meshRenderer && colorTint == other.colorTint &&
	       HasSameSimulationSettings(other);
}

bool SceneObject::HasSameSimulationSettings(const SceneObject &other) const {
	return name == other.name && tag == other.tag && active == other.active &&
	       parentId == other.parentId &&
	       TransformUtils::AreEqual(transform, other.transform) &&
	       mesh == other.mesh && collider == other.collider &&
	       rigidBody == other.rigidBody && character == other.character &&
	       camera == other.camera && followCamera == other.followCamera &&
	       respawn == other.respawn && triggerActions == other.triggerActions &&
	       pointLight == other.pointLight && spotLight == other.spotLight &&
	       directionalLight == other.directionalLight && animationClip == other.animationClip &&
	       audioSource == other.audioSource &&
	       audioListener == other.audioListener &&
	       prefabLink == other.prefabLink && motionPath == other.motionPath &&
	       transformAnimation == other.transformAnimation &&
	       animator == other.animator &&
	       particleEmitter == other.particleEmitter && script == other.script &&
	       canvas == other.canvas && rectTransform == other.rectTransform &&
	       text == other.text && image == other.image && button == other.button;
}

bool SceneObject::CanAnimate(const AnimationClipTrack &track) const {
	if (track.IsTransform()) {
		return !transformAnimation && !motionPath && !character && !followCamera &&
		       (!rigidBody || !rigidBody->enabled || rigidBody->isKinematic);
	}

	return rectTransform &&
	       (track.property != AnimationClipTrack::Property::OPACITY || text || image);
}
