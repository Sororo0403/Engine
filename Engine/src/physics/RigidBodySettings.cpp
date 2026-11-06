#include "physics/RigidBodySettings.h"
#include <cmath>

bool RigidBodySettings::IsValid() const {
	const auto range = [](float value, float low, float high) {
		return std::isfinite(value) && value >= low && value <= high;
	};
	const auto velocity = [&](const DirectX::XMFLOAT3 &v, float limit) {
		const double length =
		    double(v.x) * v.x + double(v.y) * v.y + double(v.z) * v.z;
		return range(v.x, -limit, limit) && range(v.y, -limit, limit) &&
		       range(v.z, -limit, limit) && length <= double(limit) * limit;
	};
	return range(mass, 0.001f, 1000000.0f) && range(friction, 0, 1) &&
	       range(restitution, 0, 1) && range(gravityFactor, 0, 10) &&
	       range(linearDamping, 0, 1) && range(angularDamping, 0, 1) &&
	       velocity(linearVelocity, 500) && velocity(angularVelocity, 100);
}

bool RigidBodySettings::operator==(const RigidBodySettings &other) const {
	const auto equal = [](const auto &a, const auto &b) {
		return a.x == b.x && a.y == b.y && a.z == b.z;
	};
	return enabled == other.enabled && isKinematic == other.isKinematic &&
	       mass == other.mass && friction == other.friction &&
	       restitution == other.restitution &&
	       gravityFactor == other.gravityFactor &&
	       linearDamping == other.linearDamping &&
	       angularDamping == other.angularDamping &&
	       equal(linearVelocity, other.linearVelocity) &&
	       equal(angularVelocity, other.angularVelocity) &&
	       freezeRotation == other.freezeRotation;
}
