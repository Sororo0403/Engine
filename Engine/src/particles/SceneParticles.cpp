#include "particles/SceneParticles.h"
#include "math/MathUtils.h"
#include "scene/Scene.h"
#include "scene/SceneActivity.h"
#include "scene/SceneHierarchy.h"
#include <algorithm>
#include <cmath>
#include <exception>
#include <stdexcept>
#include <utility>

float SceneParticles::Random(State &state) {
	auto &x = state.random;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	return static_cast<float>(x >> 8) * (1.0f / 16777216.0f);
}

void SceneParticles::Integrate(Particle &p,
                               const ParticleEmitterComponent &settings,
                               const DirectX::XMFLOAT4X4 &matrix,
                               float seconds) {
	using namespace DirectX;
	auto gravity = XMLoadFloat3(&settings.gravity);
	if (settings.localSpace)
		gravity = XMVector3TransformNormal(
		    gravity, XMMatrixInverse(nullptr, XMLoadFloat4x4(&matrix)));
	const auto position = XMVectorAdd(
	    XMLoadFloat3(&p.position),
	    XMVectorAdd(XMVectorScale(XMLoadFloat3(&p.velocity), seconds),
	                XMVectorScale(gravity, .5f * seconds * seconds)));
	XMStoreFloat3(&p.position, position);
	XMStoreFloat3(&p.velocity, XMVectorAdd(XMLoadFloat3(&p.velocity),
	                                       XMVectorScale(gravity, seconds)));
	p.age += seconds;
}

void SceneParticles::Emit(State &state, const DirectX::XMFLOAT4X4 &matrix,
                          const DirectX::XMFLOAT3 &origin, int count, float age,
                          std::size_t &total) {
	using namespace DirectX;
	const auto &s = state.settings;
	const auto available = (std::min)(static_cast<std::size_t>(s.maxParticles) -
	                                      state.particles.size(),
	                                  kMaximumTotalParticles - total);
	const int make = (std::min)(count, static_cast<int>(available));

	for (int i = 0; i < make; ++i) {
		Particle p;
		p.lifetime =
		    s.lifetimeMin + (s.lifetimeMax - s.lifetimeMin) * Random(state);
		const float angle = XM_2PI * Random(state),
		            radius = s.radius * std::sqrt(Random(state));
		auto position = XMVectorSet(std::cos(angle) * radius, 0,
		                            std::sin(angle) * radius, 1);
		const float cosine =
		    1 -
		    (1 - std::cos(XMConvertToRadians(s.coneDegrees))) * Random(state);
		const float sine = std::sqrt((std::max)(0.0f, 1 - cosine * cosine));
		const float phi = XM_2PI * Random(state),
		            speed =
		                s.speedMin + (s.speedMax - s.speedMin) * Random(state);
		auto direction =
		    XMVectorSet(sine * std::cos(phi), cosine, sine * std::sin(phi), 0);
		p.rotation = XM_2PI * Random(state);

		if (!s.localSpace) {
			const auto world = XMLoadFloat4x4(&matrix);
			position = XMVector3TransformCoord(position, world);
			position =
			    XMVectorAdd(position, XMVectorSet(origin.x - matrix._41,
			                                      origin.y - matrix._42,
			                                      origin.z - matrix._43, 0));
			direction =
			    XMVector3Normalize(XMVector3TransformNormal(direction, world));
			p.scale = (std::max)({XMVectorGetX(XMVector3Length(world.r[0])),
			                      XMVectorGetX(XMVector3Length(world.r[1])),
			                      XMVectorGetX(XMVector3Length(world.r[2]))});
		}

		XMStoreFloat3(&p.position, position);
		XMStoreFloat3(&p.velocity, XMVectorScale(direction, speed));
		Integrate(p, s, matrix, age);

		if (p.age < p.lifetime) {
			state.particles.push_back(p);
			++total;
		}
	}
}

bool SceneParticles::Configure(const Scene &scene, bool preserve,
                               std::string &error,
                               SceneObjectId *invalidObject) {
	SceneObjectId current = 0;

	try {
		std::map<SceneObjectId, State> next;
		SceneActivity::States active;
		SceneHierarchy::WorldTransforms transforms;
		if (!SceneActivity::BuildStates(scene, active) ||
		    !SceneHierarchy::BuildWorldTransforms(scene, transforms))
			throw std::runtime_error("Particle hierarchy is invalid.");
		std::size_t total = 0;

		for (const auto &[id, o] : scene.GetObjects()) {
			if (!o.particleEmitter)
				continue;
			current = id;
			if (!o.particleEmitter->IsValid())
				throw std::runtime_error(
				    "Particle Emitter settings are invalid.");
			State state;
			const auto found = states_.find(id);
			if (preserve && found != states_.end() &&
			    found->second.settings == *o.particleEmitter)
				state = found->second;
			else {
				state.settings = *o.particleEmitter;
				state.random = state.settings.seed ? state.settings.seed : 1;
				state.emitting = state.pendingStart =
				    state.settings.playOnStart;
				state.lastOrigin = transforms.at(id).GetPosition();
			}

			total += state.particles.size();
			next.emplace(id, std::move(state));
		}

		for (auto &[id, state] : next)
			if (state.pendingStart && active.at(id)) {
				state.pendingStart = false;
				Emit(state, transforms.at(id).matrix,
				     transforms.at(id).GetPosition(), state.settings.burst, 0,
				     total);
			}
		states_ = std::move(next);
		error.clear();
		if (invalidObject)
			*invalidObject = 0;
		return true;
	} catch (const std::exception &e) {
		error = e.what();
		if (invalidObject)
			*invalidObject = current;
		return false;
	}
}

bool SceneParticles::Advance(const Scene &scene, float seconds,
                             std::string &error) {
	try {
		if (!std::isfinite(seconds) || seconds < 0)
			throw std::runtime_error("Particle time is invalid.");
		if (seconds == 0) {
			error.clear();
			return true;
		}

		SceneActivity::States active;
		SceneHierarchy::WorldTransforms transforms;
		if (!SceneActivity::BuildStates(scene, active) ||
		    !SceneHierarchy::BuildWorldTransforms(scene, transforms))
			throw std::runtime_error("Particle hierarchy is invalid.");
		std::size_t total = 0;

		for (const auto &[id, s] : states_) {
			(void)id;
			total += s.particles.size();
		}

		const double duration = (std::min)(double(seconds), .1);

		for (auto &[id, state] : states_) {
			if (!scene.FindObject(id) || !scene.FindObject(id)->particleEmitter)
				throw std::runtime_error("Particle Emitter is missing.");
			const auto &world = transforms.at(id);
			const auto origin = world.GetPosition();

			if (!active.at(id)) {
				state.lastOrigin = origin;
				continue;
			}

			double advanced = 0;

			while (advanced < duration) {
				const double h =
				    (std::min)({duration - advanced, 1.0 / 120.0,
				                state.emitting
				                    ? double(state.settings.duration) -
				                          state.elapsed
				                    : duration});
				if (h <= 0)
					throw std::runtime_error("Particle clock is invalid.");
				const float step = static_cast<float>(h);
				const auto before = state.particles.size();
				for (auto &p : state.particles)
					Integrate(p, state.settings, world.matrix, step);
				std::erase_if(state.particles, [](const auto &p) {
					return p.age >= p.lifetime;
				});
				total -= before - state.particles.size();

				if (state.emitting) {
					const double sum = state.fraction + h * state.settings.rate;
					const int count = static_cast<int>(std::floor(sum + 1e-9));
					for (int i = 0; i < count &&
					                state.particles.size() <
					                    static_cast<std::size_t>(
					                        state.settings.maxParticles) &&
					                total < kMaximumTotalParticles;
					     ++i) {
						const double birth = state.settings.rate > 0
						                         ? ((i + 1) - state.fraction) /
						                               state.settings.rate
						                         : 0;
						const float blend = static_cast<float>(std::clamp(
						    (advanced + birth) / duration, 0.0, 1.0));
						DirectX::XMFLOAT3 at{
						    state.lastOrigin.x +
						        (origin.x - state.lastOrigin.x) * blend,
						    state.lastOrigin.y +
						        (origin.y - state.lastOrigin.y) * blend,
						    state.lastOrigin.z +
						        (origin.z - state.lastOrigin.z) * blend};
						Emit(state, world.matrix, at, 1,
						     static_cast<float>((std::max)(0.0, h - birth)),
						     total);
					}
					state.fraction = (std::max)(0.0, sum - count);
					state.elapsed += h;

					if (state.elapsed >= state.settings.duration - 1e-9) {
						if (state.settings.loop) {
							state.elapsed = 0;
							Emit(state, world.matrix, origin,
							     state.settings.burst, 0, total);
						} else {
							state.elapsed = state.settings.duration;
							state.emitting = false;
						}
					}
				}

				advanced += h;
			}

			for (const auto &p : state.particles)
				if (!MathUtils::IsFinite(p.position) ||
				    !MathUtils::IsFinite(p.velocity))
					throw std::runtime_error(
					    "Particle motion became non-finite.");
			state.lastOrigin = origin;
		}

		error.clear();
		return true;
	} catch (const std::exception &e) {
		error = e.what();
		return false;
	}
}

bool SceneParticles::Control(const Scene &scene, SceneObjectId id,
                             Operation operation, int count, bool restart,
                             std::string &error) {
	try {
		auto found = states_.find(id);
		if (found == states_.end() || !scene.FindObject(id) ||
		    !scene.FindObject(id)->particleEmitter)
			throw std::runtime_error("This object requires Particle Emitter.");
		if (count < 0 || count > ParticleEmitterComponent::kMaximumParticles)
			throw std::runtime_error("Emit count must be between 0 and 4096.");
		auto state = found->second;
		state.pendingStart = false;
		SceneWorldTransform world;
		if (!SceneHierarchy::GetWorldTransform(scene, id, world))
			throw std::runtime_error("Particle transform is invalid.");
		std::size_t total = 0;

		for (const auto &[key, value] : states_) {
			(void)key;
			total += value.particles.size();
		}

		if (operation == Operation::PLAY) {
			if (!SceneActivity::IsActive(scene, id))
				throw std::runtime_error(
				    "Activate the object before playing particles.");
			if (restart) {
				total -= state.particles.size();
				state.particles.clear();
				state.elapsed = state.fraction = 0;
				state.random = state.settings.seed ? state.settings.seed : 1;
				Emit(state, world.matrix, world.GetPosition(),
				     state.settings.burst, 0, total);
			}

			state.emitting =
			    state.settings.loop || state.elapsed < state.settings.duration;
		} else if (operation == Operation::STOP)
			state.emitting = false;
		else if (operation == Operation::CLEAR) {
			state.particles.clear();
			state.fraction = 0;
		} else if (operation == Operation::EMIT) {
			if (!SceneActivity::IsActive(scene, id))
				throw std::runtime_error(
				    "Activate the object before emitting particles.");
			Emit(state, world.matrix, world.GetPosition(), count, 0, total);
		} else
			throw std::runtime_error("Particle operation is invalid.");
		state.lastOrigin = world.GetPosition();
		found->second = std::move(state);
		error.clear();
		return true;
	} catch (const std::exception &e) {
		error = e.what();
		return false;
	}
}

std::optional<ParticlePlaybackState>
SceneParticles::GetState(SceneObjectId id) const {
	const auto found = states_.find(id);
	if (found == states_.end())
		return std::nullopt;
	return ParticlePlaybackState{found->second.elapsed,
	                             found->second.particles.size(),
	                             found->second.emitting};
}

bool SceneParticles::Snapshot(const Scene &scene,
                              std::vector<ParticleDrawBatch> &batches,
                              std::string &error) const {
	try {
		std::vector<ParticleDrawBatch> result;
		SceneHierarchy::WorldTransforms transforms;
		SceneActivity::States active;
		if (!SceneHierarchy::BuildWorldTransforms(scene, transforms) ||
		    !SceneActivity::BuildStates(scene, active))
			throw std::runtime_error("Particle hierarchy is invalid.");
		for (const auto &[id, state] : states_) {
			if (!scene.FindObject(id))
				throw std::runtime_error("Particle Emitter is missing.");
			if (!active.at(id) || state.particles.empty())
				continue;
			ParticleDrawBatch batch;
			batch.texture = state.settings.texture;
			batch.additive = state.settings.additive;
			batch.sprites.reserve(state.particles.size());
			const auto world =
			    DirectX::XMLoadFloat4x4(&transforms.at(id).matrix);
			const float scale =
			    state.settings.localSpace
			        ? (std::max)({DirectX::XMVectorGetX(
			                          DirectX::XMVector3Length(world.r[0])),
			                      DirectX::XMVectorGetX(
			                          DirectX::XMVector3Length(world.r[1])),
			                      DirectX::XMVectorGetX(
			                          DirectX::XMVector3Length(world.r[2]))})
			        : 1;
			for (const auto &p : state.particles) {
				const float t = std::clamp(p.age / p.lifetime, 0.0f, 1.0f);
				ParticleSprite sprite;
				sprite.position = p.position;
				if (state.settings.localSpace)
					DirectX::XMStoreFloat3(
					    &sprite.position,
					    DirectX::XMVector3TransformCoord(
					        DirectX::XMLoadFloat3(&p.position), world));
				sprite.size =
				    (state.settings.sizeStart +
				     (state.settings.sizeEnd - state.settings.sizeStart) * t) *
				    p.scale * scale;
				sprite.color = {state.settings.colorStart[0] +
				                    (state.settings.colorEnd[0] -
				                     state.settings.colorStart[0]) *
				                        t,
				                state.settings.colorStart[1] +
				                    (state.settings.colorEnd[1] -
				                     state.settings.colorStart[1]) *
				                        t,
				                state.settings.colorStart[2] +
				                    (state.settings.colorEnd[2] -
				                     state.settings.colorStart[2]) *
				                        t,
				                state.settings.colorStart[3] +
				                    (state.settings.colorEnd[3] -
				                     state.settings.colorStart[3]) *
				                        t};
				sprite.rotation =
				    p.rotation +
				    DirectX::XMConvertToRadians(state.settings.rotationSpeed) *
				        p.age;
				if (!MathUtils::IsFinite(sprite.position) ||
				    !std::isfinite(sprite.size) ||
				    !std::isfinite(sprite.rotation))
					throw std::runtime_error(
					    "Particle snapshot is non-finite.");
				batch.sprites.push_back(sprite);
			}

			result.push_back(std::move(batch));
		}

		batches = std::move(result);
		error.clear();
		return true;
	} catch (const std::exception &e) {
		error = e.what();
		return false;
	}
}
