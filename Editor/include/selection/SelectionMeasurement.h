#pragma once

#include "graphics/render/DebugLine.h"
#include "physics/AxisAlignedBox.h"
#include "scene/SceneObjectId.h"
#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string>

class Scene;
class SceneRenderer;

namespace SelectionMeasurement {
/// <summary>
/// 選択した2物体の原点間距離と、両方に有効なColliderがある場合のすき間
/// </summary>
struct Pair {
	SceneObjectId from = 0, to = 0;
	DirectX::XMFLOAT3 start{}, end{};
	std::array<double, 3> delta{};
	double distance = 0;
	double horizontalDistance = 0;
	std::optional<std::array<double, 3>> colliderGap;
	double colliderDistance = 0;
	bool collidersOverlap = false;
};

/// <summary>
/// 選択数・有効なColliderの合成範囲と、2物体選択時の測定値
/// </summary>
struct Result {
	std::size_t objects = 0, colliders = 0, invalidColliders = 0;
	std::optional<AxisAlignedBox> colliderBounds;
	std::optional<Pair> pair;
};

/// <summary>
/// モデルの有無・表示状態に依存せず、選択物体の原点と衝突形状を測定する
/// </summary>
/// <param name="scene">変更しない編集または再生シーン</param>
/// <param name="ids">重複せず、現在も存在する物体の選択順のID。空も有効</param>
/// <param name="result">成功時だけ置き換える測定値。有効なColliderだけを合成する</param>
/// <param name="error">失敗理由。成功時は空</param>
/// <returns>true:成功 false:重複・削除済みIDまたは非有限な原点。失敗時は測定値を保持する</returns>
bool Measure(const Scene &scene, std::span<const SceneObjectId> ids,
             Result &result, std::string &error);

/// <summary>
/// 原点間の測定線を作る。同じ位置や2物体以外の場合は作らない
/// </summary>
/// <param name="result">Measureで求めた測定値</param>
/// <param name="line">成功時だけ設定する黄色の線</param>
/// <returns>true:線あり false:対象なし</returns>
bool BuildLine(const Result &result, DebugLine &line);

/// <summary>
/// 2物体の原点間の線を登録する。座標が無効な選択には線を出さない
/// </summary>
/// <param name="renderer">Update成功後のScene描画先</param>
/// <param name="scene">測定対象</param>
/// <param name="ids">選択順のID</param>
/// <returns>true:登録成功または対象なし false:線の登録失敗</returns>
bool AppendLine(SceneRenderer &renderer, const Scene &scene,
                std::span<const SceneObjectId> ids);
} // namespace SelectionMeasurement
