#include "scene/SceneSerializer.h"
#include "LegacyMaterial.h"

#include "core/Logger.h"
#include "math/MathUtils.h"
#include "math/TransformUtils.h"
#include "physics/BoxColliderUtils.h"
#include "platform/windows/FileUtils.h"
#include "platform/windows/PathUtils.h"
#include "scene/Scene.h"
#include "scene/SceneHierarchy.h"

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
} // namespace

bool SceneSerializer::Serialize(const Scene &scene, std::string &text) {
	try {
		SceneHierarchy::WorldTransforms transforms;
		if (!SceneHierarchy::BuildWorldTransforms(scene, transforms)) {
			throw std::runtime_error("親子関係または合成後の変形が無効です。");
		}
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

				if (!BoxColliderUtils::GetWorldBounds(
				        *object.collider, transforms.at(id).matrix, bounds)) {
					throw std::runtime_error("衝突用ボックスが無効です。");
				}
			}

			if ((object.camera && !object.camera->IsValid()) ||
			    (object.character && !object.character->IsValid()) ||
			    (object.followCamera && !object.followCamera->IsValid()) ||
			    (object.respawn && !object.respawn->IsValid()) ||
			    (object.triggerActions && !object.triggerActions->IsValid())) {
				throw std::runtime_error(
				    "ゲーム用コンポーネントの設定が無効です。");
			}

			objects.push_back({
			    {"id", id},
			    {"parent", object.parentId},
			    {"active", object.active},
			    {"name", object.name},
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

			if (object.colorTint) {
				objects.back()["colorTint"] = {
				    {"color", ToJson(object.colorTint->color)}};
			}

			if (object.collider) {
				objects.back()["collider"] = {
				    {"center", ToJson(object.collider->center)},
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
				    {"jumpBufferTime", v.jumpBufferTime}};
			}

			if (object.followCamera) {
				const auto &v = *object.followCamera;
				objects.back()["followCamera"] = {
				    {"target", v.target},
				    {"offset", ToJson(v.offset)},
				    {"lookOffset", ToJson(v.lookOffset)},
				    {"smoothing", v.smoothing},
				    {"avoidObstacles", v.avoidObstacles},
				    {"clearance", v.clearance}};
			}

			if (object.triggerActions) {
				Json actions = Json::array();
				for (const auto &action : object.triggerActions->actions) {
					const auto operation =
					    action.operation == TriggerAction::Operation::ENABLE
					        ? "enable"
					    : action.operation == TriggerAction::Operation::DISABLE
					        ? "disable"
					        : "toggle";
					actions.push_back(
					    {{"event", action.event == TriggerEvent::Type::ENTER
					                   ? "enter"
					                   : "exit"},
					     {"target", action.target},
					     {"operation", operation},
					     {"once", action.once}});
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
/// <param name="document">version 1から18のJSON</param>
/// <returns>true:成功 false:内容が無効</returns>
bool ApplySceneDocument(Scene &scene, const Json &document) {
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
				    BoxCollider{ReadFloat3(collider.at("center")),
				                ReadFloat3(collider.at("halfExtents"))};
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
			}

			if (version <= 5 && object.followCamera) {
				object.camera = CameraComponent{};
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
					if ((event != "enter" && event != "exit") ||
					    (operation != "enable" && operation != "disable" &&
					     operation != "toggle") ||
					    !value.at("target").is_number_unsigned() ||
					    !value.at("once").is_boolean()) {
						throw std::runtime_error(
						    "Trigger Actionsの操作設定が無効です。");
					}
					component.actions.push_back(
					    {event == "enter" ? TriggerEvent::Type::ENTER
					                      : TriggerEvent::Type::EXIT,
					     value.at("target").get<SceneObjectId>(),
					     operation == "enable"
					         ? TriggerAction::Operation::ENABLE
					     : operation == "disable"
					         ? TriggerAction::Operation::DISABLE
					         : TriggerAction::Operation::TOGGLE,
					     value.at("once").get<bool>()});
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
			    (object.triggerActions && !object.triggerActions->IsValid())) {
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

			if (object.collider &&
			    !BoxColliderUtils::IsValid(*object.collider)) {
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
			    !BoxColliderUtils::GetWorldBounds(
			        *object.collider, transforms.at(id).matrix, bounds)) {
				throw std::runtime_error(
				    "親子合成後の衝突用ボックスが無効です。");
			}
		}

		// 全データの読み込みに成功してから、現在のシーンへ反映する。
		scene.ReplaceObjects(candidate.GetObjects());

		Logger::Info("シーンを読み込みました。");
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
