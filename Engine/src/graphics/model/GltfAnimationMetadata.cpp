#include "GltfAnimationMetadata.h"
#include "platform/windows/PathUtils.h"
#include <array>
#include <cstdint>
#include <exception>
#include <format>
#include <fstream>
#include <json.hpp>
#include <stdexcept>
#include <utility>

namespace {
/// <summary>
/// GLBのヘッダー値を、短い読み込みを拒否して取得する
/// </summary>
/// <param name="input">バイナリモードのモデル</param>
/// <returns>リトルエンディアンの32ビット値</returns>
std::uint32_t Read32(std::ifstream &input) {
	std::array<unsigned char, 4> bytes{};
	if (!input.read(reinterpret_cast<char *>(bytes.data()), 4))
		throw std::runtime_error("GLB header is truncated.");
	return std::uint32_t(bytes[0]) | (std::uint32_t(bytes[1]) << 8) |
	       (std::uint32_t(bytes[2]) << 16) | (std::uint32_t(bytes[3]) << 24);
}
} // namespace

bool GltfAnimationMetadata::Read(const std::filesystem::path &path,
                                 std::string &error) {
	try {
		std::ifstream input(path, std::ios::binary | std::ios::ate);
		if (!input)
			throw std::runtime_error("Could not open glTF animation metadata.");
		const auto length = input.tellg();
		if (length < 0)
			throw std::runtime_error("Could not measure glTF metadata.");
		input.seekg(0);
		std::uint64_t count = static_cast<std::uint64_t>(length);
		if (PathUtils::HasExtension(path, ".glb")) {
			if (Read32(input) != 0x46546c67 || Read32(input) != 2 ||
			    Read32(input) != count)
				throw std::runtime_error("GLB version or length is invalid.");
			count = Read32(input);
			if (Read32(input) != 0x4e4f534a || count % 4 ||
			    count + 20 > static_cast<std::uint64_t>(length))
				throw std::runtime_error("GLB JSON chunk is invalid.");
		}
		if (count > 256 * 1024 * 1024)
			throw std::runtime_error("glTF JSON exceeds 256 MiB.");
		std::string text(static_cast<std::size_t>(count), '\0');
		if (!input.read(text.data(), static_cast<std::streamsize>(count)))
			throw std::runtime_error("glTF JSON is truncated.");
		const auto json = nlohmann::json::parse(text);
		std::vector<std::map<std::string, Modes>> clips;
		if (json.contains("animations")) {
			const auto &animations = json.at("animations");
			const auto &nodes = json.at("nodes");
			if (json.at("asset").at("version") != "2.0" ||
			    !animations.is_array() ||
			    animations.size() > ModelAnimationData::kMaximumClips ||
			    !nodes.is_array() ||
			    nodes.size() > ModelAnimationData::kMaximumNodes)
				throw std::runtime_error("Animated glTF requires version 2.0 "
				                         "and bounded nodes and clips.");
			for (const auto &animation : animations) {
				std::map<std::string, Modes> modes;
				const auto &channels = animation.at("channels");
				const auto &samplers = animation.at("samplers");
				if (!channels.is_array() ||
				    channels.size() > ModelAnimationData::kMaximumNodes * 3 ||
				    !samplers.is_array())
					throw std::runtime_error(
					    "glTF animation channels are invalid.");
				for (const auto &channel : channels) {
					const auto &target = channel.at("target");
					if (!target.at("node").is_number_unsigned() ||
					    !channel.at("sampler").is_number_unsigned())
						throw std::runtime_error("glTF animation indices must "
						                         "be unsigned integers.");
					const auto node = target.at("node").get<std::size_t>(),
					           sampler =
					               channel.at("sampler").get<std::size_t>();
					if (node >= nodes.size() || sampler >= samplers.size())
						throw std::runtime_error(
						    "glTF animation target or sampler is missing.");
					const auto mode = samplers[sampler].value(
					    "interpolation", std::string("LINEAR"));
					if (mode != "LINEAR" && mode != "STEP")
						throw std::runtime_error(
						    "glTF animation currently supports LINEAR and "
						    "STEP; export cubic spline curves as sampled "
						    "linear keys.");
					const auto name = nodes[node].value("name", std::string{});
					auto &m =
					    modes[name.empty() ? std::format("nodes[{}]", node)
					                       : name];
					const auto component = target.at("path").get<std::string>();
					std::optional<bool> *value =
					    component == "translation" ? &m.position
					    : component == "rotation"  ? &m.rotation
					    : component == "scale"     ? &m.scale
					                               : nullptr;
					if (!value || value->has_value())
						throw std::runtime_error("glTF animation component is "
						                         "unsupported or duplicated.");
					*value = mode == "STEP";
				}
				clips.push_back(std::move(modes));
			}
		}
		clips_ = std::move(clips);
		error.clear();
		return true;
	} catch (const std::exception &e) {
		error = e.what();
		return false;
	}
}

bool GltfAnimationMetadata::Apply(ModelAnimationData &data,
                                  std::string &error) const {
	try {
		if (data.clips.size() != clips_.size())
			throw std::runtime_error(
			    "glTF animation clips were not retained completely.");
		for (std::size_t i = 0; i < clips_.size(); ++i) {
			std::size_t applied = 0;
			for (auto &track : data.clips[i].tracks) {
				auto at = clips_[i].find(data.nodes.at(track.node).name);
				if (at == clips_[i].end())
					throw std::runtime_error(
					    "glTF animation target name was not retained.");
				++applied;
				const auto &m = at->second;
				const auto set = [](auto &keys,
				                    const std::optional<bool> &mode) {
					if (mode) {
						if (keys.empty())
							throw std::runtime_error(
							    "glTF animation keys were omitted.");
						for (auto &key : keys)
							key.step = *mode;
					}
				};
				set(track.positions, m.position);
				set(track.rotations, m.rotation);
				set(track.scales, m.scale);
				track.before = track.after =
				    ModelAnimationTrack::Boundary::CONSTANT;
			}
			if (applied != clips_[i].size())
				throw std::runtime_error(
				    "glTF animation channels were not retained completely.");
		}
		error.clear();
		return true;
	} catch (const std::exception &e) {
		error = e.what();
		return false;
	}
}
