#pragma once

#include "scene/SceneObjectId.h"
#include <DirectXMath.h>
#include <array>
#include <cstddef>

class Scene;
class SceneRenderer;

/// <summary>
/// 実際の移動・衝突処理を使い、選択した主人公のジャンプ軌跡をSceneビューに表示する
/// </summary>
class CharacterJumpPreview {
  public:
	static constexpr std::size_t kDirectionCount = 9;
	static constexpr std::size_t kMaximumSteps = 360;
	static constexpr std::size_t kSampleInterval = 4;
	static constexpr std::size_t kMaximumPoints =
	    kMaximumSteps / kSampleInterval + 1;

	/// <summary>
	/// 軌跡の計算を終了した理由
	/// </summary>
	enum class End { LANDED, FALL_LIMIT, TIME_LIMIT };

	/// <summary>
	/// 1方向の物体原点の軌跡。先頭は開始位置、末尾は終了時の位置
	/// </summary>
	struct Path {
		std::array<DirectX::XMFLOAT3, kMaximumPoints> points{};
		std::size_t count = 0;
		End end = End::TIME_LIMIT;
		// 終了までの秒数、開始位置からの最高上昇量、終了位置までの水平直線距離。
		// 描画用の間引き前に毎更新から測定する。着地以外の終了理由では到達可能距離を意味しない。
		double durationSeconds = 0;
		double maximumHeight = 0;
		double horizontalDistance = 0;
	};

	using Paths = std::array<Path, kDirectionCount>;

	/// <summary>
	/// 接地した主人公のその場・+X・-X・+Z・-Z・斜め4方向の順でジャンプを120Hzで計算する。
	/// 斜めは+X/+Z・+X/-Z・-X/+Z・-X/-Zの順。実際の移動と同じ速度制限を使う。
	/// 着地・落下判定高度・3秒で終了し、4更新ごとと終了時の位置を記録する
	/// </summary>
	/// <param name="scene">変更しない編集シーン。非表示のColliderも障害物に含める</param>
	/// <param name="id">有効なCharacter ControllerとColliderを持つ接地した物体</param>
	/// <param name="paths">成功時に置き換える軌跡。復帰によるテレポートは含めない</param>
	/// <returns>true:計算成功 false:対象なし、未接地、無効な設定・配置・移動。失敗時は出力を保持する</returns>
	bool BuildPaths(const Scene &scene, SceneObjectId id, Paths &paths) const;

	/// <summary>
	/// 全方向のジャンプ軌跡と終点の目印を囲む8頂点を取得する
	/// </summary>
	/// <param name="scene">変更しない編集シーン</param>
	/// <param name="id">接地した有効な主人公のID</param>
	/// <param name="corners">成功時に置き換えるワールド座標の8頂点</param>
	/// <returns>true:取得成功 false:軌跡または範囲が無効。失敗時は出力を保持する</returns>
	bool GetFrameBounds(const Scene &scene, SceneObjectId id,
	                    std::array<DirectX::XMFLOAT3, 8> &corners) const;

	/// <summary>
	/// ジャンプ軌跡と、着地は緑・落下判定は赤・時間上限は黄の終点を描画へ登録する
	/// </summary>
	/// <param name="renderer">更新済みのScene描画先</param>
	/// <param name="scene">変更しない編集シーン</param>
	/// <param name="id">アクティブな選択物体</param>
	/// <returns>true:登録成功または表示対象なし false:線の登録失敗</returns>
	bool Update(SceneRenderer &renderer, const Scene &scene,
	            SceneObjectId id) const;

  private:
	static constexpr float kStepSeconds = 1.0f / 120.0f;
	static constexpr float kMarkerRadius = 0.15f;
};
