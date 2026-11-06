#include "ui/UIObjects.h"
#include "history/EditorHistory.h"
#include "scene/Scene.h"
#include "scene/SceneActivity.h"
#include <exception>
#include <stdexcept>

SceneObjectId UIObjects::Create(const Scene &scene, EditorHistory &history,
                                Kind kind, SceneObjectId parent,
                                std::string &error) {
	try {
		if (history.IsEditing())
			throw std::runtime_error(
			    "Finish the current edit before creating UI.");
		if (kind != Kind::CANVAS && kind != Kind::TEXT && kind != Kind::IMAGE &&
		    kind != Kind::BUTTON)
			throw std::runtime_error("Invalid UI object kind.");
		SceneObjectId selectedParent = 0;
		if (kind != Kind::CANVAS) {
			auto ancestor = parent;
			std::size_t depth = 0;
			while (ancestor) {
				const auto *object = scene.FindObject(ancestor);
				if (!object || ++depth > scene.GetObjects().size())
					throw std::runtime_error("UI parent hierarchy is invalid.");
				if (object->canvas) {
					selectedParent = parent;
					break;
				}
				ancestor = object->parentId;
			}
			if (!selectedParent)
				for (const auto &[id, object] : scene.GetObjects())
					if (object.canvas && object.canvas->enabled &&
					    SceneActivity::IsActive(scene, id)) {
						selectedParent = id;
						break;
					}
		}
		SceneObjectId created = 0;
		const bool applied = history.ApplySceneChanges(
		    [&](Scene &candidate) {
			    if (kind == Kind::CANVAS || !selectedParent) {
				    auto id = candidate.CreateObject("Canvas");
				    if (!id)
					    return false;
				    candidate.FindObject(id)->canvas = CanvasComponent{};
				    selectedParent = id;
				    if (kind == Kind::CANVAS) {
					    created = id;
					    return true;
				    }
			    }
			    created =
			        candidate.CreateObject(kind == Kind::BUTTON ? "Button"
			                               : kind == Kind::TEXT ? "Text"
			                                                    : "Image");
			    if (!created)
				    return false;
			    auto &object = *candidate.FindObject(created);
			    object.parentId = selectedParent;
			    object.rectTransform = RectTransformComponent{};
			    if (kind == Kind::BUTTON) {
				    object.button = ButtonComponent{};
				    object.image = ImageComponent{};
				    object.image->color = {.22f, .25f, .3f, 1};
				    object.text = TextComponent{};
				    object.text->text = "Button";
				    object.text->alignment = TextComponent::Alignment::CENTER;
				    object.text->verticalAlignment =
				        TextComponent::VerticalAlignment::MIDDLE;
				    object.text->raycastTarget = false;
			    } else if (kind == Kind::TEXT)
				    object.text = TextComponent{};
			    else
				    object.image = ImageComponent{};
			    return true;
		    },
		    kind == Kind::CANVAS   ? "Create Canvas"
		    : kind == Kind::TEXT   ? "Create UI Text"
		    : kind == Kind::BUTTON ? "Create UI Button"
		                           : "Create UI Image");
		if (!applied)
			throw std::runtime_error(
			    "Could not create the UI object. The scene was kept.");
		error.clear();
		return created;
	} catch (const std::exception &exception) {
		error = exception.what();
		return 0;
	}
}
