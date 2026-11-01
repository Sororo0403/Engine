#include "scene/SceneSerializer.h"

#include "core/Logger.h"
#include "math/MathUtils.h"
#include "math/TransformUtils.h"
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
		}

		const Json document{
		    {"format", "EngineScene"},
		    {"version", 3},
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
		     document.at("version") != 3)) {
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

			const Json &transform = item.at("transform");

			object.transform.translation =
			    ReadFloat3(transform.at("translation"));
			// 旧形式のオイラー角は、従来と同じ回転順序で変換する
			object.transform.rotation =
			    version == 1
			        ? TransformUtils::RotationFromEuler(
			              ReadFloat3(transform.at("rotation")))
			        : ReadRotation(transform.at("rotation"));
			object.transform.scale = ReadFloat3(transform.at("scale"));

			if (!TransformUtils::IsValid(object.transform)) {
				throw std::runtime_error("Transformが無効です。");
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
