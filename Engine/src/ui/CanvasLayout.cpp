#include "ui/CanvasLayout.h"
#include "scene/Scene.h"
#include "scene/SceneActivity.h"
#include "scene/SceneHierarchy.h"
#include <algorithm>
#include <cmath>
#include <exception>
#include <format>
#include <map>
#include <numbers>
#include <stdexcept>
#include <tuple>

bool UIElement::IntersectsViewport(int width, int height) const {
	const auto center = transform.ToScreen(rect.x + rect.width * .5f,
	                                       rect.y + rect.height * .5f);
	const float c = std::abs(transform.cosine), s = std::abs(transform.sine);
	const float halfWidth = (c * rect.width + s * rect.height) * .5f;
	const float halfHeight = (s * rect.width + c * rect.height) * .5f;
	return center[0] + halfWidth > 0 && center[1] + halfHeight > 0 &&
	       center[0] - halfWidth < static_cast<float>(width) &&
	       center[1] - halfHeight < static_cast<float>(height);
}

bool CanvasLayout::Collect(const Scene &scene, int width, int height,
                           std::vector<UIElement> &elements, std::string &error,
                           SceneObjectId *invalidObject, bool skipIncomplete) {
	SceneObjectId current = 0;
	if (invalidObject)
		*invalidObject = 0;
	try {
		if (width < 1 || height < 1 || width > 16384 || height > 16384)
			throw std::runtime_error("UI target dimensions are invalid.");
		std::vector<SceneObjectId> order;
		SceneActivity::States active;
		if (!SceneHierarchy::GetEvaluationOrder(scene, order) ||
		    !SceneActivity::BuildStates(scene, active))
			throw std::runtime_error("UI parent hierarchy is invalid.");

		struct Context {
			SceneObjectId canvas = 0;
			UIRect rect;
			float scale = 1;
			UITransform transform;
			bool visible = true;
			int sort = 0;
		};

		struct Entry {
			UIElement element;
			int sort;
			std::size_t index;
		};

		std::map<SceneObjectId, Context> contexts;
		std::vector<Entry> result;

		for (std::size_t index = 0; index < order.size(); ++index) {
			const auto id = order[index];
			current = id;
			const auto &object = *scene.FindObject(id);
			auto context =
			    object.parentId ? contexts.at(object.parentId) : Context{};
			context.visible &= active.at(id);

			if (!context.visible) {
				contexts.emplace(id, context);
				continue;
			}

			if (object.canvas) {
				if (!object.canvas->IsValid() || object.rectTransform) {
					if (skipIncomplete) {
						context.visible = false;
						contexts.emplace(id, context);
						continue;
					}

					throw std::runtime_error(
					    "Canvas needs valid settings and cannot also have Rect "
					    "Transform.");
				}

				const auto &canvas = *object.canvas;
				context.visible &= canvas.enabled;
				context.canvas = id;
				context.sort = canvas.sortOrder;
				const double sx = double(width) / canvas.referenceSize[0],
				             sy = double(height) / canvas.referenceSize[1];
				context.scale =
				    canvas.scaleWithScreen
				        ? static_cast<float>(
				              std::pow(sx, 1 - canvas.matchWidthOrHeight) *
				              std::pow(sy, canvas.matchWidthOrHeight))
				        : 1;
				context.rect = {0, 0, float(width), float(height)};
				context.transform = {};
			}

			if (object.rectTransform) {
				if (!object.rectTransform->IsValid())
					throw std::runtime_error(
					    "Rect Transform settings are invalid.");
				if (context.canvas) {
					const auto &rect = *object.rectTransform;
					const auto parent = context.rect;
					const float spanX =
					    parent.width * (rect.anchorMax[0] - rect.anchorMin[0]);
					const float spanY =
					    parent.height * (rect.anchorMax[1] - rect.anchorMin[1]);
					const float w = spanX + rect.sizeDelta[0] * context.scale,
					            h = spanY + rect.sizeDelta[1] * context.scale;
					context.rect = {
					    parent.x + parent.width * rect.anchorMin[0] +
					        spanX * rect.pivot[0] +
					        rect.position[0] * context.scale -
					        w * rect.pivot[0],
					    parent.y + parent.height * rect.anchorMin[1] +
					        spanY * rect.pivot[1] +
					        rect.position[1] * context.scale -
					        h * rect.pivot[1],
					    w, h};
					if (rect.rotationDegrees != 0) {
						const float px = context.rect.x + w * .5f,
						            py = context.rect.y + h * .5f;
						const auto pivot = context.transform.ToScreen(px, py);
						const float radians = rect.rotationDegrees *
						                      (std::numbers::pi_v<float> / 180.f);
						const float c = std::cos(radians), s = std::sin(radians);
						const float parentC = context.transform.cosine,
						            parentS = context.transform.sine;
						const float cosine = parentC * c - parentS * s,
						            sine = parentS * c + parentC * s;
						context.transform = {
						    cosine, sine, pivot[0] - (cosine * px - sine * py),
						    pivot[1] - (sine * px + cosine * py)};
					}
				}
			}

			contexts.emplace(id, context);
			if (!context.visible)
				continue;
			const bool drawText = object.text && object.text->enabled,
			           drawImage = object.image && object.image->enabled;
			const bool button = object.button && object.button->enabled;
			if (!drawText && !drawImage && !button)
				continue;
			if ((object.text && !object.text->IsValid()) ||
			    (object.image && !object.image->IsValid()) ||
			    (object.button && !object.button->IsValid()))
				throw std::runtime_error(
				    "Text, Image or Button settings are invalid.");
			if (!object.rectTransform || !context.canvas) {
				if (skipIncomplete)
					continue;
				throw std::runtime_error("Text, Image and Button need Rect "
				                         "Transform and a parent Canvas.");
			}

			const auto &rect = context.rect;
			if (!std::isfinite(rect.x) || !std::isfinite(rect.y) ||
			    !std::isfinite(rect.width) || !std::isfinite(rect.height))
				throw std::runtime_error(
				    "Composed UI rectangle is not finite.");
			if (rect.width <= 0 || rect.height <= 0)
				continue;
			if (result.size() >= kMaximumElements) {
				if (skipIncomplete)
					continue;
				throw std::runtime_error(
				    "A scene can show at most 256 UI elements.");
			}

			result.push_back({{id, context.canvas, rect, context.scale, context.transform},
			                  context.sort,
			                  index});
		}

		std::ranges::sort(result, [](const Entry &a, const Entry &b) {
			return std::tie(a.sort, a.element.canvas, a.index) <
			       std::tie(b.sort, b.element.canvas, b.index);
		});
		std::vector<UIElement> candidate;
		candidate.reserve(result.size());
		for (const auto &entry : result)
			candidate.push_back(entry.element);
		elements.swap(candidate);
		error.clear();
		return true;
	} catch (const std::exception &exception) {
		error = std::format("UI object ID {}: {}", current, exception.what());
		if (invalidObject)
			*invalidObject = current;
		return false;
	}
}
