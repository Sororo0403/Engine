#include "scene/SceneGrid.h"

#include "graphics/render/DebugLine.h"
#include "scene/SceneRenderer.h"

#include <cmath>

bool SceneGrid::SetSettings(const Settings &settings) {
	if (!std::isfinite(settings.height) ||
	    std::abs(settings.height) > kMaximumHeight ||
	    !std::isfinite(settings.spacing) || settings.spacing <= 0.0f ||
	    settings.spacing > kMaximumSpacing || settings.halfLineCount < 1 ||
	    settings.halfLineCount > kMaximumHalfLineCount ||
	    settings.majorLineInterval < 1 ||
	    settings.majorLineInterval > kMaximumMajorLineInterval) {
		return false;
	}

	settings_ = settings;
	return true;
}

SceneGrid::Settings SceneGrid::GetSettings() const {
	return settings_;
}

bool SceneGrid::Update(SceneRenderer &renderer) const {
	const float extent =
	    static_cast<float>(settings_.halfLineCount) * settings_.spacing;

	for (int index = -settings_.halfLineCount; index <= settings_.halfLineCount;
	     ++index) {
		// 原点を通る線は、後で座標軸の色で登録する
		if (index == 0) {
			continue;
		}

		const float offset = static_cast<float>(index) * settings_.spacing;

		const auto &color = index % settings_.majorLineInterval == 0
		                        ? majorColor_
		                        : minorColor_;

		const DebugLine xLine{
		    {-extent, settings_.height, offset},
		    {extent, settings_.height, offset},
		    color,
		};

		const DebugLine zLine{
		    {offset, settings_.height, -extent},
		    {offset, settings_.height, extent},
		    color,
		};

		if (!renderer.AddDebugLine(xLine) || !renderer.AddDebugLine(zLine)) {
			return false;
		}
	}

	const DebugLine xAxis{
	    {-extent, settings_.height, 0.0f},
	    {extent, settings_.height, 0.0f},
	    xAxisColor_,
	};

	const DebugLine zAxis{
	    {0.0f, settings_.height, -extent},
	    {0.0f, settings_.height, extent},
	    zAxisColor_,
	};

	return renderer.AddDebugLine(xAxis) && renderer.AddDebugLine(zAxis);
}
