#pragma once

/// <summary>
/// Sceneカメラを向ける方向。各方向は対象を見る側のワールド軸を表す
/// </summary>
enum class CameraView {
	FRONT,  // -Z側
	BACK,   // +Z側
	LEFT,   // -X側
	RIGHT,  // +X側
	TOP,    // +Y側
	BOTTOM, // -Y側
};
