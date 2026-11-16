#include "gameplay/StagePlan.h"

#include "scene/PrimitiveObjects.h"
#include "scene/Scene.h"
#include "scene/SceneLighting.h"

#include <json.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <set>
#include <stdexcept>
#include <utility>

namespace StagePlan {
namespace {
using Json = nlohmann::json;
using DirectX::XMFLOAT3;

constexpr std::size_t kMaxInputBytes = 256 * 1024;
constexpr std::size_t kMaxSections = 8;
constexpr std::size_t kMaxLandings = 8;
constexpr std::size_t kMaxWinds = 3; // 8区間でも風と主人公が既存のLua参照数に収まる
constexpr float kMinimumPlatformSize = 0.4f;
constexpr float kPlayerHalfWidth = 0.32f;
constexpr float kPlayerHalfHeight = 0.5f;
constexpr CharacterSettings kPlayerSettings{
    .moveSpeed = 4.6f,
    .jumpSpeed = 7.5f,
    .gravity = 20.0f,
    .terminalSpeed = 30.0f,
    .coyoteTime = 0.08f,
    .jumpBufferTime = 0.12f,
    .groundSnapDistance = 0.12f,
    .stepHeight = 0.36f,
    .airFloatDelay = 0.25f,
    .airFloatDuration = 0.85f,
};

/// <summary>
/// 生成対象を特定するための安定した識別子を確認する
/// </summary>
std::string ReadId(const Json &json) {
	const auto id = json.get<std::string>();
	if (id.empty() || id.size() > 40 ||
	    !std::ranges::all_of(id, [](char c) {
		    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
	    })) {
		throw std::runtime_error("Use short IDs containing a-z, 0-9 or underscores.");
	}
	return id;
}

XMFLOAT3 ReadVector(const Json &json, float minimum, float maximum) {
	if (!json.is_array() || json.size() != 3) {
		throw std::runtime_error("Expected three numbers in X, Y, Z order.");
	}
	const auto read = [&](std::size_t index) {
		if (!json[index].is_number()) {
			throw std::runtime_error("Coordinates must be numbers.");
		}
		const double value = json[index].get<double>();
		if (!std::isfinite(value) || value < minimum || value > maximum) {
			throw std::runtime_error("Coordinates or dimensions are outside the supported range.");
		}
		return static_cast<float>(value);
	};
	return {read(0), read(1), read(2)};
}

Platform ReadPlatform(const Json &json) {
	const auto style = json.at("asset").get<std::string>();
	if (style != "stone" && style != "turf") {
		throw std::runtime_error("Unknown platform asset '" + style + "'.");
	}
	return {ReadId(json.at("id")),
	        style == "stone" ? PlatformStyle::STONE : PlatformStyle::TURF,
	        ReadVector(json.at("topCenter"), -2048.0f, 2048.0f),
	        ReadVector(json.at("size"), kMinimumPlatformSize, 24.0f)};
}

Json Describe(const XMFLOAT3 &value) {
	return Json::array({value.x, value.y, value.z});
}

Json Describe(const Platform &platform) {
	return {{"id", platform.id},
	        {"asset", platform.style == PlatformStyle::STONE ? "stone" : "turf"},
	        {"topCenter", Describe(platform.topCenter)},
	        {"size", Describe(platform.size)}};
}

/// <summary>
/// 区間で指定された種類の風が含まれるか確認し、配置データを読み込む
/// </summary>
Section ReadSection(const Json &json, const SectionBrief &brief) {
	Section section;
	section.id = ReadId(json.at("id"));
	if (section.id != brief.id) {
		throw std::runtime_error("Expected section ID '" + brief.id + "'.");
	}
	const auto &landings = json.at("landings");
	const auto &winds = json.at("winds");
	if (!landings.is_array() || landings.empty() || landings.size() > kMaxLandings ||
	    !winds.is_array() || winds.empty() || winds.size() > kMaxWinds) {
		throw std::runtime_error("Provide 1–8 landings and 1–3 wind volumes.");
	}
	for (const auto &landing : landings) {
		section.landings.push_back(ReadPlatform(landing));
	}
	for (const auto &wind : winds) {
		section.winds.push_back({ReadVector(wind.at("center"), -2048.0f, 2048.0f),
		                         ReadVector(wind.at("size"), 0.25f, 64.0f),
		                         ReadVector(wind.at("velocity"), -16.0f, 16.0f),
		                         wind.at("airborneOnly").get<bool>()});
	}
	const bool hasRequestedWind = std::ranges::any_of(section.winds, [&](const Wind &wind) {
		return brief.wind == WindRequirement::UPDRAFT ? wind.velocity.y > 0.0f :
		       std::hypot(wind.velocity.x, wind.velocity.z) > 0.0f;
	});
	if (!hasRequestedWind) {
		throw std::runtime_error("The layout is missing the requested wind.");
	}
	return section;
}

bool Overlaps(const Platform &a, const Platform &b) {
	return std::abs(a.topCenter.x - b.topCenter.x) < (a.size.x + b.size.x) * 0.5f &&
	       std::abs(a.topCenter.z - b.topCenter.z) < (a.size.z + b.size.z) * 0.5f &&
	       a.topCenter.y - a.size.y < b.topCenter.y &&
	       b.topCenter.y - b.size.y < a.topCenter.y;
}

/// <summary>
/// 区間の順番を保って全足場を検査し、重複IDと交差する足場を検出する
/// </summary>
void ValidatePlatforms(const Layout &layout, std::span<const SectionBrief> brief) {
	std::set<std::string> ids{layout.start.id};
	std::vector<const Platform *> platforms{&layout.start};
	for (std::size_t i = 0; i < layout.sections.size(); ++i) {
		for (const auto &landing : layout.sections[i].landings) {
			if (!ids.insert(landing.id).second) {
				throw std::runtime_error(brief[i].name + ": Duplicate landing '" + landing.id + "'.");
			}
			for (const auto *other : platforms) {
				if (Overlaps(landing, *other)) {
					throw std::runtime_error(brief[i].name + ": Landing '" + landing.id +
					                         "' overlaps '" + other->id + "'.");
				}
			}
			platforms.push_back(&landing);
		}
	}
}

bool MatchesBrief(const Layout &layout, std::span<const SectionBrief> brief) {
	return layout.sections.size() == brief.size() &&
	       std::ranges::equal(layout.sections, brief, {}, &Section::id, &SectionBrief::id);
}

/// <summary>
/// 足場を指定した断面寸法の箱として追加し、上面を計画の高さに合わせる
/// </summary>
void AddPlatform(const Platform &platform, SceneObjectId parent, Scene &scene) {
	auto object = PrimitiveObjects::CreateBox();
	object.name = platform.id;
	object.parentId = parent;
	object.transform.scale = {platform.size.x * 0.5f, platform.size.y * 0.5f,
	                          platform.size.z * 0.5f};
	object.transform.translation = platform.topCenter;
	object.transform.translation.y -= object.transform.scale.y;
	object.colorTint = ColorTint{platform.style == PlatformStyle::STONE
	                                 ? XMFLOAT3{0.66f, 0.58f, 0.46f}
	                                 : XMFLOAT3{0.35f, 0.56f, 0.35f}};
	scene.CreateObject(std::move(object));
}
} // namespace

bool Import(std::string_view text, std::span<const SectionBrief> brief,
            std::optional<std::string_view> replaceSection, Layout &layout,
            std::string &error) {
	try {
		if (text.size() > kMaxInputBytes || brief.empty() || brief.size() > kMaxSections) {
			throw std::runtime_error("Use a layout under 256 KiB with 1–8 sections.");
		}
		std::set<std::string> ids;
		for (const auto &section : brief) {
			if (section.name.empty() || !ids.insert(ReadId(section.id)).second) {
				throw std::runtime_error("Section names and unique IDs are required.");
			}
		}
		const auto json = Json::parse(text);
		if (json.at("format") != "StagePlan" || !json.at("version").is_number_integer() ||
		    json.at("version") != 1) {
			throw std::runtime_error("Expected StagePlan version 1.");
		}

		const auto &sections = json.at("sections");
		if (!sections.is_array() || sections.size() != (replaceSection ? 1 : brief.size())) {
			throw std::runtime_error("The response does not contain the requested sections.");
		}
		Layout candidate;
		if (replaceSection) {
			const auto section = std::ranges::find(brief, *replaceSection, &SectionBrief::id);
			if (!MatchesBrief(layout, brief) || section == brief.end()) {
				throw std::runtime_error("There is no imported layout for the current section order.");
			}
			const auto index = static_cast<std::size_t>(section - brief.begin());
			auto revision = ReadSection(sections[0], *section);
			if (Describe(revision.landings.back()) != Describe(layout.sections[index].landings.back())) {
				throw std::runtime_error(section->name + ": The fixed exit landing was changed.");
			}
			candidate = layout;
			candidate.sections[index] = std::move(revision);
		} else {
			candidate.start = ReadPlatform(json.at("start"));
			if (candidate.start.size.x < 1.5f || candidate.start.size.z < 1.5f) {
				throw std::runtime_error("Make the starting platform at least 1.5 m wide and deep.");
			}
			for (std::size_t i = 0; i < brief.size(); ++i) {
				try {
					candidate.sections.push_back(ReadSection(sections[i], brief[i]));
				} catch (const std::exception &exception) {
					throw std::runtime_error(brief[i].name + ": " + exception.what());
				}
			}
		}
		ValidatePlatforms(candidate, brief);
		layout = std::move(candidate);
		error.clear();
		return true;
	} catch (const std::exception &exception) {
		error = exception.what();
		return false;
	}
}

std::string MakePrompt(std::span<const SectionBrief> brief, const Layout *previous,
                       std::optional<std::string_view> replaceSection) {
	Json request = Json::array();
	for (const auto &section : brief) {
		request.push_back({{"id", section.id}, {"name", section.name},
		                   {"wind", section.wind == WindRequirement::UPDRAFT ? "updraft" : "crosswind"},
		                   {"windIsRequired", section.windIsRequired}, {"intent", section.intent}});
	}
	if (request.empty()) {
		return {};
	}

	std::string prompt =
	    "Design a playable linear 3D platformer course for Skywind. Return only valid JSON, "
	    "without markdown. Preserve the supplied section order and IDs.\n"
	    "Schema: {\"format\":\"StagePlan\",\"version\":1,\"start\":Platform,"
	    "\"sections\":[{\"id\":string,\"landings\":[Platform],\"winds\":[Wind]}]}.\n"
	    "Platform = {\"id\":string,\"asset\":\"stone\" or \"turf\","
	    "\"topCenter\":[x,y,z],\"size\":[width,thickness,depth]}. Both assets are "
	    "axis-aligned solid boxes, scaled to the specified world size (0.4–24 m per axis). "
	    "Use unique lowercase Latin IDs with digits or underscores, up to 40 characters. "
	    "Use a generous first platform. Each section starts on the previous exit (or start), "
	    "so do not repeat that platform. Ordered landings include the section's exit.\n"
	    "Wind = {\"center\":[x,y,z],\"size\":[width,height,depth],"
	    "\"velocity\":[x,y,z],\"airborneOnly\":boolean}. Y is up. Wind velocity is added "
	    "to character motion inside the volume. Provide 1–8 landings and 1–3 wind volumes per section. "
	    "Do not overlap platforms or introduce shortcuts across required landings.\n";
	prompt += std::format(
	    "Player: {:.2f} m/s movement, {:.2f} m/s jump, {:.2f} m/s^2 gravity, "
	    "{:.2f} s max air float when held after {:.2f} s, {:.2f} m wide and {:.2f} m tall.\n",
	    kPlayerSettings.moveSpeed, kPlayerSettings.jumpSpeed, kPlayerSettings.gravity,
	    kPlayerSettings.airFloatDuration, kPlayerSettings.airFloatDelay, kPlayerHalfWidth * 2,
	    kPlayerHalfHeight * 2);

	if (replaceSection) {
		const auto section = std::ranges::find(brief, *replaceSection, &SectionBrief::id);
		if (!previous || !MatchesBrief(*previous, brief) || section == brief.end()) {
			return {};
		}
		const auto index = static_cast<std::size_t>(section - brief.begin());
		const auto &entry = index == 0 ? previous->start :
		                                 previous->sections[index - 1].landings.back();
		const auto &exit = previous->sections[index].landings.back();
		prompt += "Return just the requested section in sections, and omit start. Keep the exit "
		          "platform exactly as listed, and connect from the fixed entry. Other sections stay fixed.\n";
		prompt += Json{{"replace", *replaceSection}, {"fixedEntry", Describe(entry)},
		               {"fixedExit", Describe(exit)}}.dump(2) + "\n";
	}
	return prompt + request.dump(2);
}

bool BuildScene(const Layout &layout, const std::filesystem::path &projectRoot,
                Scene &output, std::string &error) {
	try {
		if (layout.sections.empty()) {
			throw std::runtime_error("Import a valid layout first.");
		}
		const auto scriptPath =
		    projectRoot / L"assets/skywind/scripts/StagePlanAirflow.lua";
		if (!std::filesystem::is_regular_file(scriptPath)) {
			throw std::runtime_error("Open the Skywind prototype project to build this layout.");
		}

		Scene scene;
		AddPlatform(layout.start, 0, scene);
		float minimumY = layout.start.topCenter.y;
		SceneObject airflow;
		airflow.name = "Stage Plan Airflow";
		airflow.script.emplace();
		airflow.script->asset = scriptPath;
		Json fields = Json::array();
		for (const auto &section : layout.sections) {
			const auto parent = scene.CreateObject(section.id);
			for (const auto &landing : section.landings) {
				AddPlatform(landing, parent, scene);
				minimumY = std::min(minimumY, landing.topCenter.y);
			}
			for (std::size_t i = 0; i < section.winds.size(); ++i) {
				const auto &wind = section.winds[i];
				const auto key = std::format("wind_{}", fields.size() + 1);
				SceneObject volume;
				volume.name = std::format("{} Wind {}", section.id, i + 1);
				volume.parentId = parent;
				volume.transform.translation = wind.center;
				volume.transform.scale = {wind.size.x * 0.5f, wind.size.y * 0.5f,
				                          wind.size.z * 0.5f};
				airflow.script->references.emplace_back(key, scene.CreateObject(std::move(volume)));
				fields.push_back({{"reference", key}, {"velocity", Describe(wind.velocity)},
				                  {"airborneOnly", wind.airborneOnly}});
			}
		}
		auto player = PrimitiveObjects::CreateBox();
		player.name = "Player";
		player.transform.scale = {kPlayerHalfWidth, kPlayerHalfHeight, kPlayerHalfWidth};
		player.transform.translation = layout.start.topCenter;
		player.transform.translation.y += kPlayerHalfHeight + 0.02f;
		player.colorTint = ColorTint{{0.9f, 0.67f, 0.2f}};
		player.character = kPlayerSettings;
		player.respawn = RespawnSettings{minimumY - 8.0f};
		const auto playerId = scene.CreateObject(std::move(player));
		airflow.script->references.emplace_back("player", playerId);
		airflow.script->initialState = Json{{"fields", std::move(fields)}}.dump();
		scene.CreateObject(std::move(airflow));

		SceneObject camera;
		camera.name = "Follow Camera";
		camera.camera.emplace();
		camera.followCamera.emplace();
		camera.followCamera->target = playerId;
		camera.followCamera->offset = {0.0f, 4.2f, -7.5f};
		camera.followCamera->lookOffset = {0.0f, 1.0f, 3.0f};
		camera.followCamera->avoidObstacles = true;
		scene.CreateObject(std::move(camera));

		LightingSettings lighting;
		lighting.direction = {1.0f, -2.0f, 1.0f};
		lighting.intensity = 0.9f;
		scene.CreateObject(SceneLighting::CreateDirectionalLight(lighting));
		output.Swap(scene);
		error.clear();
		return true;
	} catch (const std::exception &exception) {
		error = exception.what();
		return false;
	}
}
} // namespace StagePlan
