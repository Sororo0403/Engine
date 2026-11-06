#include "SceneRigidBodySerializer.h"
#include "scene/SceneObject.h"
#include <json.hpp>
#include <stdexcept>

namespace {
/// <summary>
/// 型を変換せずに、3つの数値からなる速度を読み込む
/// </summary>
/// <param name="value">数値3成分のJSON配列</param>
/// <returns>読み込んだ速度。型が無効な場合は例外</returns>
DirectX::XMFLOAT3 Velocity(const nlohmann::json &value) {
	if (!value.is_array() || value.size() != 3 || !value[0].is_number() ||
	    !value[1].is_number() || !value[2].is_number())
		throw std::runtime_error(
		    "Rigid Body velocities require three numbers.");
	return {value[0].get<float>(), value[1].get<float>(),
	        value[2].get<float>()};
}
} // namespace

void SceneRigidBodySerializer::Write(nlohmann::json &item,
                                     const SceneObject &object) {
	if (!object.rigidBody)
		return;
	const auto &r = *object.rigidBody;
	if (!r.IsValid())
		throw std::runtime_error("Rigid Body settings are invalid.");
	item["rigidBody"] = {
	    {"enabled", r.enabled},
	    {"isKinematic", r.isKinematic},
	    {"mass", r.mass},
	    {"friction", r.friction},
	    {"restitution", r.restitution},
	    {"gravityFactor", r.gravityFactor},
	    {"linearDamping", r.linearDamping},
	    {"angularDamping", r.angularDamping},
	    {"linearVelocity",
	     {r.linearVelocity.x, r.linearVelocity.y, r.linearVelocity.z}},
	    {"angularVelocity",
	     {r.angularVelocity.x, r.angularVelocity.y, r.angularVelocity.z}},
	    {"freezeRotation", r.freezeRotation}};
}

void SceneRigidBodySerializer::Read(const nlohmann::json &item,
                                    SceneObject &object, int version) {
	if (version < 32 || !item.contains("rigidBody"))
		return;
	const auto &v = item.at("rigidBody");
	for (const auto *field : {"enabled", "isKinematic"})
		if (!v.at(field).is_boolean())
			throw std::runtime_error("Rigid Body flags require booleans.");
	for (const auto *field :
	     {"mass", "friction", "restitution", "gravityFactor", "linearDamping",
	      "angularDamping"})
		if (!v.at(field).is_number())
			throw std::runtime_error("Rigid Body settings require numbers.");
	const auto &locks = v.at("freezeRotation");
	if (!locks.is_array() || locks.size() != 3 || !locks[0].is_boolean() ||
	    !locks[1].is_boolean() || !locks[2].is_boolean())
		throw std::runtime_error(
		    "Rigid Body rotation locks require three booleans.");
	RigidBodySettings r;
	r.enabled = v.at("enabled").get<bool>();
	r.isKinematic = v.at("isKinematic").get<bool>();
	r.mass = v.at("mass").get<float>();
	r.friction = v.at("friction").get<float>();
	r.restitution = v.at("restitution").get<float>();
	r.gravityFactor = v.at("gravityFactor").get<float>();
	r.linearDamping = v.at("linearDamping").get<float>();
	r.angularDamping = v.at("angularDamping").get<float>();
	r.linearVelocity = Velocity(v.at("linearVelocity"));
	r.angularVelocity = Velocity(v.at("angularVelocity"));
	r.freezeRotation = locks.get<std::array<bool, 3>>();
	if (!r.IsValid())
		throw std::runtime_error(
		    "Rigid Body settings are outside the supported range.");
	object.rigidBody = r;
}
