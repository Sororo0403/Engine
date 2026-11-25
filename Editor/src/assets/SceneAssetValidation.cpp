#include "assets/SceneAssetValidation.h"
#include "animation/AnimationClipAsset.h"
#include "animation/SceneAnimators.h"
#include "scripting/LuaScript.h"
#include <algorithm>
#include <array>

#include "assets/AssetDependencies.h"
#include "assets/ImageValidation.h"
#include "audio/WaveReader.h"
#include "gameplay/SceneSimulation.h"
#include "platform/windows/PathUtils.h"
#include "platform/windows/TextRasterizer.h"
#include "scene/PrefabSerializer.h"
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
	struct AssetError {
		std::string message;
		std::filesystem::path path;
		SceneObjectId destinationObjectId = 0;
	};
	std::map<std::filesystem::path, std::vector<AssetError>> models, textures, materials,
	    monoAudio, stereoAudio, scenes, scripts, uiTextures, prefabs, clips, fonts;
	std::map<std::pair<std::filesystem::path, std::string>,
	         std::vector<AssetError>>
	    arrivals;

	const auto inspect = [](const std::filesystem::path &path, auto &cache,
	                        auto collect) -> const std::vector<AssetError> & {
		auto [entry, inserted] = cache.try_emplace(path);

		if (!inserted) {
			return entry->second;
		}

		auto &errors = entry->second;
		std::filesystem::path source;

		try {
			source = PathUtils::ResolveFromExecutable(path);
			const auto files = collect(path);
			std::set<std::filesystem::path> seen;

			for (const auto &file : files) {
				if (!seen.insert(file.lexically_normal()).second) {
					continue;
				}

				if (!std::filesystem::is_regular_file(file)) {
					errors.push_back({"Missing asset file: " +
					                  PathUtils::ToUtf8(file), file});
				}
			}
		} catch (const AssetDependencies::MissingModelDependencies &error) {
			for (const auto &file : error.paths)
				errors.push_back({AssetDependencies::DescribeMissingModelDependency(file), file});
		} catch (const std::bad_alloc &) {
			throw;
		} catch (const std::exception &error) {
			errors.push_back(
			    {std::format("{}: {}", PathUtils::ToUtf8(path), error.what()),
			     std::move(source)});
		}

		return errors;
	};

	for (const auto &[id, object] : scene.GetObjects()) {
		const auto add = [&](std::string_view field, const auto &errors,
		                     const std::filesystem::path &model = {}) {
			for (const auto &error : errors) {
				issues.push_back(
				    {id,
				     std::format("{}: {} Object: \"{}\" (ID {}).", field, error.message,
				                 object.name.empty() ? "Object" : object.name,
				                 id), error.path, error.destinationObjectId, model});
			}
		};

		if (object.triggerActions)
			for (const auto &action : object.triggerActions->actions) {
				if (action.operation != TriggerAction::Operation::LOAD_SCENE ||
				    action.scenePath.empty())
					continue;
				if (!action.arrivalTag.empty()) {
					auto [entry, inserted] = arrivals.try_emplace(
					    {action.scenePath, action.arrivalTag});
					if (inserted) {
						Scene target;
						std::string error;
						SceneObjectId invalidObject = 0;
						if (!SceneSerializer::Load(target, action.scenePath))
							error = "Could not read destination scene: " +
							        PathUtils::ToUtf8(action.scenePath);
						else
							SceneSimulation::ValidateArrival(
							    target, action.arrivalTag, error, {}, &invalidObject);
						if (!error.empty()) {
							const auto &path = action.scenePath;
							const auto location =
							    path.is_absolute() || !path.has_root_path()
							        ? PathUtils::ResolveFromExecutable(path)
							        : std::filesystem::path{};
							entry->second.push_back(
							    {std::move(error), location,
							     target.Contains(invalidObject) ? invalidObject : 0});
						}
					}

					add("Load Scene", entry->second);
					continue;
				}

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
		if (object.text && !object.text->fontAsset.empty())
			add("UI Text Font", inspect(object.text->fontAsset, fonts, [](const auto &path) {
				const auto file = PathUtils::ResolveFromExecutable(path);
				if (std::filesystem::is_regular_file(file))
					TextRasterizer::ValidateFontAsset(file);
				return std::array{file};
			}));
		if (object.image && !object.image->texture.empty())
			add("UI Image",
			    inspect(object.image->texture, uiTextures,
			            [](const auto &image) {
				            auto files = AssetDependencies::TextureFiles(image);
				            for (const auto &file : files)
					            if (std::filesystem::is_regular_file(file))
						            ImageValidation::Read(file, 4096);
				            return files;
			            }));
		if (object.particleEmitter && !object.particleEmitter->texture.empty())
			add("Particle Image",
			    inspect(object.particleEmitter->texture, uiTextures,
			            [](const auto &image) {
				            auto files = AssetDependencies::TextureFiles(image);
				            for (const auto &file : files)
					            if (std::filesystem::is_regular_file(file))
						            ImageValidation::Read(file, 4096);
				            return files;
			            }));
		if (object.script)
			for (const auto &[name, path] : object.script->scenes) {
				if (path.empty())
					continue;
				add("Script Scene " + name,
				    inspect(path, scenes, [](const auto &file) {
					    Scene loaded;
					    if (!SceneSerializer::Load(loaded, file))
						    throw std::runtime_error(
						        "Could not read Script Scene.");
					    return std::vector<std::filesystem::path>{
					        PathUtils::ResolveFromExecutable(file)};
				    }));
			}
		if (object.script)
			for (const auto &[name, path] : object.script->prefabs) {
				if (path.empty())
					continue;
				add("Script Prefab " + name,
				    inspect(path, prefabs, [](const auto &file) {
					    Scene loaded;
					    std::string error;
					    if (!PrefabSerializer::Load(file, loaded, error))
						    throw std::runtime_error(error);
					    return std::vector<std::filesystem::path>{
					        PathUtils::ResolveFromExecutable(file)};
				    }));
			}
		if (object.script && !object.script->asset.empty()) {
			add("Lua Script",
			    inspect(object.script->asset, scripts, [](const auto &path) {
				    auto absolute = PathUtils::ResolveFromExecutable(path);
				    absolute.make_preferred();
				    std::string source, error;
				    LuaScriptProgram program;
				    if (!LuaScript::Read(absolute, source, error) ||
				        !LuaScript::Compile(source, PathUtils::ToUtf8(absolute),
				                            program, error))
					    throw std::runtime_error(error);
				    return std::vector<std::filesystem::path>{std::move(absolute)};
			    }));
		}

		if (object.animationClip && object.animationClip->usesAsset) {
			const auto &asset = object.animationClip->asset;
			if (asset.empty()) {
				const std::array missing{AssetError{"Assign an Animation Clip.", {}}};
				add("Animation Clip", missing);
			} else {
				add("Animation Clip",
				    inspect(asset, clips, [](const auto &path) {
					    AnimationClipComponent clip;
					    std::string error;
					    if (!AnimationClipAsset::Load(path, clip, error))
						    throw std::runtime_error(error);
					    return std::vector<std::filesystem::path>{
					        PathUtils::ResolveFromExecutable(path)};
				    }));
			}
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
			                    [](const auto &path) {
				                    return AssetDependencies::ModelFiles(path);
			                    }), object.mesh->modelPath);
		}

		if (object.meshRenderer) {
			for (const auto &[slot, path] : object.meshRenderer->materials.slots) {
				add(std::format("Material Asset slot {}", slot),
				    inspect(path, materials, [](const auto &material) {
					    const auto files = AssetDependencies::MaterialFiles(material);
					    if (files.size() > 1 && std::filesystem::is_regular_file(files[1]))
						    ImageValidation::Read(files[1]);
					    return files;
				    }), path);
			}
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

	if (std::any_of(scene.GetObjects().begin(), scene.GetObjects().end(),
	                [](const auto &entry) {
		                return entry.second.animator.has_value();
	                })) {
		SceneAnimators validation;
		std::string issue;
		SceneObjectId object = 0;
		if (!validation.Configure(scene, false, issue, &object))
			issues.push_back({object, "Animator: " + issue});
	}
	return issues;
}
