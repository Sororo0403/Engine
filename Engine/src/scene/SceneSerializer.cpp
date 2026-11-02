#include "scene/SceneSerializer.h"

#include "core/Logger.h"
#include "math/MathUtils.h"
#include "math/TransformUtils.h"
#include "physics/BoxColliderUtils.h"
#include "platform/windows/PathUtils.h"
#include "scene/Scene.h"

#include <json.hpp>

#include <exception>
#include <format>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
using Json = nlohmann::json;

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
} // namespace

bool SceneSerializer::Save(const Scene &scene,
                           const std::filesystem::path &filePath) {
	try {
		const auto targetPath = PathUtils::ResolveFromExecutable(filePath);

		Json objects = Json::array();

		for (const auto &[id, object] : scene.GetObjects()) {
			if (!TransformUtils::IsValid(object.transform)) {
				throw std::runtime_error("Transformが無効です。");
			}

			if (object.colorTint && !object.colorTint->IsValid()) {
				throw std::runtime_error("色の設定が無効です。");
			}

			if (object.collider) {
				AxisAlignedBox bounds;

				if (!BoxColliderUtils::GetWorldBounds(
				        *object.collider, object.transform, bounds)) {
					throw std::runtime_error("衝突用ボックスが無効です。");
				}
			}

			if ((object.camera && !object.camera->IsValid()) ||
			    (object.character && !object.character->IsValid()) ||
			    (object.followCamera && !object.followCamera->IsValid()) ||
			    (object.respawn && !object.respawn->IsValid())) {
				throw std::runtime_error(
				    "ゲーム用コンポーネントの設定が無効です。");
			}

			objects.push_back({
			    {"id", id},
			    {"name", object.name},
			    {"model", PathUtils::ToUtf8(object.modelPath)},
			    {"visible", object.visible},
			    {"transform",
			     {
			         {"translation", ToJson(object.transform.translation)},
			         {"rotation", ToJson(object.transform.rotation)},
			         {"scale", ToJson(object.transform.scale)},
			     }},
			});

			if (object.colorTint) {
				objects.back()["colorTint"] = {
				    {"color", ToJson(object.colorTint->color)}};
			}

			if (object.collider) {
				objects.back()["collider"] = {
				    {"center", ToJson(object.collider->center)},
				    {"halfExtents", ToJson(object.collider->halfExtents)}};
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
				    {"terminalSpeed", v.terminalSpeed}};
			}

			if (object.followCamera) {
				const auto &v = *object.followCamera;
				objects.back()["followCamera"] = {
				    {"target", v.target},
				    {"offset", ToJson(v.offset)},
				    {"lookOffset", ToJson(v.lookOffset)},
				    {"smoothing", v.smoothing}};
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
		    {"version", 7},
		    {"objects", std::move(objects)},
		};

		// 文字列への変換に成功してから、ファイルの書き込みを始める
		const std::string text = document.dump(2);

		std::filesystem::create_directories(targetPath.parent_path());

		auto temporaryPath = targetPath;
		temporaryPath += ".tmp";

		// 元の保存ファイルへ直接書き込まず、一時ファイルを使う
		std::ofstream output;
		output.exceptions(std::ios::failbit | std::ios::badbit);
		output.open(temporaryPath, std::ios::binary | std::ios::trunc);

		output << text << '\n';
		output.close();

		// 書き込みとクローズに成功してから保存ファイルを置き換える
		std::filesystem::rename(temporaryPath, targetPath);

		Logger::Info("シーンを保存しました。");
		return true;
	} catch (const std::exception &exception) {
		Logger::Error(
		    std::format("SceneSerializer::Save: {}", exception.what()));
		return false;
	}
}

bool SceneSerializer::Load(Scene &scene,
                           const std::filesystem::path &filePath) {
	try {
		const auto sourcePath = PathUtils::ResolveFromExecutable(filePath);

		std::ifstream input(sourcePath, std::ios::binary);

		if (!input) {
			throw std::runtime_error("シーンファイルを開けませんでした。");
		}

		input.exceptions(std::ios::badbit);

		const Json document = Json::parse(input);

		if (!document.is_object() || document.at("format") != "EngineScene" ||
		    !document.at("version").is_number_integer() ||
		    (document.at("version") != 1 && document.at("version") != 2 &&
		     document.at("version") != 3 && document.at("version") != 4 &&
		     document.at("version") != 5 && document.at("version") != 6 &&
		     document.at("version") != 7)) {
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

			if (version >= 3) {
				object.visible = item.at("visible").get<bool>();
			}

			object.name = item.at("name").get<std::string>();

			if (object.name.empty()) {
				object.name = "Object";
			}

			object.modelPath =
			    PathUtils::FromUtf8(item.at("model").get<std::string>());

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
			}

			if (version <= 5 && object.followCamera) {
				object.camera = CameraComponent{};
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
			    (object.respawn && !object.respawn->IsValid())) {
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

			if (object.collider) {
				AxisAlignedBox bounds;

				if (!BoxColliderUtils::GetWorldBounds(
				        *object.collider, object.transform, bounds)) {
					throw std::runtime_error("衝突用ボックスが無効です。");
				}
			}

			const bool inserted = objects.emplace(id, std::move(object)).second;

			if (!inserted) {
				throw std::runtime_error("物体のIDが重複しています。");
			}
		}

		// 全データの読み込みに成功してから、現在のシーンへ反映する
		scene.ReplaceObjects(std::move(objects));

		Logger::Info("シーンを読み込みました。");
		return true;
	} catch (const std::exception &exception) {
		Logger::Error(
		    std::format("SceneSerializer::Load: {}", exception.what()));
		return false;
	}
}
