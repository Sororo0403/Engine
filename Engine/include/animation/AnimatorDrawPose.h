#pragma once
#include "animation/ModelAnimationSampler.h"
#include "scene/SceneObjectId.h"

/// <summary>
/// 描画直前の物体と、読み込み元を特定する共有データ・変更しない骨の姿勢
/// </summary>
struct AnimatorDrawPose {
	SceneObjectId object = 0;
	std::shared_ptr<const ModelAnimationData> model;
	std::shared_ptr<const ModelAnimationPose> pose;
};
