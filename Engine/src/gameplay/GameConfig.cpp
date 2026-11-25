#include "gameplay/GameConfig.h"
#include "graphics/camera/Camera.h"
#include "platform/windows/FileUtils.h"
#include "platform/windows/PathUtils.h"
#include "platform/windows/StringConversion.h"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <json.hpp>
#include <memory>
#include <stdexcept>
#include <utility>

namespace {
/// <summary>
/// 設定に登録されたキーから、保存可能な単一の値だけを取り出す
/// </summary>
std::string SelectPersistentValues(const nlohmann::json &data,
                                 const std::vector<std::string> &keys) {
	auto settings = nlohmann::json::object();
	if (data.is_object()) {
		for (const auto &key : keys) {
			const auto value = data.find(key);
			if (value == data.end())
				continue;
			if (value->is_boolean() || value->is_number() ||
			    (value->is_string() &&
			     value->get_ref<const std::string &>().size() <= 256)) {
				settings[key] = *value;
			}
		}
	}
	return settings.dump();
}

bool IsSettingsName(std::string_view name) {
	return !name.empty() && name.size() <= 64 &&
	       std::ranges::all_of(name, [](char c) {
		       return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
		              (c >= '0' && c <= '9') || c == '-' || c == '_';
	       });
}

/// <summary>
/// JSONから有限な3成分の数値を読み込む
/// </summary>
/// <param name="value">数値の配列</param>
/// <returns>読み込んだ値。型が無効なら例外</returns>
DirectX::XMFLOAT3 ReadVector(const nlohmann::json &value) {
	if (!value.is_array() || value.size() != 3) {
		throw std::runtime_error("Camera vectors need three numbers.");
	}

	for (const auto &v : value) {
		if (!v.is_number()) {
			throw std::runtime_error("Camera values must be numbers.");
		}
	}

	return {value[0].get<float>(), value[1].get<float>(),
	        value[2].get<float>()};
}
} // namespace

std::filesystem::path GameConfig::GetScriptSettingsPath(bool editorTest) const {
	if (!IsSettingsName(settingsId)) {
		return {};
	}

	wchar_t *localAppData = nullptr;
	const auto result = _wdupenv_s(&localAppData, nullptr, L"LOCALAPPDATA");
	const std::unique_ptr<wchar_t, decltype(&std::free)> directory(localAppData, &std::free);
	if (result != 0 || !directory) {
		return {};
	}

	const std::filesystem::path root(directory.get());
	if (!root.is_absolute()) {
		return {};
	}
	return root / L"Engine" / (editorTest ? L"EditorTestGameSettings" : L"GameSettings") /
	       ("game-" + settingsId + ".json");
}

bool GameConfig::LoadScriptSettings(const std::filesystem::path &path, std::string &settings,
                                    std::string &error) const {
	constexpr std::size_t kMaxScriptSettingsBytes = 32768;
	error.clear();
	const auto fail = [&](std::string_view reason) {
		error = "Could not load game script settings: " + PathUtils::ToUtf8(path) +
		        "\n" + std::string(reason);
		return false;
	};

	std::ifstream file(path, std::ios::binary);
	if (!file) {
		std::error_code status;
		if (std::filesystem::symlink_status(path, status).type() == std::filesystem::file_type::not_found &&
		    (!status || status == std::errc::no_such_file_or_directory)) {
			settings = "{}";
			return true;
		}
		return fail("The file could not be opened.");
	}

	std::string bytes(kMaxScriptSettingsBytes + 1, '\0');
	file.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
	if (file.bad() || (!file && !file.eof())) {
		return fail("The file could not be read.");
	}
	if (file.gcount() > static_cast<std::streamsize>(kMaxScriptSettingsBytes)) {
		return fail("The file exceeds the size limit.");
	}
	bytes.resize(static_cast<std::size_t>(file.gcount()));
	const auto data = nlohmann::json::parse(bytes, nullptr, false);
	if (!data.is_object()) {
		return fail("The file must contain a valid JSON object.");
	}

	settings = SelectPersistentValues(data, persistentScriptKeys);
	return true;
}

std::string GameConfig::SelectScriptSettings(std::string_view data) const {
	return SelectPersistentValues(nlohmann::json::parse(data, nullptr, false), persistentScriptKeys);
}

bool GameConfig::IsValid() const {
	return HasValidWindowSettings() && HasValidStartupSettings();
}

bool GameConfig::HasValidWindowSettings() const {
	return HasValidWindowTitle() && IsValidWindowDimension(width) &&
	       IsValidWindowDimension(height);
}

bool GameConfig::HasValidWindowTitle() const {
	return !title.empty() && title.find('\0') == std::string::npos &&
	       !StringConversion::Utf8ToWide(title).empty();
}

bool GameConfig::IsValidWindowDimension(int value) {
	return value >= 1 && value <= 8192;
}

bool GameConfig::HasValidStartupSettings() const {
	if (!lighting.IsValid() || !input.IsValid() ||
	    !GameActionBinding::IsValid(actions) || scenePath.empty() ||
	    scenePath.has_root_path() ||
	    scenePath.native().find(L'\0') != std::wstring::npos ||
	    !PathUtils::HasExtension(scenePath, ".json")) {
		return false;
	}

	if (settingsId.empty() != persistentScriptKeys.empty() ||
	    (!settingsId.empty() && !IsSettingsName(settingsId)) ||
	    persistentScriptKeys.size() > 32 ||
	    !std::ranges::all_of(persistentScriptKeys, IsSettingsName)) {
		return false;
	}

	for (const auto &part : scenePath) {
		if (part == L".." || part.native().find(L':') != std::wstring::npos) {
			return false;
		}
	}

	Camera camera;
	return camera.SetPose(fallbackCamera);
}

bool GameConfig::Serialize(std::string &text, std::string &error) const {
	try {
		error.clear();

		if (!IsValid()) {
			throw std::runtime_error("Game startup settings are invalid.");
		}

		auto vector = [](const auto &v) {
			return nlohmann::json::array({v.x, v.y, v.z});
		};
		nlohmann::json inputJson{{"arrowMovement", input.arrowMovement}};

		for (const auto &action : GameInputBindings::kActions) {
			inputJson[action.name] =
			    GameInputBindings::GetName(input.*action.member);
		}

		const auto gamepadButton = [](const auto &button) -> nlohmann::json {
			if (!button)
				return nullptr;
			return GameActionBinding::kGamepadOptions[static_cast<std::size_t>(*button)].name;
		};
		inputJson["moveStick"] = input.moveStick == GameInputBindings::MoveStick::LEFT ? "Left"
		    : input.moveStick == GameInputBindings::MoveStick::RIGHT ? "Right" : "None";
		inputJson["dpadMovement"] = input.dpadMovement;
		inputJson["gamepadJump"] = gamepadButton(input.gamepadJump);
		inputJson["gamepadInteract"] = gamepadButton(input.gamepadInteract);

		const auto mouseButton = [](const auto &button) -> nlohmann::json {
			return button ? nlohmann::json(GameActionBinding::kMouseOptions[static_cast<std::size_t>(*button)].name)
			              : nlohmann::json(nullptr);
		};
		const auto uiKey = [](const auto &key) -> nlohmann::json {
			return key ? nlohmann::json(GameActionBinding::GetKeyName(*key)) : nlohmann::json(nullptr);
		};
		const auto &camera = input.camera;
		inputJson["camera"] = {
		    {"stick", camera.stick == GameInputBindings::MoveStick::LEFT ? "Left"
		        : camera.stick == GameInputBindings::MoveStick::RIGHT ? "Right" : "None"},
		    {"rotate", mouseButton(camera.rotate)}, {"mouseReset", mouseButton(camera.mouseReset)},
		    {"padReset", gamepadButton(camera.padReset)}, {"wheelZoom", camera.wheelZoom}};
		auto uiKeys = nlohmann::json::array();
		auto uiButtons = nlohmann::json::array();
		for (std::size_t i = 0; i < GameInputBindings::UI::COUNT; ++i) {
			uiKeys.push_back(uiKey(input.ui.keys[i]));
			uiButtons.push_back(gamepadButton(input.ui.buttons[i]));
		}
		inputJson["ui"] = {
		    {"keys", std::move(uiKeys)}, {"buttons", std::move(uiButtons)},
		    {"next", uiKey(input.ui.next)}, {"alternateSubmit", uiKey(input.ui.alternateSubmit)},
		    {"pointer", mouseButton(input.ui.pointer)}, {"capturesGameplay", input.ui.capturesGameplay}};

		auto actionsJson = nlohmann::json::array();

		for (const auto &action : actions) {
			nlohmann::json entry{{"name", action.name},
			                     {"keyboard", nullptr},
			                     {"mouse", nullptr},
			                     {"gamepad", nullptr},
			                     {"gamepadAxis", nullptr}};
			if (action.keyboard)
				entry["keyboard"] =
				    GameActionBinding::GetKeyName(*action.keyboard);
			if (action.mouse)
				entry["mouse"] =
				    GameActionBinding::kMouseOptions[static_cast<std::size_t>(
				                                         *action.mouse)]
				        .name;
			if (action.gamepad)
				entry["gamepad"] =
				    GameActionBinding::kGamepadOptions[static_cast<std::size_t>(
				                                           *action.gamepad)]
				        .name;
			if (action.gamepadAxis)
				entry["gamepadAxis"] =
				    GameActionBinding::kGamepadAxisOptions
				        [static_cast<std::size_t>(*action.gamepadAxis)]
				            .name;
			actionsJson.push_back(std::move(entry));
		}

		const nlohmann::json data = {
		    {"format", "EngineGame"},
		    {"version", kCurrentVersion},
		    {"gamepadEnabled", gamepadEnabled},
		    {"cameraRelativeMovement", cameraRelativeMovement},
		    {"input", inputJson},
		    {"actions", actionsJson},
		    {"settingsId", settingsId},
		    {"persistentScriptKeys", persistentScriptKeys},
		    {"lighting",
		     {{"direction", vector(lighting.direction)},
		      {"color", vector(lighting.color)},
		      {"intensity", lighting.intensity},
		      {"ambient", lighting.ambient},
		      {"shadows",
		       {{"enabled", lighting.shadows.enabled},
		        {"resolution", lighting.shadows.resolution},
		        {"extent", lighting.shadows.extent},
		        {"strength", lighting.shadows.strength},
		        {"bias", lighting.shadows.bias},
		        {"normalBias", lighting.shadows.normalBias}}}}},
		    {"title", title},
		    {"width", width},
		    {"height", height},
		    {"scene", PathUtils::ToUtf8(scenePath)},
		    {"camera",
		     {{"position", vector(fallbackCamera.position)},
		      {"target", vector(fallbackCamera.target)},
		      {"up", vector(fallbackCamera.up)}}}};
		std::string serialized = data.dump(2);
		serialized += '\n';
		text = std::move(serialized);
		return true;
	} catch (const std::exception &e) {
		error = e.what();
		return false;
	}
}

bool GameConfig::Save(const std::filesystem::path &path,
                      std::string &error, bool overwrite) const {
	try {
		std::string text;

		if (!Serialize(text, error)) {
			return false;
		}

		if (path.empty()) {
			throw std::invalid_argument(
			    "Choose a game configuration destination.");
		}

		// 起動設定の相対パスは、Loadと同じ作業ディレクトリ基準を保持する。
		return FileUtils::WriteTextAtomically(std::filesystem::absolute(path),
		                                      text, error, overwrite);
	} catch (const std::exception &e) {
		error = e.what();
		return false;
	}
}

bool GameConfig::Load(const std::filesystem::path &path, std::string &error) {
	try {
		std::ifstream file(path, std::ios::binary);

		if (!file) {
			throw std::runtime_error("Could not open game.json.");
		}

		file.exceptions(std::ios::badbit);
		const std::string text{std::istreambuf_iterator<char>(file), {}};
		return Deserialize(text, error);
	} catch (const std::exception &e) {
		error = e.what();
		return false;
	}
}

bool GameConfig::Deserialize(std::string_view text, std::string &error) {
	try {
		error.clear();
		const auto data = nlohmann::json::parse(text);

		if (data.at("format") != "EngineGame" ||
		    !data.at("version").is_number_integer() ||
		    (data.at("version") < 1 || data.at("version") > kCurrentVersion) ||
		    !data.at("width").is_number_integer() ||
		    !data.at("height").is_number_integer()) {
			throw std::runtime_error("Unsupported game configuration.");
		}

		if (data.at("width") < 1 || data.at("width") > 8192 ||
		    data.at("height") < 1 || data.at("height") > 8192) {
			throw std::runtime_error(
			    "Window dimensions are outside the supported range.");
		}

		GameConfig candidate;
		candidate.title = data.at("title").get<std::string>();
		candidate.width = data.at("width").get<int>();
		candidate.height = data.at("height").get<int>();
		candidate.scenePath =
		    PathUtils::FromUtf8(data.at("scene").get<std::string>());
		if (data.at("version") >= 2) {
			candidate.input.arrowMovement =
			    data.at("input").value("arrowMovement", false);

			for (const auto &action : GameInputBindings::kActions) {
				if (data.at("version") < 6 &&
				    action.member == &GameInputBindings::interact) {
					continue;
				}

				const auto key = GameInputBindings::FromName(
				    data.at("input").at(action.name).get<std::string>());

				if (!key) {
					throw std::runtime_error("Unsupported gameplay key.");
				}

				candidate.input.*action.member = *key;
			}
		}

		if (data.at("version") >= 16) {
			const auto &bindings = data.at("input");
			const auto stick = bindings.at("moveStick").get<std::string>();
			if (stick == "Left")
				candidate.input.moveStick = GameInputBindings::MoveStick::LEFT;
			else if (stick == "Right")
				candidate.input.moveStick = GameInputBindings::MoveStick::RIGHT;
			else if (stick == "None")
				candidate.input.moveStick = GameInputBindings::MoveStick::NONE;
			else
				throw std::runtime_error("Unsupported gameplay move stick.");
			candidate.input.dpadMovement = bindings.at("dpadMovement").get<bool>();
			const auto readGamepad = [](const nlohmann::json &value) -> std::optional<GamepadButton> {
				if (value.is_null())
					return std::nullopt;
				for (const auto &option : GameActionBinding::kGamepadOptions)
					if (value == option.name)
						return option.button;
				throw std::runtime_error("Unsupported gameplay gamepad button.");
			};
			candidate.input.gamepadJump = readGamepad(bindings.at("gamepadJump"));
			candidate.input.gamepadInteract = readGamepad(bindings.at("gamepadInteract"));

			const auto readMouse = [](const nlohmann::json &value) -> std::optional<MouseButton> {
				if (value.is_null())
					return std::nullopt;
				for (const auto &option : GameActionBinding::kMouseOptions)
					if (value == option.name)
						return option.button;
				throw std::runtime_error("Unsupported mouse button.");
			};
			const auto readKey = [](const nlohmann::json &value) -> std::optional<Key> {
				if (value.is_null())
					return std::nullopt;
				const auto key = GameActionBinding::FromKeyName(value.get<std::string>());
				if (!key)
					throw std::runtime_error("Unsupported UI key.");
				return key;
			};
			if (bindings.contains("camera")) {
				const auto &settings = bindings.at("camera");
				auto &camera = candidate.input.camera;
				const auto axis = settings.at("stick").get<std::string>();
				if (axis == "Left")
					camera.stick = GameInputBindings::MoveStick::LEFT;
				else if (axis == "Right")
					camera.stick = GameInputBindings::MoveStick::RIGHT;
				else if (axis == "None")
					camera.stick = GameInputBindings::MoveStick::NONE;
				else
					throw std::runtime_error("Unsupported camera stick.");
				camera.rotate = readMouse(settings.at("rotate"));
				camera.mouseReset = readMouse(settings.at("mouseReset"));
				camera.padReset = readGamepad(settings.at("padReset"));
				camera.wheelZoom = settings.at("wheelZoom").get<bool>();
			}
			if (bindings.contains("ui")) {
				const auto &settings = bindings.at("ui");
				auto &ui = candidate.input.ui;
				const auto &keys = settings.at("keys");
				const auto &buttons = settings.at("buttons");
				if (!keys.is_array() || !buttons.is_array() ||
				    keys.size() != GameInputBindings::UI::COUNT || buttons.size() != keys.size())
					throw std::runtime_error("Invalid UI bindings.");
				for (std::size_t i = 0; i < keys.size(); ++i) {
					ui.keys[i] = readKey(keys[i]);
					ui.buttons[i] = readGamepad(buttons[i]);
				}
				ui.next = readKey(settings.at("next"));
				ui.alternateSubmit = readKey(settings.at("alternateSubmit"));
				ui.pointer = readMouse(settings.at("pointer"));
				ui.capturesGameplay = settings.at("capturesGameplay").get<bool>();
			}
		}

		if (data.at("version") < 6 && !candidate.input.IsValid()) {
			// 旧5操作は変更せず、新しい操作だけを未使用の対応キーへ割り当てる。
			for (const auto key :
			     std::array{Key::E, Key::F, Key::Q, Key::G, Key::R, Key::T}) {
				bool used = false;

				for (const auto &action : GameInputBindings::kActions) {
					if (action.member != &GameInputBindings::interact &&
					    candidate.input.*action.member == key) {
						used = true;
					}
				}

				if (!used) {
					candidate.input.interact = key;
					break;
				}
			}
		}

		if (data.at("version") >= 3) {
			if (!data.at("cameraRelativeMovement").is_boolean()) {
				throw std::runtime_error("Movement space must be a boolean.");
			}

			candidate.cameraRelativeMovement =
			    data.at("cameraRelativeMovement").get<bool>();
		}

		if (data.at("version") >= 4) {
			const auto &light = data.at("lighting");
			candidate.lighting.direction = ReadVector(light.at("direction"));
			candidate.lighting.color = ReadVector(light.at("color"));

			if (!light.at("intensity").is_number() ||
			    !light.at("ambient").is_number()) {
				throw std::runtime_error(
				    "Light intensity and ambient must be numbers.");
			}

			candidate.lighting.intensity = light.at("intensity").get<float>();
			candidate.lighting.ambient = light.at("ambient").get<float>();
		}

		if (data.at("version") >= 5) {
			if (!data.at("gamepadEnabled").is_boolean()) {
				throw std::runtime_error("Gamepad input must be a boolean.");
			}

			candidate.gamepadEnabled = data.at("gamepadEnabled").get<bool>();
		}

		if (data.at("version") >= 7) {
			const auto &entries = data.at("actions");
			if (!entries.is_array() ||
			    entries.size() > GameActionBinding::kMaximumCount)
				throw std::runtime_error("Named input actions must be an array "
				                         "of up to 32 entries.");
			for (const auto &entry : entries) {
				GameActionBinding action;
				action.name = entry.at("name").get<std::string>();
				const auto &key = entry.at("keyboard");

				if (!key.is_null()) {
					action.keyboard =
					    GameActionBinding::FromKeyName(key.get<std::string>());
					if (!action.keyboard || (action.keyboard == Key::ESCAPE &&
					                         data.at("version") < 10))
						throw std::runtime_error("Unsupported action key.");
				}

				const auto readButton = [](const auto &value,
				                           const auto &options,
				                           auto &destination) {
					if (value.is_null())
						return;
					const auto name = value.template get<std::string>();

					for (const auto &option : options) {
						if (name == option.name) {
							destination = option.button;
							return;
						}
					}

					throw std::runtime_error(
					    "Unsupported action button or axis.");
				};
				readButton(entry.at("mouse"), GameActionBinding::kMouseOptions,
				           action.mouse);
				readButton(entry.at("gamepad"),
				           GameActionBinding::kGamepadOptions, action.gamepad);
				if (data.at("version") >= 9)
					readButton(entry.at("gamepadAxis"),
					           GameActionBinding::kGamepadAxisOptions,
					           action.gamepadAxis);
				candidate.actions.push_back(std::move(action));
			}
		}

		if (data.at("version") >= 8) {
			const auto &shadows = data.at("lighting").at("shadows");
			if (!shadows.at("enabled").is_boolean() ||
			    !shadows.at("resolution").is_number_integer())
				throw std::runtime_error(
				    "Shadow mode or resolution is invalid.");
			auto &s = candidate.lighting.shadows;
			s.enabled = shadows.at("enabled").get<bool>();
			if (shadows.at("resolution") != 512 &&
			    shadows.at("resolution") != 1024 &&
			    shadows.at("resolution") != 2048)
				throw std::runtime_error("Unsupported shadow resolution.");
			s.resolution = shadows.at("resolution").get<int>();
			const auto number = [&](const char *name) {
				if (!shadows.at(name).is_number())
					throw std::runtime_error(
					    "Shadow settings must use numbers.");
				return shadows.at(name).get<float>();
			};
			s.extent = number("extent");
			s.strength = number("strength");
			s.bias = number("bias");
			s.normalBias = number("normalBias");
		}

		if (data.at("version") >= 11) {
			candidate.settingsId = data.at("settingsId").get<std::string>();
			candidate.persistentScriptKeys =
			    data.at("persistentScriptKeys").get<std::vector<std::string>>();
		}

		const auto &camera = data.at("camera");
		candidate.fallbackCamera = {ReadVector(camera.at("position")),
		                            ReadVector(camera.at("target")),
		                            ReadVector(camera.at("up"))};

		if (!candidate.IsValid()) {
			throw std::runtime_error("Game startup settings are invalid.");
		}

		*this = std::move(candidate);
		return true;
	} catch (const std::exception &e) {
		error = e.what();
		return false;
	}
}
