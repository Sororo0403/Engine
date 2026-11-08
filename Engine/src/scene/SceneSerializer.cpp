#include "scene/SceneSerializer.h"
#include "LegacyMaterial.h"
#include "SceneRigidBodySerializer.h"
#include "SceneUISerializer.h"

#include "core/Logger.h"
#include "math/MathUtils.h"
#include "math/TransformUtils.h"
#include "physics/ColliderUtils.h"
#include "platform/windows/FileUtils.h"
#include "platform/windows/PathUtils.h"
#include "scene/Scene.h"
#include "scene/SceneHierarchy.h"
#include "scene/SceneObjectReferences.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <json.hpp>

#include <exception>
#include <format>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
using Json = nlohmann::json;

/// <summary>
/// 衝突設定に使う32ビット符号なし整数を、切り捨てや桁あふれなしで読み込む
/// </summary>
/// <param name="value">0からUINT32_MAXまでのJSON整数</param>
/// <returns>読み込んだ整数。型や範囲が無効な場合は例外</returns>
std::uint32_t ReadCollisionValue(const Json &value) {
	if (!value.is_number_unsigned() ||
	    value.get<std::uint64_t>() >
	        (std::numeric_limits<std::uint32_t>::max)()) {
		throw std::runtime_error(
		    "衝突設定には32ビット符号なし整数が必要です。");
	}
	return value.get<std::uint32_t>();
}

/// <summary>
/// 3成分の値をJSON配列に変換する
/// </summary>
/// <param name="value">変換する値</param>
/// <returns>x・y・zの順に並べたJSON配列</returns>
Json ToJson(const DirectX::XMFLOAT3 &value) {
	return Json::array({value.x, value.y, value.z});
}

/// <summary>
/// クォータニオンをJSON配列に変換する
/// </summary>
/// <param name="value">変換する回転</param>
/// <returns>x・y・z・wの順に並べたJSON配列</returns>
Json ToJson(const DirectX::XMFLOAT4 &value) {
	return Json::array({value.x, value.y, value.z, value.w});
}

/// <summary>
/// JSON配列からUV用の有限な2成分を読み込む
/// </summary>
/// <param name="value">数値を2つ持つJSON配列</param>
/// <returns>UVの横・縦の順に並べた値</returns>
std::array<float, 2> ReadFloat2(const Json &value) {
	if (!value.is_array() || value.size() != 2 || !value.at(0).is_number() ||
	    !value.at(1).is_number()) {
		throw std::runtime_error("UVには数値2成分の配列が必要です。");
	}

	const std::array<float, 2> result{value.at(0).get<float>(),
	                                  value.at(1).get<float>()};

	if (!std::isfinite(result[0]) || !std::isfinite(result[1])) {
		throw std::runtime_error("UVの値がfloatの有効範囲を超えています。");
	}

	return result;
}

/// <summary>
/// JSON配列から3成分の値を読み込む
/// </summary>
/// <param name="value">数値を3つ持つJSON配列</param>
/// <returns>読み込んだ値</returns>
DirectX::XMFLOAT3 ReadFloat3(const Json &value) {
	if (!value.is_array() || value.size() != 3) {
		throw std::runtime_error("Transformの値には要素数3の配列が必要です。");
	}

	for (const auto &component : value) {
		if (!component.is_number()) {
			throw std::runtime_error(
			    "Transformの配列に数値以外が含まれています。");
		}
	}

	const DirectX::XMFLOAT3 result{
	    value.at(0).get<float>(),
	    value.at(1).get<float>(),
	    value.at(2).get<float>(),
	};

	if (!MathUtils::IsFinite(result)) {
		throw std::runtime_error(
		    "Transformの値がfloatの有効範囲を超えています。");
	}

	return result;
}

/// <summary>
/// JSON配列から回転クォータニオンを読み込んで正規化する
/// </summary>
/// <param name="value">数値を4つ持つJSON配列</param>
/// <returns>読み込んだ単位クォータニオン</returns>
DirectX::XMFLOAT4 ReadRotation(const Json &value) {
	if (!value.is_array() || value.size() != 4) {
		throw std::runtime_error("回転には要素数4の配列が必要です。");
	}

	for (const auto &component : value) {
		if (!component.is_number()) {
			throw std::runtime_error("回転の配列に数値以外が含まれています。");
		}
	}

	const DirectX::XMFLOAT4 rotation{
	    value.at(0).get<float>(),
	    value.at(1).get<float>(),
	    value.at(2).get<float>(),
	    value.at(3).get<float>(),
	};

	DirectX::XMFLOAT4 normalized;

	if (!TransformUtils::TryNormalizeRotation(rotation, normalized)) {
		throw std::runtime_error("回転クォータニオンが無効です。");
	}

	return normalized;
}

/// <summary>
/// 画像スロットの配列を読み込み、旧材質参照を画像へ移行して検証する
/// </summary>
/// <param name="bindings">スロット番号と参照パスの配列</param>
/// <param name="version">シーン形式。10以前は旧材質JSONから画像だけを取得する</param>
/// <returns>検証済みの画像割り当て。無効な場合は例外</returns>
TextureBindings ReadBindings(const Json &bindings, int version) {
	if (!bindings.is_array()) {
		throw std::runtime_error("画像の割り当て配列が無効です。");
	}

	TextureBindings result;
	std::set<std::uint32_t> seen;
	for (const auto &binding : bindings) {
		const auto &slot = binding.at("slot");
		if (!slot.is_number_unsigned() ||
		    slot.get<std::uint64_t>() > UINT32_MAX ||
		    !seen.insert(slot.get<std::uint32_t>()).second) {
			throw std::runtime_error(
			    "画像スロットの番号が無効、または重複しています。");
		}

		auto path = PathUtils::FromUtf8(binding.at("path").get<std::string>());
		if (version <= 10) {
			if (path.empty() ||
			    path.native().find(L'\0') != std::wstring::npos ||
			    !PathUtils::HasExtension(path, ".json") ||
			    !PathUtils::HasExtension(path.stem(), ".material")) {
				throw std::runtime_error("旧材質の参照先が無効です。");
			}

			LegacyMaterial legacy;
			std::string error;
			if (!LegacyMaterialReader::Load(path, legacy, error)) {
				throw std::runtime_error("旧材質の画像参照を移行できません: " +
				                         error);
			}

			if (legacy.baseColorTexture.empty()) {
				continue;
			}

			path = (path.parent_path() / legacy.baseColorTexture)
			           .lexically_normal();
		}

		result.slots.emplace_back(slot.get<std::uint32_t>(), std::move(path));
	}

	std::sort(result.slots.begin(), result.slots.end(),
	          [](const auto &a, const auto &b) { return a.first < b.first; });
	if (!result.IsValid()) {
		throw std::runtime_error("画像テクスチャの参照先が無効です。");
	}

	return result;
}

/// <summary>
/// 基準シーンの検証では、通常の読み込み成功メッセージを出さずに同じ検証を使う
/// </summary>
/// <param name="scene">成功時だけ置き換えるシーン</param>
/// <param name="document">読み込むJSON</param>
/// <param name="logSuccess">成功メッセージを記録するか</param>
/// <returns>true:成功 false:形式や設定が無効</returns>
bool ApplySceneDocument(Scene &scene, const Json &document,
                        bool logSuccess = true);

/// <summary>
/// Prefabの根・元ID・保存済み対応表と、再帰を含まない基準シーンを検証する
/// </summary>
/// <param name="scene">保存または読み込むシーン</param>
void ValidatePrefabLinks(const Scene &scene) {
	std::map<SceneObjectId, std::set<SceneObjectId>> members;
	for (const auto &[id, object] : scene.GetObjects()) {
		if (!object.prefabLink) {
			continue;
		}
		const auto &link = *object.prefabLink;
		if (!link.IsValid()) {
			throw std::runtime_error("Prefab link data is invalid.");
		}
		if (!members[link.root].insert(link.source).second) {
			throw std::runtime_error(
			    "Prefab source IDs must be unique within an instance.");
		}
		const auto *owner = scene.FindObject(link.root);
		if (!owner || !owner->prefabLink ||
		    owner->prefabLink->root != link.root ||
		    owner->prefabLink->asset.empty() ||
		    !owner->prefabLink->ContainsSource(link.source) ||
		    owner->prefabLink->GetInstance(link.source) != id) {
			throw std::runtime_error(
			    "Prefab member does not match its owning root.");
		}
		if (id != link.root) {
			if (!link.asset.empty() || !link.baseline.empty() ||
			    !link.objects.empty()) {
				throw std::runtime_error(
				    "Only a Prefab root may store its baseline.");
			}
			continue;
		}
		if (!PathUtils::HasExtension(link.asset, ".json") ||
		    !PathUtils::HasExtension(link.asset.stem(), ".prefab") ||
		    (link.asset.has_root_path() && !link.asset.is_absolute())) {
			throw std::runtime_error("Prefab asset path is invalid.");
		}
		const auto baseline = Json::parse(link.baseline);
		for (const auto &value : baseline.at("objects")) {
			if (value.contains("prefabLink")) {
				throw std::runtime_error(
				    "Prefab baselines cannot contain nested links.");
			}
		}
		Scene original;
		if (!ApplySceneDocument(original, baseline, false) ||
		    !original.Contains(link.source) ||
		    original.FindObject(link.source)->parentId != 0 ||
		    original.GetObjects().size() != link.objects.size()) {
			throw std::runtime_error(
			    "Prefab baseline or source map is invalid.");
		}
		std::vector<SceneObjectId> ids;
		if (!SceneHierarchy::GetSubtree(original, link.source, ids) ||
		    ids.size() != original.GetObjects().size()) {
			throw std::runtime_error("Prefab baseline must be one hierarchy.");
		}
		for (const auto &[source, value] : original.GetObjects()) {
			if (!link.ContainsSource(source) ||
			    SceneObjectReferences::HasExternal(value, ids)) {
				throw std::runtime_error(
				    "Prefab baseline contains an external reference.");
			}
		}
	}
}

} // namespace

bool SceneSerializer::Serialize(const Scene &scene, std::string &text) {
	try {
		SceneHierarchy::WorldTransforms transforms;
		if (!SceneHierarchy::BuildWorldTransforms(scene, transforms)) {
			throw std::runtime_error("親子関係または合成後の変形が無効です。");
		}
		ValidatePrefabLinks(scene);
		Json objects = Json::array();

		for (const auto &[id, object] : scene.GetObjects()) {
			if (!TransformUtils::IsValid(object.transform)) {
				throw std::runtime_error("Transformが無効です。");
			}

			if ((object.mesh && !object.mesh->IsValid()) ||
			    (object.meshRenderer && !object.meshRenderer->IsValid())) {
				throw std::runtime_error(
				    "MeshまたはMesh Rendererの設定が無効です。");
			}

			if (object.colorTint && !object.colorTint->IsValid()) {
				throw std::runtime_error("色の設定が無効です。");
			}

			if (object.collider) {
				AxisAlignedBox bounds;

				if (!ColliderUtils::GetWorldBounds(
				        *object.collider, transforms.at(id).matrix, bounds)) {
					throw std::runtime_error("衝突用ボックスが無効です。");
				}
			}

			if ((object.camera && !object.camera->IsValid()) ||
			    (object.character && !object.character->IsValid()) ||
			    (object.followCamera && !object.followCamera->IsValid()) ||
			    (object.respawn && !object.respawn->IsValid()) ||
			    !SceneObject::IsValidTag(object.tag) ||
			    (object.triggerActions && !object.triggerActions->IsValid()) ||
			    (object.pointLight && !object.pointLight->IsValid()) ||
			    (object.spotLight && !object.spotLight->IsValid()) ||
			    (object.motionPath && !object.motionPath->IsValid()) ||
			    (object.transformAnimation &&
			     !object.transformAnimation->IsValid()) ||
			    (object.script && !object.script->IsValid()) ||
			    (object.animator && !object.animator->IsValid()) ||
			    (object.particleEmitter &&
			     !object.particleEmitter->IsValid()) ||
			    (object.audioSource && !object.audioSource->IsValid())) {
				throw std::runtime_error(
				    "ゲーム用コンポーネントの設定が無効です。");
			}

			objects.push_back({
			    {"id", id},
			    {"parent", object.parentId},
			    {"active", object.active},
			    {"name", object.name},
			    {"tag", object.tag},
			    {"transform",
			     {
			         {"translation", ToJson(object.transform.translation)},
			         {"rotation", ToJson(object.transform.rotation)},
			         {"scale", ToJson(object.transform.scale)},
			     }},
			});

			if (object.mesh) {
				objects.back()["mesh"] = {
				    {"model", PathUtils::ToUtf8(object.mesh->modelPath)}};
			}

			if (object.meshRenderer) {
				auto bindings = Json::array();

				for (const auto &[slot, path] :
				     object.meshRenderer->textures.slots) {
					bindings.push_back(
					    {{"slot", slot}, {"path", PathUtils::ToUtf8(path)}});
				}

				objects.back()["meshRenderer"] = {
				    {"visible", object.meshRenderer->visible},
				    {"textures", std::move(bindings)},
				    {"uvScale", object.meshRenderer->uvScale},
				    {"uvOffset", object.meshRenderer->uvOffset}};
			}

			if (object.prefabLink) {
				const auto &link = *object.prefabLink;
				Json mappings = Json::array();
				for (const auto &[source, instance] : link.objects) {
					mappings.push_back(
					    {{"source", source}, {"instance", instance}});
				}
				objects.back()["prefabLink"] = {
				    {"root", link.root},
				    {"source", link.source},
				    {"asset", PathUtils::ToUtf8(link.asset)},
				    {"baseline", link.baseline},
				    {"objects", std::move(mappings)}};
			}

			if (object.audioSource) {
				const auto &v = *object.audioSource;
				objects.back()["audioSource"] = {
				    {"clip", PathUtils::ToUtf8(v.clipPath)},
				    {"volume", v.volume},
				    {"loop", v.loop},
				    {"playOnStart", v.playOnStart},
				    {"spatial", v.spatial},
				    {"minDistance", v.minDistance},
				    {"maxDistance", v.maxDistance}};
			}

			if (object.audioListener)
				objects.back()["audioListener"] = Json::object();

			if (object.pointLight) {
				const auto &light = *object.pointLight;
				objects.back()["pointLight"] = {{"color", light.color},
				                                {"intensity", light.intensity},
				                                {"range", light.range}};
			}

			SceneUISerializer::Write(objects.back(), object);
			SceneRigidBodySerializer::Write(objects.back(), object);
			if (object.script) {
				const auto &script = *object.script;
				Json references = Json::array();
				for (const auto &[name, target] : script.references)
					references.push_back({{"name", name}, {"target", target}});
				Json prefabs = Json::array();
				for (const auto &[name, path] : script.prefabs)
					prefabs.push_back(
					    {{"name", name}, {"path", PathUtils::ToUtf8(path)}});
				Json scenes = Json::array();
				for (const auto &[name, path] : script.scenes)
					scenes.push_back(
					    {{"name", name}, {"path", PathUtils::ToUtf8(path)}});
				objects.back()["script"] = {
				    {"asset", PathUtils::ToUtf8(script.asset)},
				    {"enabled", script.enabled},
				    {"state", Json::parse(script.initialState)},
				    {"references", std::move(references)},
				    {"prefabs", std::move(prefabs)},
				    {"scenes", std::move(scenes)}};
			}

			if (object.animator) {
				const auto &a = *object.animator;
				objects.back()["animator"] = {{"clip", a.clip},
				                              {"speed", a.speed},
				                              {"loop", a.loop},
				                              {"playOnStart", a.playOnStart}};
			}
			if (object.particleEmitter) {
				const auto &p = *object.particleEmitter;
				objects.back()["particleEmitter"] = {
				    {"playOnStart", p.playOnStart},
				    {"loop", p.loop},
				    {"localSpace", p.localSpace},
				    {"additive", p.additive},
				    {"texture", PathUtils::ToUtf8(p.texture)},
				    {"seed", p.seed},
				    {"maxParticles", p.maxParticles},
				    {"burst", p.burst},
				    {"duration", p.duration},
				    {"rate", p.rate},
				    {"lifetime", {p.lifetimeMin, p.lifetimeMax}},
				    {"speed", {p.speedMin, p.speedMax}},
				    {"radius", p.radius},
				    {"coneDegrees", p.coneDegrees},
				    {"size", {p.sizeStart, p.sizeEnd}},
				    {"rotationSpeed", p.rotationSpeed},
				    {"gravity", ToJson(p.gravity)},
				    {"colorStart", p.colorStart},
				    {"colorEnd", p.colorEnd}};
			}
			if (object.transformAnimation) {
				const auto &a = *object.transformAnimation;
				auto keys = Json::array();
				for (const auto &key : a.keys)
					keys.push_back(
					    {{"seconds", key.seconds},
					     {"translation", ToJson(key.offset.translation)},
					     {"rotation", ToJson(key.offset.rotation)},
					     {"scale", ToJson(key.offset.scale)}});
				objects.back()["transformAnimation"] = {
				    {"keys", std::move(keys)},
				    {"mode", a.mode == TransformAnimationComponent::Mode::ONCE
				                 ? "once"
				             : a.mode == TransformAnimationComponent::Mode::LOOP
				                 ? "loop"
				                 : "pingPong"},
				    {"interpolation",
				     a.interpolation ==
				             TransformAnimationComponent::Interpolation::LINEAR
				         ? "linear"
				     : a.interpolation ==
				             TransformAnimationComponent::Interpolation::SMOOTH
				         ? "smooth"
				         : "step"},
				    {"speed", a.speed},
				    {"playOnStart", a.playOnStart}};
			}
			if (object.motionPath) {
				const auto &path = *object.motionPath;
				objects.back()["motionPath"] = {
				    {"points", path.points},
				    {"segmentSeconds", path.segmentSeconds},
				    {"waypointWaitSeconds", path.waypointWaitSeconds},
				    {"mode", path.mode == MotionPathComponent::Mode::ONCE
				                 ? "once"
				             : path.mode == MotionPathComponent::Mode::LOOP
				                 ? "loop"
				                 : "pingPong"},
				    {"smooth", path.smooth},
				    {"playOnStart", path.playOnStart}};
			}

			if (object.spotLight) {
				const auto &light = *object.spotLight;
				objects.back()["spotLight"] = {
				    {"color", light.color},
				    {"intensity", light.intensity},
				    {"range", light.range},
				    {"innerAngle", light.innerAngle},
				    {"outerAngle", light.outerAngle}};
			}

			if (object.colorTint) {
				objects.back()["colorTint"] = {
				    {"color", ToJson(object.colorTint->color)}};
			}

			if (object.collider) {
				objects.back()["collider"] = {
				    {"center", ToJson(object.collider->center)},
				    {"shape",
				     object.collider->shape == Collider::Shape::BOX ? "Box"
				     : object.collider->shape == Collider::Shape::SPHERE
				         ? "Sphere"
				         : "Capsule"},
				    {"radius", object.collider->radius},
				    {"halfHeight", object.collider->halfHeight},
				    {"halfExtents", ToJson(object.collider->halfExtents)},
				    {"isTrigger", object.collider->isTrigger},
				    {"filter",
				     {{"layer", object.collider->filter.layer},
				      {"mask", object.collider->filter.mask}}}};
			}

			if (object.camera) {
				const auto &v = *object.camera;
				objects.back()["camera"] = {
				    {"primary", v.primary},
				    {"verticalFovDegrees", v.settings.verticalFovDegrees},
				    {"nearClip", v.settings.nearClip},
				    {"farClip", v.settings.farClip}};
			}

			if (object.character) {
				const auto &v = *object.character;
				objects.back()["character"] = {
				    {"moveSpeed", v.moveSpeed},
				    {"jumpSpeed", v.jumpSpeed},
				    {"gravity", v.gravity},
				    {"terminalSpeed", v.terminalSpeed},
				    {"coyoteTime", v.coyoteTime},
				    {"jumpBufferTime", v.jumpBufferTime},
				    {"groundSnapDistance", v.groundSnapDistance},
				    {"stepHeight", v.stepHeight}};
			}

			if (object.followCamera) {
				const auto &v = *object.followCamera;
				objects.back()["followCamera"] = {
				    {"target", v.target},
				    {"offset", ToJson(v.offset)},
				    {"lookOffset", ToJson(v.lookOffset)},
				    {"smoothing", v.smoothing},
				    {"avoidObstacles", v.avoidObstacles},
				    {"clearance", v.clearance},
				    {"orbit",
				     {{"enabled", v.orbit.enabled},
				      {"mouseSensitivity", v.orbit.mouseSensitivity},
				      {"stickSpeed", v.orbit.stickSpeed},
				      {"zoomSensitivity", v.orbit.zoomSensitivity},
				      {"minimumPitch", v.orbit.minimumPitch},
				      {"maximumPitch", v.orbit.maximumPitch},
				      {"minimumDistance", v.orbit.minimumDistance},
				      {"maximumDistance", v.orbit.maximumDistance}}}};
			}

			if (object.triggerActions) {
				Json actions = Json::array();
				for (const auto &action : object.triggerActions->actions) {
					const auto operation =
					    action.operation == TriggerAction::Operation::ENABLE
					        ? "enable"
					    : action.operation == TriggerAction::Operation::DISABLE
					        ? "disable"
					    : action.operation == TriggerAction::Operation::TOGGLE
					        ? "toggle"
					    : action.operation ==
					            TriggerAction::Operation::PLAY_AUDIO
					        ? "playAudio"
					    : action.operation ==
					            TriggerAction::Operation::STOP_AUDIO
					        ? "stopAudio"
					    : action.operation == TriggerAction::Operation::TELEPORT
					        ? "teleport"
					    : action.operation ==
					            TriggerAction::Operation::SET_CHECKPOINT
					        ? "setCheckpoint"
					    : action.operation ==
					            TriggerAction::Operation::SET_CAMERA
					        ? "setCamera"
					    : action.operation ==
					            TriggerAction::Operation::PLAY_MOTION_PATH
					        ? "playMotionPath"
					    : action.operation ==
					            TriggerAction::Operation::PAUSE_MOTION_PATH
					        ? "pauseMotionPath"
					    : action.operation ==
					            TriggerAction::Operation::REVERSE_MOTION_PATH
					        ? "reverseMotionPath"
					    : action.operation == TriggerAction::Operation::
					                              PLAY_TRANSFORM_ANIMATION
					        ? "playTransformAnimation"
					    : action.operation == TriggerAction::Operation::
					                              RESTART_TRANSFORM_ANIMATION
					        ? "restartTransformAnimation"
					    : action.operation == TriggerAction::Operation::
					                              PAUSE_TRANSFORM_ANIMATION
					        ? "pauseTransformAnimation"
					    : action.operation == TriggerAction::Operation::
					                              STOP_TRANSFORM_ANIMATION
					        ? "stopTransformAnimation"
					    : action.operation ==
					            TriggerAction::Operation::EMIT_PARTICLES
					        ? "emitParticles"
					    : action.operation ==
					            TriggerAction::Operation::PLAY_ANIMATOR
					        ? "playAnimator"
					    : action.operation ==
					            TriggerAction::Operation::RESTART_ANIMATOR
					        ? "restartAnimator"
					    : action.operation ==
					            TriggerAction::Operation::PAUSE_ANIMATOR
					        ? "pauseAnimator"
					    : action.operation ==
					            TriggerAction::Operation::STOP_ANIMATOR
					        ? "stopAnimator"
					    : action.operation ==
					            TriggerAction::Operation::SEND_SCRIPT_MESSAGE
					        ? "sendScriptMessage"
					    : action.operation ==
					            TriggerAction::Operation::QUIT_GAME
					        ? "quitGame"
					    : action.operation ==
					            TriggerAction::Operation::RESTART_SCENE
					        ? "restartScene"
					    : action.operation ==
					            TriggerAction::Operation::PAUSE_GAME
					        ? "pauseGame"
					    : action.operation ==
					            TriggerAction::Operation::RESUME_GAME
					        ? "resumeGame"
					    : action.operation ==
					            TriggerAction::Operation::TOGGLE_PAUSE
					        ? "togglePause"
					    : action.operation ==
					            TriggerAction::Operation::LAUNCH_CHARACTER
					        ? "launchCharacter"
					        : "loadScene";
					actions.push_back(
					    {{"event",
					      action.event == TriggerAction::Event::ENTER  ? "enter"
					      : action.event == TriggerAction::Event::EXIT ? "exit"
					      : action.event == TriggerAction::Event::FIRST_ENTER
					          ? "firstEnter"
					      : action.event == TriggerAction::Event::LAST_EXIT
					          ? "lastExit"
					      : action.event == TriggerAction::Event::BUTTON_CLICK
					          ? "buttonClick"
					      : action.event == TriggerAction::Event::INPUT_PRESSED
					          ? "inputPressed"
					          : "interact"},
					     {"target", action.target},
					     {"operation", operation},
					     {"once", action.once},
					     {"destination", action.destination},
					     {"scene", PathUtils::ToUtf8(action.scenePath)},
					     {"contact",
					      action.contact == TriggerAction::Contact::CHARACTER
					          ? "character"
					      : action.contact ==
					              TriggerAction::Contact::DYNAMIC_BODY
					          ? "dynamicBody"
					          : "any"},
					     {"contactTag", action.contactTag},
					     {"inputAction", action.inputAction},
					     {"messageName", action.messageName},
					     {"delaySeconds", action.delaySeconds},
					     {"cameraBlendSeconds", action.cameraBlendSeconds},
					     {"arrivalTag", action.arrivalTag},
					     {"launchSpeed", action.launchSpeed}});
				}
				objects.back()["triggerActions"] = {
				    {"actions", std::move(actions)}};
			}

			if (object.respawn) {
				const auto &v = *object.respawn;
				objects.back()["respawn"] = {
				    {"minimumY", v.minimumY},
				    {"useStartPosition", v.useStartPosition},
				    {"position", ToJson(v.position)}};
			}
		}

		const Json document{
		    {"format", "EngineScene"},
		    {"version", SceneSerializer::kCurrentVersion},
		    {"objects", std::move(objects)},
		};

		// 文字列への変換に成功してから、呼び出し側の出力を置き換える
		std::string serialized = document.dump(2);
		serialized += '\n';
		text = std::move(serialized);

		return true;
	} catch (const std::exception &exception) {
		Logger::Error(
		    std::format("SceneSerializer::Serialize: {}", exception.what()));
		return false;
	}
}

bool SceneSerializer::Save(const Scene &scene,
                           const std::filesystem::path &filePath) {
	try {
		std::string text;

		if (!Serialize(scene, text)) {
			return false;
		}

		const auto targetPath = PathUtils::ResolveFromExecutable(filePath);

		std::filesystem::create_directories(targetPath.parent_path());

		std::string error;

		if (!FileUtils::WriteTextAtomically(targetPath, text, error)) {
			Logger::Error(std::format("SceneSerializer::Save: {}", error));
			return false;
		}

		Logger::Info("シーンを保存しました。");
		return true;
	} catch (const std::exception &exception) {
		Logger::Error(
		    std::format("SceneSerializer::Save: {}", exception.what()));
		return false;
	}
}

namespace {
/// <summary>
/// 解析したシーンドキュメントを検証し、全件成功した場合だけ反映する
/// </summary>
/// <param name="scene">読み込み先</param>
/// <param name="document">version 1から22のJSON</param>
/// <returns>true:成功 false:内容が無効</returns>
bool ApplySceneDocument(Scene &scene, const Json &document, bool logSuccess) {
	try {
		if (!document.is_object() || document.at("format") != "EngineScene" ||
		    !document.at("version").is_number_integer() ||
		    (document.at("version") < 1 ||
		     document.at("version") > SceneSerializer::kCurrentVersion)) {
			throw std::runtime_error("未対応のシーンファイル形式です。");
		}

		const int version = document.at("version").get<int>();

		const Json &items = document.at("objects");

		if (!items.is_array()) {
			throw std::runtime_error("objectsには配列が必要です。");
		}

		std::map<SceneObjectId, SceneObject> objects;

		for (const auto &item : items) {
			const Json &idValue = item.at("id");

			if (!idValue.is_number_unsigned()) {
				throw std::runtime_error(
				    "物体のIDには符号なし整数が必要です。");
			}

			const SceneObjectId id = idValue.get<SceneObjectId>();

			SceneObject object;
			if (version >= 17) {
				object.active = item.at("active").get<bool>();
			}
			if (version >= 14) {
				const auto &parent = item.at("parent");
				if (!parent.is_number_unsigned()) {
					throw std::runtime_error("親IDには0以上の整数が必要です。");
				}
				object.parentId = parent.get<SceneObjectId>();
			}

			object.name = item.at("name").get<std::string>();
			if (version >= 42)
				object.tag = item.at("tag").get<std::string>();
			if (!SceneObject::IsValidTag(object.tag))
				throw std::runtime_error("物体のタグが無効です。");

			if (object.name.empty()) {
				object.name = "Object";
			}

			if (version >= 9) {
				if (item.contains("mesh")) {
					object.mesh = MeshComponent{PathUtils::FromUtf8(
					    item.at("mesh").at("model").get<std::string>())};
				}

				if (item.contains("meshRenderer")) {
					const auto &renderer = item.at("meshRenderer");
					object.meshRenderer = MeshRendererComponent{
					    renderer.at("visible").get<bool>(),
					    ReadBindings(renderer.at(version >= 11 ? "textures"
					                                           : "materials"),
					                 version)};

					if (version >= 13) {
						object.meshRenderer->uvScale =
						    ReadFloat2(renderer.at("uvScale"));
						object.meshRenderer->uvOffset =
						    ReadFloat2(renderer.at("uvOffset"));
					}
				}
			} else {
				const auto path =
				    PathUtils::FromUtf8(item.at("model").get<std::string>());
				const bool visible =
				    version < 3 || item.at("visible").get<bool>();
				const auto bindings =
				    version >= 8 && item.contains("materials")
				        ? ReadBindings(item.at("materials"), version)
				        : TextureBindings{};

				if (!path.empty()) {
					object.mesh = MeshComponent{path};
				}

				// 形状がなくても、旧形式の非表示や材質の割り当てを失わない。
				if (object.mesh || !visible || !bindings.slots.empty()) {
					object.meshRenderer =
					    MeshRendererComponent{visible, bindings};
				}
			}

			if ((object.mesh && !object.mesh->IsValid()) ||
			    (object.meshRenderer && !object.meshRenderer->IsValid())) {
				throw std::runtime_error(
				    "MeshまたはMesh Rendererの設定が無効です。");
			}

			if (version >= 20 && item.contains("audioSource")) {
				const auto &v = item.at("audioSource");
				for (const auto *key : {"volume", "minDistance", "maxDistance"})
					if (!v.at(key).is_number())
						throw std::runtime_error(
						    "Audio Sourceの音量と距離には数値が必要です。");
				for (const auto *key : {"loop", "playOnStart", "spatial"})
					if (!v.at(key).is_boolean())
						throw std::runtime_error(
						    "Audio Sourceの切り替えには真偽値が必要です。");
				object.audioSource = AudioSourceComponent{
				    PathUtils::FromUtf8(v.at("clip").get<std::string>()),
				    v.at("volume").get<float>(),
				    v.at("loop").get<bool>(),
				    v.at("playOnStart").get<bool>(),
				    v.at("spatial").get<bool>(),
				    v.at("minDistance").get<float>(),
				    v.at("maxDistance").get<float>()};
			}

			if (version >= 20 && item.contains("audioListener")) {
				if (!item.at("audioListener").is_object())
					throw std::runtime_error(
					    "Audio Listenerの設定にはオブジェクトが必要です。");
				object.audioListener.emplace();
			}

			if (version >= 19 && item.contains("pointLight")) {
				const auto &light = item.at("pointLight");
				if (!light.at("intensity").is_number() ||
				    !light.at("range").is_number()) {
					throw std::runtime_error(
					    "Point Lightの強さと範囲には数値が必要です。");
				}
				const auto color = ReadFloat3(light.at("color"));
				object.pointLight =
				    PointLightComponent{{color.x, color.y, color.z},
				                        light.at("intensity").get<float>(),
				                        light.at("range").get<float>()};
			}

			SceneUISerializer::Read(item, object, version);
			SceneRigidBodySerializer::Read(item, object, version);
			if (version >= 29 && item.contains("script")) {
				const auto &value = item.at("script");
				if (!value.at("asset").is_string() ||
				    !value.at("enabled").is_boolean() ||
				    !value.at("state").is_object() ||
				    !value.at("references").is_array() ||
				    value.at("references").size() >
				        ScriptComponent::kMaximumReferences)
					throw std::runtime_error("Lua Script fields are invalid.");
				ScriptComponent script;
				script.asset =
				    PathUtils::FromUtf8(value.at("asset").get<std::string>());
				script.enabled = value.at("enabled").get<bool>();
				script.initialState = value.at("state").dump();
				for (const auto &reference : value.at("references")) {
					if (!reference.at("name").is_string() ||
					    !reference.at("target").is_number_unsigned())
						throw std::runtime_error(
						    "Script object reference is invalid.");
					script.references.emplace_back(
					    reference.at("name").get<std::string>(),
					    reference.at("target").get<SceneObjectId>());
				}
				if (version >= 33) {
					const auto &prefabs = value.at("prefabs");
					if (!prefabs.is_array() ||
					    prefabs.size() > ScriptComponent::kMaximumPrefabs)
						throw std::runtime_error(
						    "Script Prefab fields are invalid.");
					for (const auto &entry : prefabs) {
						if (!entry.at("name").is_string() ||
						    !entry.at("path").is_string())
							throw std::runtime_error(
							    "Script Prefab reference is invalid.");
						script.prefabs.emplace_back(
						    entry.at("name").get<std::string>(),
						    PathUtils::FromUtf8(
						        entry.at("path").get<std::string>()));
					}
				}
				if (version >= 34) {
					const auto &scenes = value.at("scenes");
					if (!scenes.is_array() ||
					    scenes.size() > ScriptComponent::kMaximumScenes)
						throw std::runtime_error(
						    "Script Scene fields are invalid.");
					for (const auto &entry : scenes) {
						if (!entry.at("name").is_string() ||
						    !entry.at("path").is_string())
							throw std::runtime_error(
							    "Script Scene reference is invalid.");
						script.scenes.emplace_back(
						    entry.at("name").get<std::string>(),
						    PathUtils::FromUtf8(
						        entry.at("path").get<std::string>()));
					}
				}
				if (!script.IsValid())
					throw std::runtime_error(
					    "Lua Script settings are invalid.");
				object.script = std::move(script);
			}

			if (version >= 37 && item.contains("animator")) {
				const auto &v = item.at("animator");
				if (!v.is_object() || !v.at("clip").is_string() ||
				    !v.at("speed").is_number() || !v.at("loop").is_boolean() ||
				    !v.at("playOnStart").is_boolean())
					throw std::runtime_error(
					    "Animator requires a clip name, numeric speed and "
					    "boolean flags.");
				AnimatorComponent value{
				    v.at("clip").get<std::string>(), v.at("speed").get<float>(),
				    v.at("loop").get<bool>(), v.at("playOnStart").get<bool>()};
				if (!value.IsValid())
					throw std::runtime_error("Animator settings are invalid.");
				object.animator = std::move(value);
			}
			if (version >= 36 && item.contains("particleEmitter")) {
				const auto &v = item.at("particleEmitter");
				ParticleEmitterComponent p;
				for (auto field :
				     {"playOnStart", "loop", "localSpace", "additive"})
					if (!v.at(field).is_boolean())
						throw std::runtime_error(
						    "Particle flags must be boolean.");
				for (auto field : {"maxParticles", "burst", "seed"})
					if (!v.at(field).is_number_integer())
						throw std::runtime_error(
						    "Particle counts and seed must be integers.");
				if (v.at("maxParticles") < 1 || v.at("maxParticles") > 4096 ||
				    v.at("burst") < 0 || v.at("burst") > 4096 ||
				    v.at("seed") < 0 ||
				    v.at("seed") > (std::numeric_limits<std::uint32_t>::max)())
					throw std::runtime_error(
					    "Particle count or seed is out of range.");
				p.playOnStart = v.at("playOnStart").get<bool>();
				p.loop = v.at("loop").get<bool>();
				p.localSpace = v.at("localSpace").get<bool>();
				p.additive = v.at("additive").get<bool>();
				p.texture =
				    PathUtils::FromUtf8(v.at("texture").get<std::string>());
				p.seed = v.at("seed").get<std::uint32_t>();
				p.maxParticles = v.at("maxParticles").get<int>();
				p.burst = v.at("burst").get<int>();
				const auto number = [&](const char *field) {
					if (!v.at(field).is_number())
						throw std::runtime_error(
						    "Particle settings must use numbers.");
					return v.at(field).get<float>();
				};
				p.duration = number("duration");
				p.rate = number("rate");
				p.radius = number("radius");
				p.coneDegrees = number("coneDegrees");
				p.rotationSpeed = number("rotationSpeed");
				p.gravity = ReadFloat3(v.at("gravity"));
				const auto lifetime = ReadFloat2(v.at("lifetime")),
				           speed = ReadFloat2(v.at("speed")),
				           size = ReadFloat2(v.at("size"));
				p.lifetimeMin = lifetime[0];
				p.lifetimeMax = lifetime[1];
				p.speedMin = speed[0];
				p.speedMax = speed[1];
				p.sizeStart = size[0];
				p.sizeEnd = size[1];
				for (auto field : {"colorStart", "colorEnd"}) {
					const auto &color = v.at(field);
					if (!color.is_array() || color.size() != 4)
						throw std::runtime_error(
						    "Particle colors need four channels.");
					for (std::size_t c = 0; c < 4; ++c) {
						if (!color[c].is_number())
							throw std::runtime_error(
							    "Particle colors must use numbers.");
						(std::string_view(field) == "colorStart"
						     ? p.colorStart
						     : p.colorEnd)[c] = color[c].get<float>();
					}
				}
				if (!p.IsValid())
					throw std::runtime_error(
					    "Particle Emitter settings are invalid.");
				object.particleEmitter = std::move(p);
			}
			if (version >= 35 && item.contains("transformAnimation")) {
				const auto &value = item.at("transformAnimation"),
				           &keys = value.at("keys");
				if (!keys.is_array() || keys.size() < 2 ||
				    keys.size() > TransformAnimationComponent::kMaximumKeys ||
				    !value.at("speed").is_number() ||
				    !value.at("playOnStart").is_boolean())
					throw std::runtime_error(
					    "Transform Animation fields are invalid.");
				TransformAnimationComponent animation;
				animation.keys.clear();
				for (const auto &key : keys) {
					if (!key.at("seconds").is_number())
						throw std::runtime_error(
						    "Animation key time must be a number.");
					TransformAnimationKey frame;
					frame.seconds = key.at("seconds").get<float>();
					frame.offset.translation =
					    ReadFloat3(key.at("translation"));
					frame.offset.scale = ReadFloat3(key.at("scale"));
					frame.offset.rotation = ReadRotation(key.at("rotation"));
					animation.keys.push_back(frame);
				}
				const auto mode = value.at("mode").get<std::string>(),
				           interpolation =
				               value.at("interpolation").get<std::string>();
				if (mode == "once")
					animation.mode = TransformAnimationComponent::Mode::ONCE;
				else if (mode == "loop")
					animation.mode = TransformAnimationComponent::Mode::LOOP;
				else if (mode == "pingPong")
					animation.mode =
					    TransformAnimationComponent::Mode::PING_PONG;
				else
					throw std::runtime_error("Animation mode is invalid.");
				if (interpolation == "linear")
					animation.interpolation =
					    TransformAnimationComponent::Interpolation::LINEAR;
				else if (interpolation == "smooth")
					animation.interpolation =
					    TransformAnimationComponent::Interpolation::SMOOTH;
				else if (interpolation == "step")
					animation.interpolation =
					    TransformAnimationComponent::Interpolation::STEP;
				else
					throw std::runtime_error(
					    "Animation interpolation is invalid.");
				animation.speed = value.at("speed").get<float>();
				animation.playOnStart = value.at("playOnStart").get<bool>();
				if (!animation.IsValid())
					throw std::runtime_error(
					    "Transform Animation settings are invalid.");
				object.transformAnimation = std::move(animation);
			}
			if (version >= 27 && item.contains("motionPath")) {
				const auto &value = item.at("motionPath");
				const auto &points = value.at("points");
				if (!points.is_array() || points.size() < 2 ||
				    points.size() > MotionPathComponent::kMaximumPoints ||
				    !value.at("segmentSeconds").is_number() ||
				    !value.at("smooth").is_boolean() ||
				    !value.at("mode").is_string())
					throw std::runtime_error("Motion Path fields are invalid.");
				MotionPathComponent path;
				path.points.clear();
				for (const auto &point : points) {
					const auto p = ReadFloat3(point);
					path.points.push_back({p.x, p.y, p.z});
				}
				path.segmentSeconds = value.at("segmentSeconds").get<float>();
				if (version >= 61) {
					if (!value.at("waypointWaitSeconds").is_number())
						throw std::runtime_error(
						    "Motion Path waypoint wait is invalid.");
					path.waypointWaitSeconds =
					    value.at("waypointWaitSeconds").get<float>();
				}
				path.smooth = value.at("smooth").get<bool>();
				if (version >= 44) {
					if (!value.at("playOnStart").is_boolean())
						throw std::runtime_error(
						    "Motion Path Play on Start is invalid.");
					path.playOnStart = value.at("playOnStart").get<bool>();
				}
				const auto mode = value.at("mode").get<std::string>();
				if (mode == "once")
					path.mode = MotionPathComponent::Mode::ONCE;
				else if (mode == "loop")
					path.mode = MotionPathComponent::Mode::LOOP;
				else if (mode == "pingPong")
					path.mode = MotionPathComponent::Mode::PING_PONG;
				else
					throw std::runtime_error("Motion Path mode is invalid.");
				if (!path.IsValid())
					throw std::runtime_error(
					    "Motion Path settings are invalid.");
				object.motionPath = std::move(path);
			}

			if (version >= 22 && item.contains("spotLight")) {
				const auto &light = item.at("spotLight");
				for (const auto *field :
				     {"intensity", "range", "innerAngle", "outerAngle"}) {
					if (!light.at(field).is_number()) {
						throw std::runtime_error(
						    "Spot Lightの強さ・範囲・角度には数値が必要です。");
					}
				}
				const auto color = ReadFloat3(light.at("color"));
				object.spotLight =
				    SpotLightComponent{{color.x, color.y, color.z},
				                       light.at("intensity").get<float>(),
				                       light.at("range").get<float>(),
				                       light.at("innerAngle").get<float>(),
				                       light.at("outerAngle").get<float>()};
			}

			if (version >= 7 && item.contains("colorTint")) {
				object.colorTint =
				    ColorTint{ReadFloat3(item.at("colorTint").at("color"))};

				if (!object.colorTint->IsValid()) {
					throw std::runtime_error("色の設定が無効です。");
				}
			}

			if (version >= 4 && item.contains("collider")) {
				const auto &collider = item.at("collider");
				object.collider =
				    Collider{ReadFloat3(collider.at("center")),
				             ReadFloat3(collider.at("halfExtents"))};
				if (version >= 38) {
					const auto shape = collider.at("shape").get<std::string>();
					if (shape == "Box")
						object.collider->shape = Collider::Shape::BOX;
					else if (shape == "Sphere")
						object.collider->shape = Collider::Shape::SPHERE;
					else if (shape == "Capsule")
						object.collider->shape = Collider::Shape::CAPSULE;
					else
						throw std::runtime_error("Unknown Collider shape.");
					if (!collider.at("radius").is_number() ||
					    !collider.at("halfHeight").is_number())
						throw std::runtime_error(
						    "Collider dimensions must be numbers.");
					object.collider->radius =
					    collider.at("radius").get<float>();
					object.collider->halfHeight =
					    collider.at("halfHeight").get<float>();
				}
				if (version >= 16) {
					object.collider->isTrigger =
					    collider.at("isTrigger").get<bool>();
				}
				if (version >= 15) {
					const auto &filter = collider.at("filter");
					object.collider->filter = {
					    ReadCollisionValue(filter.at("layer")),
					    ReadCollisionValue(filter.at("mask"))};
				}
			}

			if (version >= 6 && item.contains("camera")) {
				const auto &v = item.at("camera");

				for (const auto *key :
				     {"verticalFovDegrees", "nearClip", "farClip"}) {
					if (!v.at(key).is_number()) {
						throw std::runtime_error(
						    "カメラの描画設定には数値が必要です。");
					}
				}

				object.camera = CameraComponent{
				    v.at("primary").get<bool>(),
				    CameraSettings{v.at("verticalFovDegrees").get<float>(),
				                   v.at("nearClip").get<float>(),
				                   v.at("farClip").get<float>()}};
			}

			if (version >= 5 && item.contains("character")) {
				const auto &v = item.at("character");

				for (const auto *key :
				     {"moveSpeed", "jumpSpeed", "gravity", "terminalSpeed"}) {
					if (!v.at(key).is_number()) {
						throw std::runtime_error(
						    "移動設定には数値が必要です。");
					}
				}

				object.character =
				    CharacterSettings{v.at("moveSpeed").get<float>(),
				                      v.at("jumpSpeed").get<float>(),
				                      v.at("gravity").get<float>(),
				                      v.at("terminalSpeed").get<float>()};

				if (version >= 10) {
					if (!v.at("coyoteTime").is_number() ||
					    !v.at("jumpBufferTime").is_number()) {
						throw std::runtime_error(
						    "ジャンプ猶予には数値が必要です。");
					}

					object.character->coyoteTime =
					    v.at("coyoteTime").get<float>();
					object.character->jumpBufferTime =
					    v.at("jumpBufferTime").get<float>();
				}
				if (version >= 62) {
					if (!v.at("groundSnapDistance").is_number())
						throw std::runtime_error(
						    "Character ground snap distance is invalid.");
					object.character->groundSnapDistance =
					    v.at("groundSnapDistance").get<float>();
				}
				if (version >= 63) {
					if (!v.at("stepHeight").is_number())
						throw std::runtime_error(
						    "Character step height is invalid.");
					object.character->stepHeight =
					    v.at("stepHeight").get<float>();
				}
			}

			if (version >= 5 && item.contains("followCamera")) {
				const auto &v = item.at("followCamera");

				if (!v.at("target").is_number_unsigned() ||
				    !v.at("smoothing").is_number()) {
					throw std::runtime_error("追従カメラの設定が無効です。");
				}

				object.followCamera = FollowCameraSettings{
				    v.at("target").get<SceneObjectId>(),
				    ReadFloat3(v.at("offset")), ReadFloat3(v.at("lookOffset")),
				    v.at("smoothing").get<float>()};
				if (version >= 12) {
					if (!v.at("avoidObstacles").is_boolean() ||
					    !v.at("clearance").is_number()) {
						throw std::runtime_error(
						    "追従カメラの障害物回避設定が無効です。");
					}
					object.followCamera->avoidObstacles =
					    v.at("avoidObstacles").get<bool>();
					object.followCamera->clearance =
					    v.at("clearance").get<float>();
				}
				if (version >= 24) {
					const auto &orbit = v.at("orbit");
					if (!orbit.at("enabled").is_boolean()) {
						throw std::runtime_error(
						    "Orbit Cameraの有効設定が無効です。");
					}
					for (const auto *field :
					     {"mouseSensitivity", "stickSpeed", "zoomSensitivity",
					      "minimumPitch", "maximumPitch", "minimumDistance",
					      "maximumDistance"}) {
						if (!orbit.at(field).is_number()) {
							throw std::runtime_error(
							    "Orbit Cameraの設定には数値が必要です。");
						}
					}
					object.followCamera->orbit = {
					    orbit.at("enabled").get<bool>(),
					    orbit.at("mouseSensitivity").get<float>(),
					    orbit.at("stickSpeed").get<float>(),
					    orbit.at("zoomSensitivity").get<float>(),
					    orbit.at("minimumPitch").get<float>(),
					    orbit.at("maximumPitch").get<float>(),
					    orbit.at("minimumDistance").get<float>(),
					    orbit.at("maximumDistance").get<float>()};
				}
			}

			if (version <= 5 && object.followCamera) {
				object.camera = CameraComponent{};
			}

			if (version >= 26 && item.contains("prefabLink")) {
				const auto &value = item.at("prefabLink");
				if (!value.at("root").is_number_unsigned() ||
				    !value.at("source").is_number_unsigned() ||
				    !value.at("asset").is_string() ||
				    !value.at("baseline").is_string() ||
				    !value.at("objects").is_array()) {
					throw std::runtime_error("Prefab link fields are invalid.");
				}
				PrefabLinkComponent link;
				link.root = value.at("root").get<SceneObjectId>();
				link.source = value.at("source").get<SceneObjectId>();
				link.asset =
				    PathUtils::FromUtf8(value.at("asset").get<std::string>());
				link.baseline = value.at("baseline").get<std::string>();
				for (const auto &entry : value.at("objects")) {
					if (!entry.at("source").is_number_unsigned() ||
					    !entry.at("instance").is_number_unsigned() ||
					    link.ContainsSource(
					        entry.at("source").get<SceneObjectId>())) {
						throw std::runtime_error(
						    "Prefab source map is invalid.");
					}
					link.objects.emplace_back(
					    entry.at("source").get<SceneObjectId>(),
					    entry.at("instance").get<SceneObjectId>());
				}
				std::ranges::sort(link.objects);

				object.prefabLink = std::move(link);
			}

			if (version >= 18 && item.contains("triggerActions")) {
				const auto &actions = item.at("triggerActions").at("actions");
				if (!actions.is_array() ||
				    actions.size() > TriggerActionsComponent::kMaximumActions) {
					throw std::runtime_error(
					    "Trigger Actionsの操作配列が無効です。");
				}
				TriggerActionsComponent component;
				for (const auto &value : actions) {
					const auto event = value.at("event").get<std::string>();
					const auto operation =
					    value.at("operation").get<std::string>();
					if ((event != "enter" && event != "exit" &&
					     !(version >= 25 && event == "interact") &&
					     !(version >= 46 &&
					       (event == "firstEnter" || event == "lastExit")) &&
					     !(version >= 56 && event == "buttonClick") &&
					     !(version >= 59 && event == "inputPressed")) ||
					    (operation != "enable" && operation != "disable" &&
					     operation != "toggle" &&
					     !(version >= 21 && (operation == "playAudio" ||
					                         operation == "stopAudio")) &&
					     !(version >= 23 && operation == "teleport") &&
					     !(version >= 28 && operation == "loadScene") &&
					     !(version >= 57 && operation == "quitGame") &&
					     !(version >= 64 && operation == "launchCharacter") &&
					     !(version >= 58 && operation == "restartScene") &&
					     !(version >= 60 && (operation == "pauseGame" ||
					                         operation == "resumeGame" ||
					                         operation == "togglePause")) &&
					     !(version >= 39 && operation == "setCheckpoint") &&
					     !(version >= 40 && operation == "setCamera") &&
					     !(version >= 44 && (operation == "playMotionPath" ||
					                         operation == "pauseMotionPath")) &&
					     !(version >= 45 && operation == "reverseMotionPath") &&
					     !(version >= 47 &&
					       (operation == "playTransformAnimation" ||
					        operation == "restartTransformAnimation" ||
					        operation == "pauseTransformAnimation" ||
					        operation == "stopTransformAnimation")) &&
					     !(version >= 48 && operation == "emitParticles") &&
					     !(version >= 50 && (operation == "playAnimator" ||
					                         operation == "restartAnimator" ||
					                         operation == "pauseAnimator" ||
					                         operation == "stopAnimator")) &&
					     !(version >= 51 &&
					       operation == "sendScriptMessage")) ||
					    (version >= 23 &&
					     !value.at("destination").is_number_unsigned()) ||
					    !value.at("target").is_number_unsigned() ||
					    !value.at("once").is_boolean() ||
					    (version >= 28 && !value.at("scene").is_string()) ||
					    (version >= 52 &&
					     !value.at("delaySeconds").is_number()) ||
					    (version >= 53 &&
					     !value.at("cameraBlendSeconds").is_number())) {
						throw std::runtime_error(
						    "Trigger Actionsの操作設定が無効です。");
					}
					const auto contact =
					    version >= 41 ? value.at("contact").get<std::string>()
					                  : "any";
					if (contact != "any" && contact != "character" &&
					    contact != "dynamicBody")
						throw std::runtime_error(
						    "Trigger Actionsの接触条件が無効です。");
					component.actions.push_back(
					    {event == "enter"  ? TriggerAction::Event::ENTER
					     : event == "exit" ? TriggerAction::Event::EXIT
					     : event == "firstEnter"
					         ? TriggerAction::Event::FIRST_ENTER
					     : event == "lastExit" ? TriggerAction::Event::LAST_EXIT
					     : event == "buttonClick"
					         ? TriggerAction::Event::BUTTON_CLICK
					     : event == "inputPressed"
					         ? TriggerAction::Event::INPUT_PRESSED
					         : TriggerAction::Event::INTERACT,
					     value.at("target").get<SceneObjectId>(),
					     operation == "enable"
					         ? TriggerAction::Operation::ENABLE
					     : operation == "disable"
					         ? TriggerAction::Operation::DISABLE
					     : operation == "toggle"
					         ? TriggerAction::Operation::TOGGLE
					     : operation == "playAudio"
					         ? TriggerAction::Operation::PLAY_AUDIO
					     : operation == "stopAudio"
					         ? TriggerAction::Operation::STOP_AUDIO
					     : operation == "teleport"
					         ? TriggerAction::Operation::TELEPORT
					     : operation == "setCheckpoint"
					         ? TriggerAction::Operation::SET_CHECKPOINT
					     : operation == "setCamera"
					         ? TriggerAction::Operation::SET_CAMERA
					     : operation == "playMotionPath"
					         ? TriggerAction::Operation::PLAY_MOTION_PATH
					     : operation == "pauseMotionPath"
					         ? TriggerAction::Operation::PAUSE_MOTION_PATH
					     : operation == "reverseMotionPath"
					         ? TriggerAction::Operation::REVERSE_MOTION_PATH
					     : operation == "playTransformAnimation"
					         ? TriggerAction::Operation::
					               PLAY_TRANSFORM_ANIMATION
					     : operation == "restartTransformAnimation"
					         ? TriggerAction::Operation::
					               RESTART_TRANSFORM_ANIMATION
					     : operation == "pauseTransformAnimation"
					         ? TriggerAction::Operation::
					               PAUSE_TRANSFORM_ANIMATION
					     : operation == "stopTransformAnimation"
					         ? TriggerAction::Operation::
					               STOP_TRANSFORM_ANIMATION
					     : operation == "emitParticles"
					         ? TriggerAction::Operation::EMIT_PARTICLES
					     : operation == "playAnimator"
					         ? TriggerAction::Operation::PLAY_ANIMATOR
					     : operation == "restartAnimator"
					         ? TriggerAction::Operation::RESTART_ANIMATOR
					     : operation == "pauseAnimator"
					         ? TriggerAction::Operation::PAUSE_ANIMATOR
					     : operation == "stopAnimator"
					         ? TriggerAction::Operation::STOP_ANIMATOR
					     : operation == "sendScriptMessage"
					         ? TriggerAction::Operation::SEND_SCRIPT_MESSAGE
					     : operation == "quitGame"
					         ? TriggerAction::Operation::QUIT_GAME
					     : operation == "restartScene"
					         ? TriggerAction::Operation::RESTART_SCENE
					     : operation == "pauseGame"
					         ? TriggerAction::Operation::PAUSE_GAME
					     : operation == "resumeGame"
					         ? TriggerAction::Operation::RESUME_GAME
					     : operation == "togglePause"
					         ? TriggerAction::Operation::TOGGLE_PAUSE
					     : operation == "launchCharacter"
					         ? TriggerAction::Operation::LAUNCH_CHARACTER
					         : TriggerAction::Operation::LOAD_SCENE,
					     value.at("once").get<bool>(),
					     version >= 23
					         ? value.at("destination").get<SceneObjectId>()
					         : 0,
					     version >= 28
					         ? PathUtils::FromUtf8(
					               value.at("scene").get<std::string>())
					         : std::filesystem::path{},
					     contact == "character"
					         ? TriggerAction::Contact::CHARACTER
					     : contact == "dynamicBody"
					         ? TriggerAction::Contact::DYNAMIC_BODY
					         : TriggerAction::Contact::ANY,
					     version >= 43
					         ? value.at("contactTag").get<std::string>()
					         : std::string{},
					     version >= 49
					         ? value.at("inputAction").get<std::string>()
					         : std::string{},
					     version >= 51
					         ? value.at("messageName").get<std::string>()
					         : std::string{},
					     version >= 52 ? value.at("delaySeconds").get<float>()
					                   : 0.f,
					     version >= 53
					         ? value.at("cameraBlendSeconds").get<float>()
					         : 0.f,
					     version >= 54
					         ? value.at("arrivalTag").get<std::string>()
					         : std::string{},
					     version >= 64 ? value.at("launchSpeed").get<float>()
					                   : TriggerAction{}.launchSpeed});
				}
				object.triggerActions = std::move(component);
			}

			if (version >= 5 && item.contains("respawn")) {
				const auto &v = item.at("respawn");

				if (!v.at("minimumY").is_number()) {
					throw std::runtime_error("落下高度には数値が必要です。");
				}

				object.respawn =
				    RespawnSettings{v.at("minimumY").get<float>(),
				                    v.at("useStartPosition").get<bool>(),
				                    ReadFloat3(v.at("position"))};
			}

			if ((object.camera && !object.camera->IsValid()) ||
			    (object.character && !object.character->IsValid()) ||
			    (object.followCamera && !object.followCamera->IsValid()) ||
			    (object.respawn && !object.respawn->IsValid()) ||
			    (object.triggerActions && !object.triggerActions->IsValid()) ||
			    (object.pointLight && !object.pointLight->IsValid()) ||
			    (object.spotLight && !object.spotLight->IsValid()) ||
			    (object.motionPath && !object.motionPath->IsValid()) ||
			    (object.transformAnimation &&
			     !object.transformAnimation->IsValid()) ||
			    (object.script && !object.script->IsValid()) ||
			    (object.animator && !object.animator->IsValid()) ||
			    (object.particleEmitter &&
			     !object.particleEmitter->IsValid()) ||
			    (object.audioSource && !object.audioSource->IsValid())) {
				throw std::runtime_error(
				    "ゲーム用コンポーネントの設定が無効です。");
			}

			const Json &transform = item.at("transform");

			object.transform.translation =
			    ReadFloat3(transform.at("translation"));
			// 旧形式のオイラー角は、従来と同じ回転順序で変換する
			object.transform.rotation =
			    version == 1 ? TransformUtils::RotationFromEuler(
			                       ReadFloat3(transform.at("rotation")))
			                 : ReadRotation(transform.at("rotation"));
			object.transform.scale = ReadFloat3(transform.at("scale"));

			if (!TransformUtils::IsValid(object.transform)) {
				throw std::runtime_error("Transformが無効です。");
			}

			if (object.collider && !ColliderUtils::IsValid(*object.collider)) {
				throw std::runtime_error("衝突用ボックスが無効です。");
			}

			const bool inserted = objects.emplace(id, std::move(object)).second;

			if (!inserted) {
				throw std::runtime_error("物体のIDが重複しています。");
			}
		}

		// 親のID順に依存せず全件を読み込み、循環・不在の親・合成後の形状を検証する。
		Scene candidate;
		candidate.ReplaceObjects(std::move(objects));
		SceneHierarchy::WorldTransforms transforms;
		if (!SceneHierarchy::BuildWorldTransforms(candidate, transforms)) {
			throw std::runtime_error("親子関係または合成後の変形が無効です。");
		}
		for (const auto &[id, object] : candidate.GetObjects()) {
			AxisAlignedBox bounds;
			if (object.collider &&
			    !ColliderUtils::GetWorldBounds(
			        *object.collider, transforms.at(id).matrix, bounds)) {
				throw std::runtime_error(
				    "親子合成後の衝突用ボックスが無効です。");
			}
		}

		ValidatePrefabLinks(candidate);

		// 全データの読み込みに成功してから、現在のシーンへ反映する。
		scene.ReplaceObjects(candidate.GetObjects());

		if (logSuccess) {
			Logger::Info("シーンを読み込みました。");
		}
		return true;
	} catch (const std::exception &exception) {
		Logger::Error(std::format("SceneSerializer: {}", exception.what()));
		return false;
	}
}

} // namespace

bool SceneSerializer::Deserialize(Scene &scene, std::string_view text) {
	try {
		return ApplySceneDocument(scene, Json::parse(text));
	} catch (const std::exception &error) {
		Logger::Error(
		    std::format("SceneSerializer::Deserialize: {}", error.what()));
		return false;
	}
}

bool SceneSerializer::Load(Scene &scene,
                           const std::filesystem::path &filePath) {
	try {
		std::ifstream input(PathUtils::ResolveFromExecutable(filePath),
		                    std::ios::binary);

		if (!input) {
			throw std::runtime_error("シーンファイルを開けませんでした。");
		}

		input.exceptions(std::ios::badbit);
		return ApplySceneDocument(scene, Json::parse(input));
	} catch (const std::exception &error) {
		Logger::Error(std::format("SceneSerializer::Load: {}", error.what()));
		return false;
	}
}
