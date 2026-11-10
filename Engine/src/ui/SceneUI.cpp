#include "ui/SceneUI.h"
#include "scene/Scene.h"
#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <stdexcept>

namespace {
/// <summary>
/// 指定したボタンの、描画順に並ぶ現在の矩形を探す
/// </summary>
/// <param name="elements">有効なCanvas配下の要素</param>
/// <param name="id">探すID</param>
/// <returns>配置。見つからない場合はnullptr</returns>
const UIElement *Element(std::span<const UIElement> elements,
                         SceneObjectId id) {
	const auto found = std::ranges::find_if(
	    elements, [id](const auto &e) { return e.object == id; });
	return found == elements.end() ? nullptr : &*found;
}

/// <summary>
/// 表示中で有効なボタンか確認する
/// </summary>
/// <param name="scene">ゲームシーン</param>
/// <param name="elements">現在の配置</param>
/// <param name="input">表示寸法</param>
/// <param name="id">対象物体</param>
/// <param name="navigation">true:キーボード・パッド選択も必要</param>
/// <returns>true:操作可能 false:無効・非表示・対象なし</returns>
bool Usable(const Scene &scene, std::span<const UIElement> elements,
            const UIInput &input, SceneObjectId id, bool navigation = false) {
	const auto *object = scene.FindObject(id);
	const auto *element = Element(elements, id);
	return object && object->button && object->button->enabled &&
	       object->button->interactable &&
	       (!navigation || object->button->navigation) && element &&
	       element->IntersectsViewport(input.width, input.height);
}

struct Hit {
	bool blocked = false;
	SceneObjectId button = 0;
};

/// <summary>
/// 最前面の対象を探し、子の文字や画像への操作を最も近い祖先Buttonへ渡す
/// </summary>
/// <param name="scene">変更しないシーン</param>
/// <param name="elements">描画順の配置</param>
/// <param name="input">正規化したポインター</param>
/// <returns>遮断の有無と最初のButton。無効なButtonも背後への操作を遮る</returns>
Hit PointerHit(const Scene &scene, std::span<const UIElement> elements,
               const UIInput &input) {
	if (!input.enabled || !input.pointerValid)
		return {};
	const float x = input.pointerX * static_cast<float>(input.width),
	            y = input.pointerY * static_cast<float>(input.height);
	for (auto i = elements.rbegin(); i != elements.rend(); ++i) {
		const auto &r = i->rect;
		const auto [px, py] = i->transform.ToLayout(x, y);
		if (px < r.x || py < r.y || px >= r.x + r.width || py >= r.y + r.height)
			continue;
		const auto *object = scene.FindObject(i->object);
		const bool target = (object->image && object->image->enabled &&
		                     object->image->raycastTarget) ||
		                    (object->text && object->text->enabled &&
		                     object->text->raycastTarget) ||
		                    (object->button && object->button->enabled);
		if (!target)
			continue;
		auto id = i->object;

		while (id) {
			const auto &candidate = *scene.FindObject(id);
			if (candidate.button)
				return {true, id};
			if (id == i->canvas)
				break;
			id = candidate.parentId;
		}

		return {true, 0};
	}

	return {};
}

/// <summary>
/// 押下後に表示矩形が変わっていないか確認する
/// </summary>
/// <param name="a">押下時の矩形</param>
/// <param name="b">現在の矩形</param>
/// <returns>true:同一 false:移動・サイズ変更あり</returns>
bool SameRect(const UIRect &a, const UIRect &b) {
	return a.x == b.x && a.y == b.y && a.width == b.width &&
	       a.height == b.height;
}
} // namespace

void SceneUI::Cancel() noexcept {
	selected_ = pressed_ = hovered_ = pressedParent_ = pressedCanvas_ = 0;
	pointerSequence_ = keyboardMode_ = pointerArmed_ = submitArmed_ = false;
	consumesPointer_ = consumesNavigation_ = false;
	last_ = UIInput{};
	clicks_.clear();
	visuals_.clear();
}

void SceneUI::Synchronize(const Scene &scene,
                          std::span<const UIElement> elements) {
	if (selected_ && !Usable(scene, elements, last_, selected_, true)) {
		selected_ = 0;
		keyboardMode_ = false;
	}

	if (pressed_) {
		const auto *element = Element(elements, pressed_);
		const auto *object = scene.FindObject(pressed_);
		if (!Usable(scene, elements, last_, pressed_) ||
		    object->parentId != pressedParent_ ||
		    element->canvas != pressedCanvas_ ||
		    !SameRect(element->rect, pressedRect_) ||
		    element->transform != pressedTransform_)
			pressed_ = 0;
	}

	const auto hit = PointerHit(scene, elements, last_);
	hovered_ = Usable(scene, elements, last_, hit.button) ? hit.button : 0;
	if (pressed_ && hovered_ != pressed_)
		pressed_ = 0;
	std::vector<UIButtonVisual> visuals;

	for (const auto &element : elements) {
		const auto &object = *scene.FindObject(element.object);
		if (!object.button)
			continue;
		const auto &button = *object.button;
		auto state = UIButtonVisual::State::NORMAL;
		if (!button.enabled || !button.interactable)
			state = UIButtonVisual::State::DISABLED;
		else if ((pressed_ == element.object && last_.pointerDown) ||
		         (last_.submitPressed && last_.navigationEnabled &&
		          keyboardMode_ && selected_ == element.object &&
		          std::ranges::find(clicks_, element.object) != clicks_.end()))
			state = UIButtonVisual::State::PRESSED;
		else if ((keyboardMode_ && selected_ == element.object) ||
		         (!keyboardMode_ && hovered_ == element.object))
			state = UIButtonVisual::State::HIGHLIGHTED;
		visuals.push_back({element.object, state});
	}

	visuals_.swap(visuals);
}

bool SceneUI::Refresh(const Scene &scene, std::string &error) {
	try {
		std::vector<UIElement> elements;
		if (!CanvasLayout::Collect(scene, last_.width, last_.height, elements,
		                           error))
			return false;
		auto next = *this;
		next.Synchronize(scene, elements);
		*this = std::move(next);
		error.clear();
		return true;
	} catch (const std::exception &exception) {
		error = exception.what();
		return false;
	}
}

bool SceneUI::Update(const Scene &scene, const UIInput &input,
                     std::string &error) {
	try {
		if (!input.IsValid())
			throw std::runtime_error(
			    "UI input dimensions, position or navigation are invalid.");
		auto next = *this;
		next.clicks_.clear();
		next.consumesPointer_ = next.consumesNavigation_ = false;

		if (!input.enabled) {
			next.Cancel();
			*this = std::move(next);
			error.clear();
			return true;
		}

		std::vector<UIElement> elements;
		if (!CanvasLayout::Collect(scene, input.width, input.height, elements,
		                           error))
			return false;
		const bool released = last_.pointerDown && !input.pointerDown;
		const bool sequence = next.pointerSequence_;
		if (input.pointerValid &&
		    (!last_.pointerValid || input.pointerX != last_.pointerX ||
		     input.pointerY != last_.pointerY))
			next.keyboardMode_ = false;
		next.last_ = input;
		next.Synchronize(scene, elements);
		const auto hit = PointerHit(scene, elements, input);
		const auto click = [&](SceneObjectId id) {
			if (Usable(scene, elements, input, id) &&
			    std::ranges::find(next.clicks_, id) == next.clicks_.end())
				next.clicks_.push_back(id);
		};
		const bool pointerReady = next.pointerArmed_;
		if (!input.pointerDown)
			next.pointerArmed_ = true;
		if (input.pointerPressed && pointerReady) {
			next.keyboardMode_ = false;
			next.pressed_ = 0;
			next.pointerSequence_ = hit.blocked;

			if (Usable(scene, elements, input, hit.button)) {
				next.pressed_ = hit.button;
				next.selected_ = hit.button;
				next.pressedParent_ = scene.FindObject(hit.button)->parentId;
				const auto &element = *Element(elements, hit.button);
				next.pressedRect_ = element.rect;
				next.pressedTransform_ = element.transform;
				next.pressedCanvas_ = element.canvas;

				if (!input.pointerDown) {
					click(hit.button);
					next.pressed_ = 0;
				}
			}
		}

		if (released) {
			if (next.pressed_ && next.pressed_ == hit.button)
				click(next.pressed_);
			next.pressed_ = 0;
		}

		next.consumesPointer_ =
		    hit.blocked || sequence || next.pointerSequence_;
		if (!input.pointerDown)
			next.pointerSequence_ = false;
		std::vector<SceneObjectId> buttons;
		for (const auto &element : elements)
			if (Usable(scene, elements, input, element.object, true))
				buttons.push_back(element.object);
		if (!input.navigationEnabled) {
			next.selected_ = 0;
			next.keyboardMode_ = false;
			next.submitArmed_ = false;
		} else {
			const bool submitReady = next.submitArmed_;
			if (!input.submitDown)
				next.submitArmed_ = true;
			const int step = int(input.next) - int(input.previous);

			if (!buttons.empty() && (step || input.horizontal ||
			                         input.vertical || input.submitPressed)) {
				next.keyboardMode_ = true;
				next.pressed_ = 0;
				const auto selected =
				    std::ranges::find(buttons, next.selected_);
				if (selected == buttons.end())
					next.selected_ =
					    step < 0 ? buttons.back() : buttons.front();
				else if (step) {
					const auto index =
					    static_cast<std::ptrdiff_t>(selected - buttons.begin());
					const auto count =
					    static_cast<std::ptrdiff_t>(buttons.size());
					next.selected_ = buttons[static_cast<std::size_t>(
					    (index + step + count) % count)];
				} else if (input.horizontal || input.vertical) {
					const auto &r = Element(elements, next.selected_)->rect;
					const double x = r.x + r.width * .5,
					             y = r.y + r.height * .5;
					double nearest = (std::numeric_limits<double>::max)();
					SceneObjectId best = next.selected_;

					for (auto id : buttons) {
						if (id == next.selected_)
							continue;
						const auto &other = Element(elements, id)->rect;
						const double dx = other.x + other.width * .5 - x,
						             dy = other.y + other.height * .5 - y;
						const double forward = input.horizontal
						                           ? dx * input.horizontal
						                           : dy * input.vertical,
						             cross = input.horizontal ? dy : dx;
						const double score =
						    forward * forward + cross * cross * 4;
						if (forward > .001 && score < nearest) {
							nearest = score;
							best = id;
						}
					}

					next.selected_ = best;
				}

				if (input.submitPressed && submitReady)
					click(next.selected_);
			}

			next.consumesNavigation_ =
			    (!buttons.empty() && input.submitDown && !submitReady) ||
			    (next.keyboardMode_ && next.selected_ != 0);
		}

		next.Synchronize(scene, elements);

		if (input.submitPressed && input.navigationEnabled &&
		    next.submitArmed_ && next.keyboardMode_ && next.selected_) {
			for (auto &visual : next.visuals_)
				if (visual.object == next.selected_ &&
				    std::ranges::find(next.clicks_, next.selected_) !=
				        next.clicks_.end())
					visual.state = UIButtonVisual::State::PRESSED;
		}

		*this = std::move(next);
		error.clear();
		return true;
	} catch (const std::exception &exception) {
		error = exception.what();
		return false;
	}
}
