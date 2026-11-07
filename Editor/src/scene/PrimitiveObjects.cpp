#include "scene/PrimitiveObjects.h"
#include "math/MathUtils.h"
#include "physics/ColliderUtils.h"
#include "platform/windows/StringConversion.h"
#include <algorithm>
#include <cmath>
#include <exception>
#include <utility>

SceneObject PrimitiveObjects::CreateBox() {
	SceneObject object;
	object.name = "Box";
	object.mesh = MeshComponent{L"resources/editor/models/box.glb"};
	object.meshRenderer = MeshRendererComponent{};
	object.collider = Collider{{0, 0, 0}, {1, 1, 1}};
	return object;
}

bool PrimitiveObjects::TryCreateBox(const BoxCreationSettings &settings,
                                    const Collider &bounds, SceneObject &object,
                                    std::string &error) {
	try {
		if (settings.name.empty() ||
		    settings.name.find('\0') != std::string::npos ||
		    StringConversion::Utf8ToWide(settings.name).empty()) {
			error = "Enter a nonempty UTF-8 name.";
			return false;
		}
		AxisAlignedBox local;
		if (!MathUtils::IsFinite(settings.center) ||
		    !MathUtils::IsFinite(settings.size) || settings.size.x <= 0 ||
		    settings.size.y <= 0 || settings.size.z <= 0 ||
		    !ColliderUtils::GetWorldBounds(bounds, Transform{}, local)) {
			error = "Enter a finite center and positive dimensions with valid "
			        "model bounds.";
			return false;
		}
		auto candidate = CreateBox();
		candidate.name = settings.name;
		candidate.collider = bounds;
		const auto dimension = [](float size, float extent) {
			return float(double(size) / (2.0 * extent));
		};
		candidate.transform.scale = {
		    dimension(settings.size.x, bounds.halfExtents.x),
		    dimension(settings.size.y, bounds.halfExtents.y),
		    dimension(settings.size.z, bounds.halfExtents.z)};
		const auto position = [](float center, float localCenter, float scale) {
			return float(double(center) - double(localCenter) * scale);
		};
		const auto &scale = candidate.transform.scale;
		candidate.transform.translation = {
		    position(settings.center.x, bounds.center.x, scale.x),
		    position(settings.center.y, bounds.center.y, scale.y),
		    position(settings.center.z, bounds.center.z, scale.z)};
		AxisAlignedBox world;
		const auto matches = [](float minimum, float maximum, float center,
		                        float requested) {
			const double tolerance =
			    BoxCreationSettings::kBoundsRelativeTolerance *
			    (std::max)(1.0, double(requested));
			return std::abs((double(maximum) - minimum) - requested) <=
			           tolerance &&
			       std::abs((double(maximum) + minimum) * 0.5 - center) <=
			           tolerance;
		};
		if (!ColliderUtils::GetWorldBounds(bounds, candidate.transform,
		                                   world) ||
		    !matches(world.minimum.x, world.maximum.x, settings.center.x,
		             settings.size.x) ||
		    !matches(world.minimum.y, world.maximum.y, settings.center.y,
		             settings.size.y) ||
		    !matches(world.minimum.z, world.maximum.z, settings.center.z,
		             settings.size.z)) {
			error = "The center and size cannot be represented together. Use "
			        "smaller coordinates or larger dimensions.";
			return false;
		}
		object = std::move(candidate);
		error.clear();
		return true;
	} catch (const std::exception &exception) {
		error = exception.what();
		return false;
	}
}
