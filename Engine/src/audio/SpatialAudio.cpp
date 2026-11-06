#include "SpatialAudio.h"

#include <Windows.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <x3daudio.h>

#pragma comment(lib, "xaudio2.lib")

bool SpatialAudio::CanCalculate(const AudioListenerPose &listener,
                                const AudioEmitterSettings &emitter) {
	if (!listener.IsValid() || !emitter.IsValid())
		return false;
	const double x = double(emitter.position.x) - listener.position.x;
	const double y = double(emitter.position.y) - listener.position.y;
	const double z = double(emitter.position.z) - listener.position.z;
	return x * x + y * y + z * z <=
	       double((std::numeric_limits<float>::max)()) / 4;
}

bool SpatialAudio::Calculate(const AudioListenerPose &listener,
                             const AudioEmitterSettings &emitter,
                             std::uint32_t speakerMask,
                             std::span<float> coefficients) {
	if (!CanCalculate(listener, emitter) || coefficients.empty() ||
	    coefficients.size() > 64 ||
	    coefficients.size() !=
	        static_cast<std::size_t>(std::popcount(speakerMask)) ||
	    (speakerMask & ~((SPEAKER_TOP_BACK_RIGHT << 1) - 1u)) != 0) {
		return false;
	}

	X3DAUDIO_HANDLE handle;
	if (FAILED(
	        X3DAudioInitialize(speakerMask, X3DAUDIO_SPEED_OF_SOUND, handle)))
		return false;
	X3DAUDIO_LISTENER receiver{};
	using namespace DirectX;
	const auto front = XMVector3Normalize(XMLoadFloat3(&listener.forward));
	const auto right =
	    XMVector3Normalize(XMVector3Cross(XMLoadFloat3(&listener.up), front));
	const auto top = XMVector3Normalize(XMVector3Cross(front, right));
	XMFLOAT3 f, u;
	XMStoreFloat3(&f, front);
	XMStoreFloat3(&u, top);
	receiver.OrientFront = {f.x, f.y, f.z};
	receiver.OrientTop = {u.x, u.y, u.z};
	receiver.Position = {listener.position.x, listener.position.y,
	                     listener.position.z};
	std::array<X3DAUDIO_DISTANCE_CURVE_POINT, 3> points{
	    {{0, 1}, {emitter.minDistance / emitter.maxDistance, 1}, {1, 0}}};
	if (emitter.minDistance == 0)
		points[1] = {1, 0};
	X3DAUDIO_DISTANCE_CURVE curve{points.data(),
	                              emitter.minDistance == 0 ? 2u : 3u};
	X3DAUDIO_EMITTER source{};
	source.OrientFront = {0, 0, 1};
	source.OrientTop = {0, 1, 0};
	source.Position = {emitter.position.x, emitter.position.y,
	                   emitter.position.z};
	source.ChannelCount = 1;
	source.CurveDistanceScaler = emitter.maxDistance;
	source.pVolumeCurve = &curve;
	std::array<float, 64> result{};
	X3DAUDIO_DSP_SETTINGS settings{};
	settings.SrcChannelCount = 1;
	settings.DstChannelCount = static_cast<UINT32>(coefficients.size());
	settings.pMatrixCoefficients = result.data();
	X3DAudioCalculate(handle, &receiver, &source, X3DAUDIO_CALCULATE_MATRIX,
	                  &settings);
	if (!std::all_of(
	        result.begin(), result.begin() + coefficients.size(),
	        [](float value) { return std::isfinite(value) && value >= 0; }))
		return false;
	std::copy_n(result.begin(), coefficients.size(), coefficients.begin());
	return true;
}
