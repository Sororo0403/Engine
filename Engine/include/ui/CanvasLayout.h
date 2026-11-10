#pragma once
#include "scene/SceneObjectId.h"
#include <array>
#include <cstddef>
#include <string>
#include <vector>
class Scene;

/// <summary>
/// 描画先の左上を原点とする、ピクセル単位のUI矩形
/// </summary>
struct UIRect {
	float x = 0, y = 0, width = 0, height = 0;
};

/// <summary>
/// 配置座標と画面座標を結ぶ、親から継承した回転と移動
/// </summary>
struct UITransform {
	float cosine = 1, sine = 0;
	float x = 0, y = 0;

	std::array<float, 2> ToScreen(float px, float py) const {
		return {cosine * px - sine * py + x, sine * px + cosine * py + y};
	}

	std::array<float, 2> ToLayout(float px, float py) const {
		px -= x;
		py -= y;
		return {cosine * px + sine * py, -sine * px + cosine * py};
	}

	bool operator==(const UITransform &) const = default;
};

/// <summary>
/// 描画順に並べるUI物体の、画面上の矩形と基準座標からの拡縮率
/// </summary>
struct UIElement {
	SceneObjectId object = 0, canvas = 0;
	UIRect rect;
	float scale = 1;
	UITransform transform;

	/// <summary>
	/// 回転した矩形の外接範囲が画面と重なるか確認する
	/// </summary>
	bool IntersectsViewport(int width, int height) const;
};

namespace CanvasLayout {
inline constexpr std::size_t kMaximumElements = 256;
/// <summary>
/// 有効なCanvasの子を親から順に配置する。3DのTransformはUIの位置へ使用しない
/// </summary>
/// <param name="scene">参照するシーン</param>
/// <param name="width">描画先の幅。1から16384</param>
/// <param name="height">描画先の高さ。1から16384</param>
/// <param name="elements">成功時だけ置き換える、Canvas描画順・階層順の文字・画像・Buttonを持つ物体</param>
/// <param name="error">失敗理由。成功時は空</param>
/// <param name="invalidObject">指定時は無効なUI物体のIDを返す。成功時は0</param>
/// <param name="skipIncomplete">true:編集プレビューでCanvas・Rect Transformが未設定の要素を省略する</param>
/// <returns>true:配置成功 false:親子関係・設定・描画数が無効。空シーンは成功</returns>
/// <remarks>CanvasとRect Transformの併用は不可。描画を有効にしたText・Image・ButtonにはRect Transformと祖先Canvasが必要。寸法0以下の要素は描画対象外</remarks>
bool Collect(const Scene &scene, int width, int height,
             std::vector<UIElement> &elements, std::string &error,
             SceneObjectId *invalidObject = nullptr,
             bool skipIncomplete = false);
} // namespace CanvasLayout
