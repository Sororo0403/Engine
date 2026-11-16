#include "scene/StageDecorationGenerator.h"

#include "graphics/model/ModelManager.h"
#include "platform/windows/PathUtils.h"
#include "scene/Scene.h"
#include "scene/SceneActivity.h"
#include "scene/SceneRenderer.h"
#include "scene/StageDecorationSource.h"

#include <json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <utility>

namespace {
using DirectX::XMFLOAT3;
using StageDecorationLayout::Role;

struct DecorationPart {
	std::filesystem::path path;
	std::string name;
	float scale;
	XMFLOAT3 offset;
};

struct DecorationAsset {
	std::vector<DecorationPart> parts;
	float yawOffset;
	AxisAlignedBox bounds;
};

constexpr std::array kRoleKeys{"windGuide", "landingGuide", "scenery", "backdrop"};

/// <summary>
/// 境界の頂点から各軸の最小値と最大値を求める
/// </summary>
AxisAlignedBox GetBounds(const std::array<XMFLOAT3, 8> &corners) {
	AxisAlignedBox bounds{corners.front(), corners.front()};
	for (const auto &point : corners) {
		bounds.minimum.x = std::min(bounds.minimum.x, point.x);
		bounds.minimum.y = std::min(bounds.minimum.y, point.y);
		bounds.minimum.z = std::min(bounds.minimum.z, point.z);
		bounds.maximum.x = std::max(bounds.maximum.x, point.x);
		bounds.maximum.y = std::max(bounds.maximum.y, point.y);
		bounds.maximum.z = std::max(bounds.maximum.z, point.z);
	}
	return bounds;
}

/// <summary>
/// プロジェクト内の素材参照を解決する
/// </summary>
std::filesystem::path ResolveAsset(const std::filesystem::path &root,
                                  const nlohmann::json &entry) {
	const auto relative = PathUtils::FromUtf8(entry.get<std::string>());
	if (relative.empty() || relative.has_root_path()) {
		throw std::runtime_error("Decoration assets need project-relative paths.");
	}
	const auto resolved = std::filesystem::canonical(root / relative);
	PathUtils::RelativeInside(root / L"assets", resolved);
	if (!std::filesystem::is_regular_file(resolved)) {
		throw std::runtime_error("Not an asset file: " + PathUtils::ToUtf8(resolved));
	}
	return resolved;
}

/// <summary>
/// モデルの寸法を取得し、テーマから指定された大きさと向きを検証する
/// </summary>
DecorationAsset ReadAsset(const nlohmann::json &entry,
                          const std::filesystem::path &root,
                          ModelManager &models) {
	DecorationAsset asset;
	const float scale = entry.value("scale", 1.0f);
	asset.yawOffset = DirectX::XMConvertToRadians(entry.value("yawDegrees", 0.0f));
	if (!std::isfinite(scale) || scale <= 0.0f || !std::isfinite(asset.yawOffset)) {
		throw std::runtime_error("Invalid decoration scale or orientation.");
	}

	const auto appendPart = [&](const nlohmann::json &data,
	                            const std::array<float, 3> &offset) {
		DecorationPart part;
		part.path = ResolveAsset(root, data.at("model"));
		part.name = data.value("name", PathUtils::ToUtf8(part.path.stem()));
		part.scale = scale;
		part.offset = {offset[0] * scale, offset[1] * scale, offset[2] * scale};
		std::array<XMFLOAT3, 8> corners;
		if (part.name.empty() || !std::ranges::all_of(offset, [](float component) {
			    return std::isfinite(component);
		    }) || !models.GetLocalBounds(models.Load(part.path), corners)) {
			throw std::runtime_error("Invalid decoration asset: " + PathUtils::ToUtf8(part.path));
		}
		for (auto &point : corners) {
			point = {point.x * scale + part.offset.x, point.y * scale + part.offset.y,
			         point.z * scale + part.offset.z};
		}
		const auto partBounds = GetBounds(corners);
		if (asset.parts.empty()) {
			asset.bounds = partBounds;
		} else {
			auto &box = asset.bounds;
			box.minimum.x = std::min(box.minimum.x, partBounds.minimum.x);
			box.minimum.y = std::min(box.minimum.y, partBounds.minimum.y);
			box.minimum.z = std::min(box.minimum.z, partBounds.minimum.z);
			box.maximum.x = std::max(box.maximum.x, partBounds.maximum.x);
			box.maximum.y = std::max(box.maximum.y, partBounds.maximum.y);
			box.maximum.z = std::max(box.maximum.z, partBounds.maximum.z);
		}
		asset.parts.push_back(std::move(part));
	};
	appendPart(entry, {});
	if (entry.contains("parts")) {
		for (const auto &part : entry.at("parts")) {
			appendPart(part, part.at("offset").get<std::array<float, 3>>());
		}
	}

	return asset;
}

/// <summary>
/// 足場のほかに、見た目だけの既存の小物や目印も配置対象から避ける
/// </summary>
void GatherVisualObstacles(const Scene &scene, const SceneRenderer &renderer,
                           StageDecorationSource &layout) {
	SceneActivity::States states;
	if (!SceneActivity::BuildStates(scene, states)) {
		throw std::runtime_error("Could not read scene object activity.");
	}
	for (const auto &[id, object] : scene.GetObjects()) {
		std::array<XMFLOAT3, 8> corners;
		if (!states.at(id) || !object.IsRenderable() || object.collider ||
		    object.character || !renderer.GetObjectBounds(id, corners)) {
			continue;
		}
		const auto bounds = GetBounds(corners);
		// 地平線や大きな雲の外接箱は通路も含むため、地上の小物の障害物から外す。
		if (bounds.maximum.x - bounds.minimum.x <= 14.0f &&
		    bounds.maximum.z - bounds.minimum.z <= 14.0f) {
			layout.obstacles.push_back(bounds);
		}
	}
}
} // namespace

std::size_t StageDecorationGenerator::Generate(
    const Scene &source, const std::filesystem::path &projectRoot, std::uint32_t seed,
    ModelManager &models, const SceneRenderer &renderer, Scene &output,
    std::string &error) {
	try {
		const auto root = std::filesystem::canonical(projectRoot);
		const auto themePath = root / L"decoration-theme.json";
		std::ifstream input(themePath, std::ios::binary);
		if (!input) {
			throw std::runtime_error("Missing decoration theme: " + PathUtils::ToUtf8(themePath));
		}
		const auto theme = nlohmann::json::parse(input);
		if (theme.at("format") != "StageDecorationTheme" || theme.at("version") != 1) {
			throw std::runtime_error("Unsupported decoration theme format.");
		}

		StageDecorationSource layout;
		const auto windScript = theme.contains("windScript")
		                            ? ResolveAsset(root, theme.at("windScript"))
		                            : std::filesystem::path{};
		if (!layout.Read(source, theme.at("platformModelPrefix").get<std::string>(),
		                 windScript, error)) {
			return 0;
		}
		GatherVisualObstacles(source, renderer, layout);

		std::array<DecorationAsset, kRoleKeys.size()> assets;
		std::array<XMFLOAT3, kRoleKeys.size()> footprints;
		for (std::size_t i = 0; i < assets.size(); ++i) {
			auto &asset = assets[i];
			asset = ReadAsset(theme.at("models").at(kRoleKeys[i]), root, models);
			const auto &box = asset.bounds;
			footprints[i] = {box.maximum.x - box.minimum.x,
			                 box.maximum.y - box.minimum.y,
			                 box.maximum.z - box.minimum.z};
		}

		const auto placements = StageDecorationLayout::Generate(
		    layout.route, layout.windAreas, layout.obstacles, footprints, seed);
		if (placements.empty()) {
			throw std::runtime_error("No decoration fits the available space.");
		}

		Scene generated;
		generated.CopyFrom(source);
		const auto parent = generated.CreateObject("Generated Stage Decorations");
		for (const auto &placement : placements) {
			const auto &asset = assets[static_cast<std::size_t>(placement.role)];
			using namespace DirectX;
			const XMVECTOR rotation = XMQuaternionRotationRollPitchYaw(
			    0.0f, placement.yaw + asset.yawOffset, 0.0f);
			const XMFLOAT3 localBase{(asset.bounds.minimum.x + asset.bounds.maximum.x) * 0.5f,
			                         asset.bounds.minimum.y,
			                         (asset.bounds.minimum.z + asset.bounds.maximum.z) * 0.5f};
			SceneObject group;
			group.name = asset.parts.front().name;
			group.parentId = parent;
			XMStoreFloat4(&group.transform.rotation, rotation);
			XMStoreFloat3(&group.transform.translation,
			              XMLoadFloat3(&placement.position) -
			                  XMVector3Rotate(XMLoadFloat3(&localBase), rotation));
			// 複数のモデルで構成する装飾は、根を編集すると一緒に移動する。
			if (asset.parts.size() > 1) {
				const auto groupId = generated.CreateObject(std::move(group));
				for (const auto &part : asset.parts) {
					SceneObject object;
					object.name = part.name;
					object.parentId = groupId;
					object.mesh = MeshComponent{part.path};
					object.meshRenderer.emplace();
					object.transform.translation = part.offset;
					object.transform.scale = {part.scale, part.scale, part.scale};
					generated.CreateObject(std::move(object));
				}
			} else {
				const auto &part = asset.parts.front();
				group.mesh = MeshComponent{part.path};
				group.meshRenderer.emplace();
				group.transform.scale = {part.scale, part.scale, part.scale};
				generated.CreateObject(std::move(group));
			}
		}
		output.Swap(generated);
		error.clear();
		return placements.size();
	} catch (const std::exception &exception) {
		error = exception.what();
		return 0;
	}
}
