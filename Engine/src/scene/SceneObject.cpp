#include "scene/SceneObject.h"
#include "math/TransformUtils.h"

bool SceneObject::operator==(const SceneObject &other) const {
	return name == other.name && active == other.active &&
	       parentId == other.parentId &&
	       TransformUtils::AreEqual(transform, other.transform) &&
	       mesh == other.mesh && meshRenderer == other.meshRenderer &&
	       colorTint == other.colorTint && collider == other.collider &&
	       character == other.character && camera == other.camera &&
	       followCamera == other.followCamera && respawn == other.respawn &&
	       triggerActions == other.triggerActions &&
	       pointLight == other.pointLight && spotLight == other.spotLight &&
	       audioSource == other.audioSource &&
	       audioListener == other.audioListener &&
	       prefabLink == other.prefabLink && motionPath == other.motionPath &&
	       script == other.script && canvas == other.canvas &&
	       rectTransform == other.rectTransform && text == other.text &&
	       image == other.image && button == other.button;
}
