#include "assets/SceneAssetValidation.h"

#include "assets/AssetDependencies.h"
#include "assets/ImageValidation.h"
#include "audio/WaveReader.h"
#include "platform/windows/PathUtils.h"
#include "scene/Scene.h"
#include "scene/SceneSerializer.h"
#include <format>
#include <map>
#include <new>
#include <set>
#include <stdexcept>
#include <string_view>

std::vector<SceneValidationIssue>
SceneAssetValidation::Inspect(const Scene &scene) {
	std::vector<SceneValidationIssue> issues;
	std::map<std::filesystem::path, std::vector<std::string>> models, textures,
	    monoAudio, stereoAudio, scenes;

	const auto inspect = [](const std::filesystem::path &path, auto &cache,
	                        auto collect) -> const std::vector<std::string> & {
		auto [entry, inserted] = cache.try_emplace(path);

		if (!inserted) {
			return entry->second;
		}

		auto &errors = entry->second;

		try {
			const auto files = collect(path);
			std::set<std::filesystem::path> seen;

			for (const auto &file : files) {
				if (!seen.insert(file.lexically_normal()).second) {
					continue;
				}

				if (!std::filesystem::is_regular_file(file)) {
					errors.push_back("Missing asset file: " +
					                 PathUtils::ToUtf8(file));
				}
			}
		} catch (const std::bad_alloc &) {
			throw;
		} catch (const std::exception &error) {
			errors.push_back(
			    std::format("{}: {}", PathUtils::ToUtf8(path), error.what()));
		}

		return errors;
	};

	for (const auto &[id, object] : scene.GetObjects()) {
		const auto add = [&](std::string_view field, const auto &errors) {
			for (const auto &error : errors) {
				issues.push_back(
				    {id,
				     std::format("{}: {} Object: \"{}\" (ID {}).", field, error,
				                 object.name.empty() ? "Object" : object.name,
				                 id)});
			}
		};

		if (object.triggerActions)
			for (const auto &action : object.triggerActions->actions) {
				if (action.operation != TriggerAction::Operation::LOAD_SCENE ||
				    action.scenePath.empty())
					continue;
				add("Load Scene",
				    inspect(action.scenePath, scenes, [](const auto &path) {
					    Scene target;
					    if (!SceneSerializer::Load(target, path))
						    throw std::runtime_error(
						        "Could not read destination scene.");
					    return std::vector<std::filesystem::path>{
					        PathUtils::ResolveFromExecutable(path)};
				    }));
			}
		if (object.audioSource && !object.audioSource->clipPath.empty()) {
			const bool spatial = object.audioSource->spatial;
			add("Audio Source",
			    inspect(object.audioSource->clipPath,
			            spatial ? monoAudio : stereoAudio,
			            [spatial](const auto &path) {
				            auto files = AssetDependencies::AudioFiles(path);
				            for (const auto &file : files)
					            if (std::filesystem::is_regular_file(file)) {
						            WaveData wave;
						            std::string error;
						            if (!WaveReader::Load(file, wave, error))
							            throw std::runtime_error(error);
						            if (spatial && wave.channels != 1)
							            throw std::runtime_error(
							                "Spatial Audio Source needs a mono "
							                "WAV clip.");
					            }

				            return files;
			            }));
		}

		if (object.HasMesh()) {
			add("Mesh", inspect(object.mesh->modelPath, models,
			                    AssetDependencies::ModelFiles));
		}

		if (object.meshRenderer) {
			for (const auto &[slot, path] :
			     object.meshRenderer->textures.slots) {
				add(std::format("Mesh Renderer slot {}", slot),
				    inspect(path, textures, [](const auto &image) {
					    const auto files =
					        AssetDependencies::TextureFiles(image);

					    for (const auto &file : files) {
						    if (std::filesystem::is_regular_file(file)) {
							    ImageValidation::Read(file);
						    }
					    }

					    return files;
				    }));
			}
		}
	}

	return issues;
}
