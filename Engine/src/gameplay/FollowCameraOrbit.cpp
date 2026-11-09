#include "gameplay/FollowCameraOrbit.h"

#include <algorithm>
#include <cmath>
#include <numbers>

bool FollowCameraOrbit::Initialize(const FollowCameraSettings &settings) {
	if (!settings.IsValid()) {
		return false;
	}

	const double x = double(settings.offset.x) - settings.lookOffset.x;
	const double y = double(settings.offset.y) - settings.lookOffset.y;
	const double z = double(settings.offset.z) - settings.lookOffset.z;
	const auto &limits = settings.orbit;
	const double degrees = 180.0 / std::numbers::pi;
	yaw_ = std::atan2(x, z) * degrees;
	pitch_ =
	    std::clamp(std::atan2(y, std::hypot(x, z)) * degrees,
	               double(limits.minimumPitch), double(limits.maximumPitch));
	distance_ = std::clamp(std::hypot(x, y, z), double(limits.minimumDistance),
	                       double(limits.maximumDistance));
	initialized_ = true;
	return true;
}

bool FollowCameraOrbit::Update(float seconds, const OrbitCameraInput &input,
                               const FollowCameraSettings &settings) {
	if (!initialized_ || !settings.IsValid() || !input.IsValid() ||
	    !std::isfinite(seconds) || seconds < 0) {
		return false;
	}

	if (seconds == 0) {
		return true;
	}

	if (input.reset) {
		return Initialize(settings);
	}

	const auto &limits = settings.orbit;
	const double time = std::min(double(seconds), 0.1);
	yaw_ =
	    std::remainder(yaw_ + double(input.mouseX) * limits.mouseSensitivity +
	                       double(input.stickX) * limits.stickSpeed * time,
	                   360.0);
	pitch_ =
	    std::clamp(pitch_ + double(input.mouseY) * limits.mouseSensitivity -
	                   double(input.stickY) * limits.stickSpeed * time,
	               double(limits.minimumPitch), double(limits.maximumPitch));
	const double exponent =
	    std::clamp(-double(input.zoom) * limits.zoomSensitivity, -80.0, 80.0);
	distance_ = std::clamp(distance_ * std::exp(exponent),
	                       double(limits.minimumDistance),
	                       double(limits.maximumDistance));
	return true;
}

CameraPose
FollowCameraOrbit::MakePose(const DirectX::XMFLOAT3 &target,
                            const FollowCameraSettings &settings) const {
	CameraPose result;
	result.target = {target.x + settings.lookOffset.x,
	                 target.y + settings.lookOffset.y,
	                 target.z + settings.lookOffset.z};
	const double yaw = yaw_ * std::numbers::pi / 180.0;
	const double pitch = pitch_ * std::numbers::pi / 180.0;
	const double horizontal = distance_ * std::cos(pitch);
	result.position = {
	    static_cast<float>(result.target.x + horizontal * std::sin(yaw)),
	    static_cast<float>(result.target.y + distance_ * std::sin(pitch)),
	    static_cast<float>(result.target.z + horizontal * std::cos(yaw))};
	return result;
}
