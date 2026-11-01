#include "history/ObjectCommand.h"
#include "math/TransformUtils.h"
#include "physics/BoxColliderUtils.h"
#include "platform/windows/PathUtils.h"
#include "scene/Scene.h"
#include "scene/SceneSerializer.h"

#include <json.hpp>

#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <vector>

void RunCharacterTests();

namespace {
void Require(bool condition, const char *message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

void ColliderBoundsTests() {
	BoxCollider collider;
	AxisAlignedBox bounds;
	Require(BoxColliderUtils::GetWorldBounds(collider, {}, bounds), "default collider bounds");
	Require(bounds.minimum.x == -0.5f && bounds.maximum.y == 0.5f, "identity bounds");
	std::mt19937 random(17);
	std::uniform_real_distribution<float> unit(-1.0f, 1.0f);
	for (int sample = 0; sample < 1200; ++sample) {
		Transform transform;
		transform.translation = {unit(random) * 10, unit(random) * 10, unit(random) * 10};
		transform.scale = {(sample & 1 ? -1.0f : 1.0f) * (2 + unit(random)),
		                   2 + unit(random), 2 + unit(random)};
		transform.rotation = TransformUtils::RotationFromEuler({unit(random), unit(random), unit(random)});
		collider.center = {unit(random), unit(random), unit(random)};
		collider.halfExtents = {1.1f + unit(random), 1.1f + unit(random), 1.1f + unit(random)};
		Require(BoxColliderUtils::GetWorldBounds(collider, transform, bounds), "transformed bounds");
		const auto matrix = TransformUtils::MakeWorldMatrix(transform);
		for (int i = 0; i < 8; ++i) {
			DirectX::XMFLOAT3 local{collider.center.x + (i & 1 ? 1 : -1) * collider.halfExtents.x,
			                        collider.center.y + (i & 2 ? 1 : -1) * collider.halfExtents.y,
			                        collider.center.z + (i & 4 ? 1 : -1) * collider.halfExtents.z};
			DirectX::XMFLOAT3 point;
			DirectX::XMStoreFloat3(&point, DirectX::XMVector3TransformCoord(DirectX::XMLoadFloat3(&local), matrix));
			Require(point.x >= bounds.minimum.x && point.x <= bounds.maximum.x &&
			        point.y >= bounds.minimum.y && point.y <= bounds.maximum.y &&
			        point.z >= bounds.minimum.z && point.z <= bounds.maximum.z, "bounds do not contain corners");
		}
	}
	const AxisAlignedBox original{{10, 11, 12}, {20, 21, 22}};
	for (float invalid : {0.0f, -1.0f, std::numeric_limits<float>::infinity(),
	                       std::numeric_limits<float>::quiet_NaN()}) {
		bounds = original;
		collider = {};
		collider.halfExtents.x = invalid;
		Require(!BoxColliderUtils::GetWorldBounds(collider, {}, bounds) &&
		        bounds.minimum.x == original.minimum.x && bounds.maximum.z == original.maximum.z,
		        "invalid collider changed output");
	}
	collider = {};
	Transform huge;
	huge.translation.x = 1.0e20f;
	Require(!BoxColliderUtils::GetWorldBounds(collider, huge, bounds), "collapsed float bounds accepted");
	std::cout << "PASS: collider bounds, 1200 transformed/mirrored shapes, invalid input preservation\n";
}

struct SceneFile {
	std::filesystem::path path = PathUtils::GetExecutableDirectory() /
	    ("gameplay-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".json");
	~SceneFile() {
		std::error_code error;
		std::filesystem::remove(path, error);
		auto temporary = path;
		temporary += ".tmp";
		std::filesystem::remove(temporary, error);
	}
	void Write(const nlohmann::json &value) const {
		std::ofstream stream(path);
		stream << value.dump();
	}
	nlohmann::json Read() const {
		std::ifstream stream(path);
		return nlohmann::json::parse(stream);
	}
};

void ColliderSerializationTests() {
	Scene source;
	const auto solid = source.CreateObject("Wall", "models/box.glb");
	source.FindObject(solid)->collider = BoxCollider{{0.1f, 0.2f, 0.3f}, {2, 3, 4}};
	source.FindObject(solid)->visible = false;
	source.FindObject(solid)->transform.scale = {-2, 3, 1};
	const auto decoration = source.CreateObject("Decoration");
	SceneFile file;
	Require(SceneSerializer::Save(source, file.path), "save collider scene");
	const auto document = file.Read();
	Require(document.at("version") == 4, "collider scene version");
	Scene loaded;
	Require(SceneSerializer::Load(loaded, file.path) &&
	        loaded.FindObject(solid)->collider == source.FindObject(solid)->collider &&
	        !loaded.FindObject(solid)->visible && !loaded.FindObject(decoration)->collider,
	        "collider round trip");
	for (int version : {1, 2, 3}) {
		auto legacy = document;
		legacy["version"] = version;
		for (auto &item : legacy["objects"]) {
			item.erase("collider");
			if (version < 3) item.erase("visible");
			if (version == 1) item["transform"]["rotation"] = {0, 0, 0};
		}
		file.Write(legacy);
		Require(SceneSerializer::Load(loaded, file.path) && !loaded.FindObject(solid)->collider,
		        "legacy scene compatibility");
	}
	Scene current;
	const auto keep = current.CreateObject("Keep current scene");
	for (const auto &invalid : std::vector<nlohmann::json>{
	         nullptr, nlohmann::json::object(),
	         {{"center", {0, 0, 0}}, {"halfExtents", {0, 1, 1}}},
	         {{"center", {true, 0, 0}}, {"halfExtents", {1, 1, 1}}},
	         {{"center", {0, 0, 0}}, {"halfExtents", {-1, 1, 1}}}}) {
		auto bad = document;
		bad["objects"][0]["collider"] = invalid;
		file.Write(bad);
		Require(!SceneSerializer::Load(current, file.path) &&
		        current.FindObject(keep)->name == "Keep current scene", "failed load replaced scene");
	}
	file.Write(document);
	source.FindObject(solid)->collider->halfExtents.x = 0;
	Require(!SceneSerializer::Save(source, file.path) && file.Read() == document,
	        "invalid collider save damaged file");
	std::cout << "PASS: version 4 collider persistence, v1/v2/v3 compatibility and atomic invalid-data failures\n";
}

void ColliderObjectHistoryTests() {
	Scene scene;
	SceneObject object;
	object.collider = BoxCollider{};
	const auto id = scene.CreateObject(object);
	ObjectCommand created(id, object, ObjectCommand::Kind::CREATE);
	scene.FindObject(id)->collider->center.x = 1;
	Require(!created.Undo(scene) && scene.Contains(id), "Undo ignored changed collider");
	scene.FindObject(id)->collider = object.collider;
	Require(created.Undo(scene) && !scene.Contains(id) && created.Redo(scene) &&
	        scene.FindObject(id)->collider == object.collider, "object history lost collider");
	std::cout << "PASS: object Undo/Redo preserves collider and rejects stale snapshots\n";
}
} // namespace

int main() {
	try {
		ColliderBoundsTests();
		ColliderSerializationTests();
		ColliderObjectHistoryTests();
		RunCharacterTests();
		std::cout << "ALL PASS\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << "FAIL: " << error.what() << '\n';
		return 1;
	}
}
