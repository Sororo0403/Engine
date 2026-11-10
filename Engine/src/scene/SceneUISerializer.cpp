#include "SceneUISerializer.h"
#include "animation/AnimationClipAsset.h"
#include "platform/windows/FileUtils.h"
#include "platform/windows/PathUtils.h"
#include "scene/SceneObject.h"
#include <array>
#include <cmath>
#include <fstream>
#include <stdexcept>

namespace {
using Json = nlohmann::json;
const std::array<const char *, 7> kTimelineProperties{
    "position", "opacity", "translation", "rotation", "scale", "sizeDelta",
    "uiRotation"};
const std::array<const char *, 5> kTimelineInterpolation{
    "linear", "smooth", "step", "easeOutCubic", "easeOutBack"};

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
	if ((object.animationClip && !object.animationClip->IsValid()) ||
	    (object.canvas && !object.canvas->IsValid()) ||
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

	if (object.animationClip && object.animationClip->usesAsset) {
		const auto &player = *object.animationClip;
		Json bindings = Json::array();
		for (const auto &[slot, target] : player.bindings)
			bindings.push_back({{"slot", slot}, {"target", target}});
		item["animationPlayer"] = {{"asset", PathUtils::ToUtf8(player.asset)},
		                           {"playOnStart", player.playOnStart},
		                           {"loop", player.loop},
		                           {"bindings", std::move(bindings)}};
	} else if (object.animationClip) {
		const auto &timeline = *object.animationClip;
		Json tracks = Json::array();
		for (const auto &track : timeline.tracks) {
			Json keys = Json::array();
			for (const auto &key : track.keys)
				keys.push_back({{"time", key.seconds}, {"value", key.value}});
			tracks.push_back({
			    {"target", track.target},
			    {"property", kTimelineProperties[static_cast<std::size_t>(track.property)]},
			    {"interpolation", kTimelineInterpolation[static_cast<std::size_t>(track.interpolation)]},
			    {"keys", std::move(keys)}});
		}
		item["animationClip"] = {{"duration", timeline.duration},
		                      {"playOnStart", timeline.playOnStart},
		                      {"loop", timeline.loop},
		                      {"tracks", std::move(tracks)}};
	}

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
		                         {"sizeDelta", r.sizeDelta},
		                         {"rotationDegrees", r.rotationDegrees}};
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

	const bool legacyTimeline = version == 70 && item.contains("uiTimeline");
	if (version >= 73 && item.contains("animationPlayer")) {
		const auto &value = item.at("animationPlayer");
		const auto &bindings = value.at("bindings");
		if (item.contains("animationClip") || !bindings.is_array() ||
		    bindings.size() > AnimationClipComponent::kMaximumTracks)
			throw std::runtime_error("Animation Player fields are invalid.");

		AnimationClipComponent player;
		player.usesAsset = true;
		player.asset = PathUtils::FromUtf8(value.at("asset").get<std::string>());
		player.playOnStart = value.at("playOnStart").get<bool>();
		player.loop = value.at("loop").get<bool>();
		for (const auto &binding : bindings) {
			if (!binding.at("slot").is_number_unsigned() ||
			    !binding.at("target").is_number_unsigned())
				throw std::runtime_error("Animation Player binding is invalid.");
			player.bindings.emplace_back(binding.at("slot").get<SceneObjectId>(),
			                             binding.at("target").get<SceneObjectId>());
		}
		object.animationClip = std::move(player);
	} else if (legacyTimeline || (version >= 71 && item.contains("animationClip"))) {
		const auto &value = item.at(legacyTimeline ? "uiTimeline" : "animationClip");
		const auto &tracks = value.at("tracks");
		if (!value.at("duration").is_number() || !tracks.is_array() ||
		    tracks.size() > AnimationClipComponent::kMaximumTracks)
			throw std::runtime_error("Animation Clip fields are invalid.");

		AnimationClipComponent timeline;
		timeline.duration = value.at("duration").get<float>();
		timeline.playOnStart = value.at("playOnStart").get<bool>();
		timeline.loop = version >= 72 && value.value("loop", false);
		for (const auto &entry : tracks) {
			const auto &keys = entry.at("keys");
			if (!entry.at("target").is_number_unsigned() || !keys.is_array() ||
			    keys.size() > AnimationClipComponent::kMaximumKeys)
				throw std::runtime_error("Animation Clip track is invalid.");

			AnimationClipTrack track;
			track.target = entry.at("target").get<SceneObjectId>();
			track.property = static_cast<AnimationClipTrack::Property>(
			    Choice(entry.at("property"), kTimelineProperties));
			track.interpolation = static_cast<AnimationClipTrack::Interpolation>(
			    Choice(entry.at("interpolation"), kTimelineInterpolation));
			track.keys.clear();
			for (const auto &key : keys) {
				if (!key.at("time").is_number())
					throw std::runtime_error("Animation Clip time must be numeric.");
				std::array<float, 4> sample{};
				if (legacyTimeline) {
					const auto pair = ReadArray<2>(key.at("value"));
					sample = {pair[0], pair[1], 0, 0};
				} else {
					sample = ReadArray<4>(key.at("value"));
				}
				track.keys.push_back({key.at("time").get<float>(), sample});
			}
			timeline.tracks.push_back(std::move(track));
		}
		object.animationClip = std::move(timeline);
	}

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
		if (v.contains("rotationDegrees")) {
			if (!v.at("rotationDegrees").is_number())
				throw std::runtime_error("UI rotation must be numeric.");
			r.rotationDegrees = v.at("rotationDegrees").get<float>();
		}
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

namespace {
bool ValidClipAsset(const AnimationClipComponent &clip) {
	return !clip.usesAsset && clip.IsValid() &&
	       std::ranges::all_of(clip.tracks, [](const auto &track) {
		       return track.target > 0 &&
		              track.target <= AnimationClipComponent::kMaximumTracks;
	       });
}
} // namespace

bool AnimationClipAsset::IsPath(const std::filesystem::path &path) {
	return PathUtils::HasExtension(path, ".json") &&
	       PathUtils::HasExtension(path.stem(), ".clip");
}

bool AnimationClipAsset::Load(const std::filesystem::path &path,
                              AnimationClipComponent &clip, std::string &error) {
	try {
		if (!IsPath(path))
			throw std::runtime_error("Choose an Animation Clip (.clip.json).");
		const auto file = PathUtils::ResolveFromExecutable(path);
		constexpr std::uintmax_t kMaximumBytes = 1024 * 1024;
		const auto size = std::filesystem::file_size(file);
		if (size > kMaximumBytes)
			throw std::runtime_error("Animation Clip is too large.");
		std::ifstream input(file, std::ios::binary);
		std::string content(static_cast<std::size_t>(size), '\0');
		if (!input.read(content.data(), static_cast<std::streamsize>(size)) ||
		    input.peek() != std::char_traits<char>::eof())
			throw std::runtime_error("Could not read Animation Clip.");

		const auto document = Json::parse(content);
		if (document.at("format") != "EngineAnimationClip" ||
		    document.at("version") != 1)
			throw std::runtime_error("Unsupported Animation Clip asset format.");
		auto value = document.at("clip");
		value["playOnStart"] = false;
		value["loop"] = false;
		SceneObject object;
		SceneUISerializer::Read({{"animationClip", std::move(value)}},
		                        object, 72);
		if (!object.animationClip || !ValidClipAsset(*object.animationClip))
			throw std::runtime_error("Animation Clip has invalid keys or slots.");
		clip = std::move(*object.animationClip);
		error.clear();
		return true;
	} catch (const std::exception &e) {
		error = e.what();
		return false;
	}
}

bool AnimationClipAsset::Save(const std::filesystem::path &path,
                              const AnimationClipComponent &clip,
                              std::string &error, bool overwrite) {
	try {
		if (!IsPath(path) || !ValidClipAsset(clip))
			throw std::runtime_error("Animation Clip has an invalid filename, keys or slots.");

		SceneObject object;
		object.animationClip = clip;
		Json encoded;
		SceneUISerializer::Write(encoded, object);
		auto &value = encoded.at("animationClip");
		value.erase("playOnStart");
		value.erase("loop");
		auto content = Json{{"format", "EngineAnimationClip"}, {"version", 1},
		                    {"clip", std::move(value)}}.dump(2);
		content += '\n';
		return FileUtils::WriteTextAtomically(path, content, error, overwrite);
	} catch (const std::exception &e) {
		error = e.what();
		return false;
	}
}

bool AnimationClipAsset::Resolve(const AnimationClipComponent &player,
                                 AnimationClipComponent &clip, std::string &error) {
	if (!player.usesAsset || !player.IsValid()) {
		error = "Animation Player settings are invalid.";
		return false;
	}

	AnimationClipComponent loaded;
	if (!Load(player.asset, loaded, error))
		return false;
	for (auto &track : loaded.tracks) {
		const auto binding = std::ranges::find_if(player.bindings, [&](const auto &entry) {
			return entry.first == track.target;
		});
		if (binding == player.bindings.end() || binding->second == 0) {
			error = "Assign an object to each Animation Player binding slot.";
			return false;
		}
		track.target = binding->second;
	}
	if (!loaded.IsValid()) {
		error = "Animation Player bindings animate the same property more than once.";
		return false;
	}

	loaded.playOnStart = player.playOnStart;
	loaded.loop = player.loop;
	clip = std::move(loaded);
	error.clear();
	return true;
}
