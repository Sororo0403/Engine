#include "scene/SceneSerializer.h"

#include "core/Logger.h"
#include "platform/windows/PathUtils.h"
#include "scene/Scene.h"

#include <json.hpp>

#include <cmath>
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
/// 実行ファイルのフォルダを基準にファイルの絶対パスを求める
/// </summary>
/// <param name="filePath">絶対パスまたは相対パス</param>
/// <returns>正規化した絶対パス</returns>
std::filesystem::path ResolveFilePath(const std::filesystem::path &filePath) {
	if (filePath.empty()) {
		throw std::invalid_argument("ファイルのパスが空です。");
	}

	if (filePath.is_absolute()) {
		return filePath.lexically_normal();
	}

	const auto directory = PathUtils::GetExecutableDirectory();

	if (directory.empty()) {
		throw std::runtime_error(
		    "実行ファイルのフォルダを取得できませんでした。");
	}

	return (directory / filePath).lexically_normal();
}

/// <summary>
/// ファイルパスをUTF-8の文字列に変換する
/// </summary>
/// <param name="path">変換するパス</param>
/// <returns>区切り文字にスラッシュを使うUTF-8文字列</returns>
std::string PathToUtf8(const std::filesystem::path &path) {
	const auto text = path.generic_u8string();

	return std::string(reinterpret_cast<const char *>(text.data()),
	                   text.size());
}

/// <summary>
/// UTF-8の文字列をファイルパスに変換する
/// </summary>
/// <param name="text">UTF-8の文字列</param>
/// <returns>変換したパス</returns>
std::filesystem::path PathFromUtf8(const std::string &text) {
	std::u8string utf8;
	utf8.reserve(text.size());

	for (const unsigned char character : text) {
		utf8.push_back(static_cast<char8_t>(character));
	}

	return std::filesystem::path(utf8);
}

/// <summary>
/// 3成分がすべて有限の値か確認する
/// </summary>
/// <param name="value">確認する値</param>
/// <returns>true:すべて有限 false:無限大または非数を含む</returns>
bool IsFinite(const DirectX::XMFLOAT3 &value) {
	return std::isfinite(value.x) && std::isfinite(value.y) &&
	       std::isfinite(value.z);
}

/// <summary>
/// 保存・読み込みする変形情報が有効か確認する
/// </summary>
/// <param name="transform">確認する変形情報</param>
void ValidateTransform(const Transform &transform) {
	if (!IsFinite(transform.translation) || !IsFinite(transform.rotation) ||
	    !IsFinite(transform.scale)) {
		throw std::runtime_error("Transformに無効な数値が含まれています。");
	}

	if (transform.scale.x == 0.0f || transform.scale.y == 0.0f ||
	    transform.scale.z == 0.0f) {
		throw std::runtime_error("Transformの拡大率に0は指定できません。");
	}
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

	if (!IsFinite(result)) {
		throw std::runtime_error(
		    "Transformの値がfloatの有効範囲を超えています。");
	}

	return result;
}
} // namespace

bool SceneSerializer::Save(const Scene &scene,
                           const std::filesystem::path &filePath) {
	try {
		const auto targetPath = ResolveFilePath(filePath);

		Json objects = Json::array();

		for (const auto &[id, object] : scene.GetObjects()) {
			ValidateTransform(object.transform);

			objects.push_back({
			    {"id", id},
			    {"name", object.name},
			    {"model", PathToUtf8(object.modelPath)},
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
		    {"version", 1},
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
		const auto sourcePath = ResolveFilePath(filePath);

		std::ifstream input(sourcePath, std::ios::binary);

		if (!input) {
			throw std::runtime_error("シーンファイルを開けませんでした。");
		}

		input.exceptions(std::ios::badbit);

		const Json document = Json::parse(input);

		if (!document.is_object() || document.at("format") != "EngineScene" ||
		    !document.at("version").is_number_integer() ||
		    document.at("version") != 1) {
			throw std::runtime_error("未対応のシーンファイル形式です。");
		}

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
			object.name = item.at("name").get<std::string>();

			if (object.name.empty()) {
				object.name = "Object";
			}

			object.modelPath =
			    PathFromUtf8(item.at("model").get<std::string>());

			const Json &transform = item.at("transform");

			object.transform.translation =
			    ReadFloat3(transform.at("translation"));
			object.transform.rotation = ReadFloat3(transform.at("rotation"));
			object.transform.scale = ReadFloat3(transform.at("scale"));

			ValidateTransform(object.transform);

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
