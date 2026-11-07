#include "SceneUISerializer.h"
#include "platform/windows/PathUtils.h"
#include "scene/SceneObject.h"
#include <array>
#include <cmath>
#include <stdexcept>

namespace {
using Json = nlohmann::json;

/// <summary>
/// 有限な固定長のUI配列を読み込む
/// </summary>
/// <typeparam name="N">成分数</typeparam>
/// <param name="value">数値だけを持つJSON配列</param>
/// <returns>読み取った値。無効な入力は例外</returns>
template <std::size_t N> std::array<float, N> ReadArray(const Json &value) {
	if (!value.is_array() || value.size() != N)
		throw std::runtime_error("UI array dimensions are invalid.");
	std::array<float, N> result;
	for (std::size_t i = 0; i < N; ++i) {
		if (!value.at(i).is_number())
			throw std::runtime_error("UI array needs numbers.");
		result[i] = value.at(i).get<float>();
		if (!std::isfinite(result[i]))
			throw std::runtime_error("UI number is not finite.");
	}
	return result;
}

/// <summary>
/// UIの列挙設定を対応する文字列から読み込む
/// </summary>
/// <typeparam name="N">候補数</typeparam>
/// <param name="value">文字列のJSON</param>
/// <param name="names">列挙値の順に並べた候補</param>
/// <returns>対応する添字。未対応の値は例外</returns>
template <std::size_t N>
std::size_t Choice(const Json &value,
                   const std::array<const char *, N> &names) {
	const auto text = value.get<std::string>();
	for (std::size_t i = 0; i < names.size(); ++i)
		if (text == names[i])
			return i;
	throw std::runtime_error("UI choice is invalid.");
}

/// <summary>
/// 任意UIコンポーネントの値だけを確認する
/// </summary>
/// <param name="object">確認する物体</param>
void Validate(const SceneObject &object) {
	if ((object.canvas && !object.canvas->IsValid()) ||
	    (object.rectTransform && !object.rectTransform->IsValid()) ||
	    (object.text && !object.text->IsValid()) ||
	    (object.image && !object.image->IsValid()) ||
	    (object.button && !object.button->IsValid()))
		throw std::runtime_error("UI component settings are invalid.");
}

const std::array<const char *, 3> kHorizontal{"left", "center", "right"},
    kVertical{"top", "middle", "bottom"};
const std::array<const char *, 4> kFillOrigins{"left", "right", "top",
                                               "bottom"};
} // namespace

void SceneUISerializer::Write(nlohmann::json &item, const SceneObject &object) {
	Validate(object);
	if (object.canvas) {
		const auto &c = *object.canvas;
		item["canvas"] = {{"enabled", c.enabled},
		                  {"scaleWithScreen", c.scaleWithScreen},
		                  {"referenceSize", c.referenceSize},
		                  {"matchWidthOrHeight", c.matchWidthOrHeight},
		                  {"sortOrder", c.sortOrder}};
	}
	if (object.rectTransform) {
		const auto &r = *object.rectTransform;
		item["rectTransform"] = {{"anchorMin", r.anchorMin},
		                         {"anchorMax", r.anchorMax},
		                         {"pivot", r.pivot},
		                         {"position", r.position},
		                         {"sizeDelta", r.sizeDelta}};
	}
	if (object.text) {
		const auto &t = *object.text;
		item["text"] = {
		    {"enabled", t.enabled},
		    {"raycastTarget", t.raycastTarget},
		    {"value", t.text},
		    {"fontFamily", t.fontFamily},
		    {"fontSize", t.fontSize},
		    {"bold", t.bold},
		    {"alignment", kHorizontal[static_cast<std::size_t>(t.alignment)]},
		    {"verticalAlignment",
		     kVertical[static_cast<std::size_t>(t.verticalAlignment)]},
		    {"color", t.color}};
	}
	if (object.image) {
		const auto &i = *object.image;
		item["image"] = {{"enabled", i.enabled},
		                 {"raycastTarget", i.raycastTarget},
		                 {"texture", PathUtils::ToUtf8(i.texture)},
		                 {"color", i.color},
		                 {"preserveAspect", i.preserveAspect},
		                 {"fillOrigin",
		                  kFillOrigins[static_cast<std::size_t>(i.fillOrigin)]},
		                 {"fillAmount", i.fillAmount}};
	}
	if (object.button) {
		const auto &b = *object.button;
		item["button"] = {{"enabled", b.enabled},
		                  {"interactable", b.interactable},
		                  {"navigation", b.navigation},
		                  {"normalColor", b.normalColor},
		                  {"highlightedColor", b.highlightedColor},
		                  {"pressedColor", b.pressedColor},
		                  {"disabledColor", b.disabledColor}};
	}
}

void SceneUISerializer::Read(const nlohmann::json &item, SceneObject &object,
                             int version) {
	if (version < 30)
		return;
	if (item.contains("canvas")) {
		const auto &v = item.at("canvas");
		CanvasComponent c;
		if (!v.at("matchWidthOrHeight").is_number() ||
		    !v.at("sortOrder").is_number_integer() ||
		    v.at("sortOrder").get<double>() < -10000 ||
		    v.at("sortOrder").get<double>() > 10000)
			throw std::runtime_error("Canvas numeric fields are invalid.");
		c.enabled = v.at("enabled").get<bool>();
		c.scaleWithScreen = v.at("scaleWithScreen").get<bool>();
		c.referenceSize = ReadArray<2>(v.at("referenceSize"));
		c.matchWidthOrHeight = v.at("matchWidthOrHeight").get<float>();
		c.sortOrder = v.at("sortOrder").get<int>();
		object.canvas = c;
	}
	if (item.contains("rectTransform")) {
		const auto &v = item.at("rectTransform");
		RectTransformComponent r;
		r.anchorMin = ReadArray<2>(v.at("anchorMin"));
		r.anchorMax = ReadArray<2>(v.at("anchorMax"));
		r.pivot = ReadArray<2>(v.at("pivot"));
		r.position = ReadArray<2>(v.at("position"));
		r.sizeDelta = ReadArray<2>(v.at("sizeDelta"));
		object.rectTransform = r;
	}
	if (item.contains("text")) {
		const auto &v = item.at("text");
		TextComponent t;
		if (!v.at("fontSize").is_number())
			throw std::runtime_error("Text font size needs a number.");
		t.enabled = v.at("enabled").get<bool>();
		if (version >= 31)
			t.raycastTarget = v.at("raycastTarget").get<bool>();
		t.text = v.at("value").get<std::string>();
		t.fontFamily = v.at("fontFamily").get<std::string>();
		t.fontSize = v.at("fontSize").get<float>();
		t.bold = v.at("bold").get<bool>();
		t.alignment = static_cast<TextComponent::Alignment>(
		    Choice(v.at("alignment"), kHorizontal));
		t.verticalAlignment = static_cast<TextComponent::VerticalAlignment>(
		    Choice(v.at("verticalAlignment"), kVertical));
		t.color = ReadArray<4>(v.at("color"));
		object.text = std::move(t);
	}
	if (item.contains("image")) {
		const auto &v = item.at("image");
		ImageComponent i;
		i.enabled = v.at("enabled").get<bool>();
		if (version >= 31)
			i.raycastTarget = v.at("raycastTarget").get<bool>();
		i.texture = PathUtils::FromUtf8(v.at("texture").get<std::string>());
		i.color = ReadArray<4>(v.at("color"));
		i.preserveAspect = v.at("preserveAspect").get<bool>();
		if (version >= 55) {
			i.fillOrigin = static_cast<ImageComponent::FillOrigin>(
			    Choice(v.at("fillOrigin"), kFillOrigins));
			if (!v.at("fillAmount").is_number())
				throw std::runtime_error("UI image fill needs a number.");
			i.fillAmount = v.at("fillAmount").get<float>();
		}
		object.image = std::move(i);
	}
	if (version >= 31 && item.contains("button")) {
		const auto &v = item.at("button");
		ButtonComponent b;
		b.enabled = v.at("enabled").get<bool>();
		b.interactable = v.at("interactable").get<bool>();
		b.navigation = v.at("navigation").get<bool>();
		b.normalColor = ReadArray<4>(v.at("normalColor"));
		b.highlightedColor = ReadArray<4>(v.at("highlightedColor"));
		b.pressedColor = ReadArray<4>(v.at("pressedColor"));
		b.disabledColor = ReadArray<4>(v.at("disabledColor"));
		object.button = b;
	}
	Validate(object);
}
