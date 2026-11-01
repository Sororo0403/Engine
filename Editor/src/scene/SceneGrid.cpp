#include "scene/SceneGrid.h"

#include "graphics/render/DebugLine.h"
#include "scene/SceneRenderer.h"

bool SceneGrid::Update(SceneRenderer &renderer) const {
	const float extent = static_cast<float>(kHalfLineCount) * kSpacing;

	for (int index = -kHalfLineCount; index <= kHalfLineCount; ++index) {
		// 原点を通る線は、後で座標軸の色で登録する
		if (index == 0) {
			continue;
		}

		const float offset = static_cast<float>(index) * kSpacing;

		const auto &color =
		    index % kMajorLineInterval == 0 ? majorColor_ : minorColor_;

		const DebugLine xLine{
		    {-extent, kHeight, offset},
		    {extent, kHeight, offset},
		    color,
		};

		const DebugLine zLine{
		    {offset, kHeight, -extent},
		    {offset, kHeight, extent},
		    color,
		};

		if (!renderer.AddDebugLine(xLine) || !renderer.AddDebugLine(zLine)) {
			return false;
		}
	}

	const DebugLine xAxis{
	    {-extent, kHeight, 0.0f},
	    {extent, kHeight, 0.0f},
	    xAxisColor_,
	};

	const DebugLine zAxis{
	    {0.0f, kHeight, -extent},
	    {0.0f, kHeight, extent},
	    zAxisColor_,
	};

	return renderer.AddDebugLine(xAxis) && renderer.AddDebugLine(zAxis);
}
