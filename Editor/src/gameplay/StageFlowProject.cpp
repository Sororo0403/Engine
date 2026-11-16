#include "gameplay/StageFlowProject.h"

#include "gameplay/StageFlowBuilder.h"
#include "gameplay/StageFlowPrefab.h"
#include "platform/windows/PathUtils.h"
#include "scene/PrefabSerializer.h"
#include "scene/PrimitiveObjects.h"
#include "scene/Scene.h"
#include "scene/SceneActivity.h"
#include "scene/SceneHierarchy.h"
#include "scene/SceneLighting.h"
#include "scene/SceneSerializer.h"

#include <json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <utility>

namespace StageFlow {
namespace {
using Json = nlohmann::json;
using namespace DirectX;

constexpr std::string_view kSpawn = "stage-spawn";
constexpr std::string_view kWind = "stage-wind";
constexpr std::string_view kDecorationPrefix = "stage-decor:";
constexpr float kCharacterHalfHeight = 0.5f;

const Json &ReadArray(const Json &object, const char *key) {
	const auto &value = object.at(key);
	if (!value.is_array()) {
		throw std::runtime_error(std::string(key) + " must be an array.");
	}
	return value;
}

/// <summary>
/// プロジェクトの素材フォルダ内にある通常ファイルへ参照を解決する
/// </summary>
std::filesystem::path ResolveAsset(const std::filesystem::path &root, const Json &value) {
	const auto relative = PathUtils::FromUtf8(value.get<std::string>());
	if (relative.empty() || relative.has_root_path()) {
		throw std::runtime_error("Stage flow assets need project-relative paths.");
	}
	const auto path = std::filesystem::canonical(root / relative);
	PathUtils::RelativeInside(root / L"assets", path);
	if (!std::filesystem::is_regular_file(path)) {
		throw std::runtime_error("Stage flow asset is not a file: " + PathUtils::ToUtf8(path));
	}
	return path;
}

void AddDecoration(Scene &scene, SceneObjectId markerId, const Decoration &decoration) {
	SceneObject visual;
	visual.name = PathUtils::ToUtf8(decoration.model.stem());
	visual.mesh = MeshComponent{decoration.model};
	visual.meshRenderer.emplace();
	visual.parentId = markerId;
	visual.transform.scale = {decoration.scale, decoration.scale, decoration.scale};
	XMStoreFloat4(&visual.transform.rotation,
	              XMQuaternionRotationRollPitchYaw(0, XMConvertToRadians(decoration.yawDegrees), 0));
	scene.CreateObject(std::move(visual));
}

/// <summary>
/// 区間内に保存された風のローカル速度を、配置後のワールド方向へ変換する
/// </summary>
Json ReadWind(const SceneObject &object, const SceneWorldTransform &world, const std::string &key) {
	if (!object.script || object.script->enabled || !object.script->asset.empty()) {
		throw std::runtime_error("Wind marker '" + object.name + "' needs a disabled Script with state.");
	}
	try {
		const auto state = Json::parse(object.script->initialState);
		const auto &value = state.at("velocity");
		if (!value.is_array() || value.size() != 3) {
			throw std::runtime_error("velocity must use [X, Y, Z].");
		}
		const auto velocity = value.get<std::array<float, 3>>();
		if (!std::ranges::all_of(velocity, [](float component) {
			    return std::isfinite(component) && std::abs(component) <= 16.0f;
		    })) {
			throw std::runtime_error("velocity components must be between -16 and 16.");
		}
		XMFLOAT3 direction;
		XMStoreFloat3(&direction, XMVector3Rotate(
		    XMVectorSet(velocity[0], velocity[1], velocity[2], 0), XMLoadFloat4(&world.rotation)));
		return {{"reference", key}, {"velocity", {direction.x, direction.y, direction.z}},
		        {"airborneOnly", state.value("airborneOnly", false)}};
	} catch (const std::exception &exception) {
		throw std::runtime_error("Wind marker '" + object.name + "': " + exception.what());
	}
}

/// <summary>
/// スタートの目印を基準に試遊用の主人公と追従カメラを作る
/// </summary>
SceneObjectId AddPlayer(Scene &scene, const SceneWorldTransform &spawn, float minimumY) {
	auto player = PrimitiveObjects::CreateBox();
	player.name = "Player";
	player.transform.translation = spawn.GetPosition();
	player.transform.translation.y += kCharacterHalfHeight + 0.02f;
	player.transform.rotation = spawn.rotation;
	player.transform.scale = {0.32f, kCharacterHalfHeight, 0.32f};
	player.colorTint = ColorTint{{0.9f, 0.67f, 0.2f}};
	player.character = CharacterSettings{
	    .moveSpeed = 4.6f, .jumpSpeed = 7.5f, .gravity = 20, .terminalSpeed = 30,
	    .coyoteTime = 0.08f, .jumpBufferTime = 0.12f, .groundSnapDistance = 0.12f,
	    .stepHeight = 0.36f, .airFloatDelay = 0.25f, .airFloatDuration = 0.85f,
	};
	player.respawn = RespawnSettings{minimumY - 8.0f};
	const auto id = scene.CreateObject(std::move(player));

	SceneObject camera;
	camera.name = "Follow Camera";
	camera.camera.emplace();
	camera.followCamera.emplace();
	auto &follow = *camera.followCamera;
	follow.target = id;
	follow.avoidObstacles = true;
	const auto rotation = XMLoadFloat4(&spawn.rotation);
	XMStoreFloat3(&follow.offset, XMVector3Rotate(XMVectorSet(0, 4.2f, -7.5f, 0), rotation));
	XMStoreFloat3(&follow.lookOffset, XMVector3Rotate(XMVectorSet(0, 1, 3, 0), rotation));
	scene.CreateObject(std::move(camera));
	return id;
}
} // namespace

bool LoadProject(const std::filesystem::path &projectRoot, Project &project, std::string &error) {
	try {
		const auto root = std::filesystem::canonical(projectRoot);
		const auto path = root / L"stage-flow.json";
		std::ifstream input(path, std::ios::binary);
		if (!input) {
			throw std::runtime_error("Missing stage-flow.json in this project.");
		}
		const auto json = Json::parse(input);
		if (json.at("format") != "StageFlow" || !json.at("version").is_number_integer() ||
		    json.at("version") != 1) {
			throw std::runtime_error("Expected StageFlow version 1.");
		}
		Project result;
		const auto &graph = json.at("graph");
		result.graph.start = graph.at("start").get<std::string>();
		result.graph.goal = graph.at("goal").get<std::string>();
		for (const auto &node : ReadArray(graph, "nodes")) {
			result.graph.nodes.push_back({node.at("id"), node.at("type")});
		}
		for (const auto &link : ReadArray(graph, "connections")) {
			result.graph.connections.push_back({link.at("from"), link.at("exit"),
			                                    link.at("to"), link.at("entry")});
		}
		for (const auto &prefab : ReadArray(json, "prefabs")) {
			auto asset = ResolveAsset(root, prefab.at("asset"));
			if (!PrefabSerializer::IsPrefabPath(asset)) {
				throw std::runtime_error("Section assets must use .prefab.json.");
			}
			result.prefabs.push_back({prefab.at("id"), prefab.at("type"), std::move(asset)});
		}
		std::set<std::string> markers;
		if (json.contains("decorations")) {
			for (const auto &decoration : ReadArray(json, "decorations")) {
				Decoration entry{decoration.at("marker"), ResolveAsset(root, decoration.at("model")),
				                 decoration.value("scale", 1.0f), decoration.value("yawDegrees", 0.0f)};
				if (entry.marker.empty() || !markers.insert(entry.marker).second ||
				    !std::isfinite(entry.scale) || entry.scale <= 0.0f || !std::isfinite(entry.yawDegrees)) {
					throw std::runtime_error("Decoration markers need unique names and a valid scale and rotation.");
				}
				result.decorations.push_back(std::move(entry));
			}
		}
		if (json.contains("airflowScript")) {
			result.airflowScript = ResolveAsset(root, json.at("airflowScript"));
		}
		std::vector<std::size_t> order;
		if (!GetPlacementOrder(result.graph, order, error)) {
			return false;
		}
		project = std::move(result);
		error.clear();
		return true;
	} catch (const std::exception &exception) {
		error = exception.what();
		return false;
	}
}

bool BuildScene(const Project &project, std::uint32_t seed, Scene &scene,
                std::vector<std::string> &variants, std::string &error) {
	try {
		std::vector<std::unique_ptr<Scene>> prefabs;
		std::vector<Module> modules;
		for (const auto &entry : project.prefabs) {
			auto prefab = std::make_unique<Scene>();
			Module module;
			if (!PrefabSerializer::Load(entry.path, *prefab, error) ||
			    !ReadModule(*prefab, entry.id, entry.sectionType, module, error)) {
				throw std::runtime_error(entry.id + ": " + error);
			}
			prefabs.push_back(std::move(prefab));
			modules.push_back(std::move(module));
		}
		std::vector<Placement> placements;
		if (!Build(project.graph, modules, seed, placements, error)) {
			return false;
		}

		Scene result;
		std::vector<std::string> chosen;
		SceneObjectId startRoot = 0;
		for (const auto &placement : placements) {
			const auto &node = project.graph.nodes[placement.node];
			const auto root = PlaceModule(*prefabs[placement.module], placement.pose, result, error);
			if (!root) {
				throw std::runtime_error(node.id + ": " + error);
			}
			result.FindObject(root)->name = node.id;
			if (node.id == project.graph.start) {
				startRoot = root;
			}
			chosen.push_back(node.id + ": " + modules[placement.module].id);
		}

		SceneHierarchy::WorldTransforms transforms;
		SceneActivity::States activity;
		if (!SceneHierarchy::BuildWorldTransforms(result, transforms) ||
		    !SceneActivity::BuildStates(result, activity)) {
			throw std::runtime_error("Could not read generated object transforms.");
		}
		std::vector<SceneObjectId> startObjects;
		if (!SceneHierarchy::GetSubtree(result, startRoot, startObjects)) {
			throw std::runtime_error("Could not read the starting section.");
		}
		std::optional<SceneWorldTransform> spawn;
		SceneObject airflow;
		airflow.name = "Stage Flow Airflow";
		airflow.script.emplace();
		airflow.script->asset = project.airflowScript;
		Json fields = Json::array();
		float minimumY = 0.0f;
		// 生成した飾りを走査対象に含めないよう、配置済みのIDだけを走査する。
		for (const auto &[id, world] : transforms) {
			if (!activity.at(id)) {
				continue;
			}
			const auto &object = *result.FindObject(id);
			const std::string_view tag = object.tag;
			minimumY = std::min(minimumY, world.GetPosition().y);
			if (tag == kSpawn) {
				if (spawn || std::ranges::find(startObjects, id) == startObjects.end()) {
					throw std::runtime_error("Only the starting prefab may contain one spawn marker.");
				}
				spawn = world;
			} else if (tag == kWind) {
				const auto key = "wind_" + std::to_string(fields.size() + 1);
				fields.push_back(ReadWind(object, world, key));
				airflow.script->references.emplace_back(key, id);
			} else if (tag.starts_with(kDecorationPrefix)) {
				const auto role = tag.substr(kDecorationPrefix.size());
				const auto decoration = std::ranges::find(project.decorations, role, &Decoration::marker);
				if (decoration != project.decorations.end()) {
					AddDecoration(result, id, *decoration);
				}
			}
		}
		if (!spawn) {
			throw std::runtime_error("Add a stage-spawn marker to the starting prefab.");
		}
		const auto playerId = AddPlayer(result, *spawn, minimumY);
		if (!fields.empty()) {
			if (airflow.script->asset.empty() || fields.size() >= ScriptComponent::kMaximumReferences) {
				throw std::runtime_error("Wind requires an airflow script and at most 31 wind volumes.");
			}
			airflow.script->references.emplace_back("player", playerId);
			airflow.script->initialState = Json{{"fields", std::move(fields)}}.dump();
			result.CreateObject(std::move(airflow));
		}
		LightingSettings lights;
		lights.intensity = 0.9f;
		result.CreateObject(SceneLighting::CreateDirectionalLight(lights));
		std::string serialized;
		if (!SceneSerializer::Serialize(result, serialized)) {
			throw std::runtime_error("The generated stage contains invalid scene data.");
		}
		scene.Swap(result);
		variants.swap(chosen);
		error.clear();
		return true;
	} catch (const std::exception &exception) {
		error = exception.what();
		return false;
	}
}
} // namespace StageFlow
