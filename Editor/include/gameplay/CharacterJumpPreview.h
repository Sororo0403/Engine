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
	static constexpr std::size_t kDirectionCount = 5;
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
	};

	using Paths = std::array<Path, kDirectionCount>;

	/// <summary>
	/// 接地した主人公のその場・+X・-X・+Z・-Z方向のジャンプを120Hzで計算する。
	/// 着地・落下判定高度・3秒で終了し、4更新ごとと終了時の位置を記録する
	/// </summary>
	/// <param name="scene">変更しない編集シーン。非表示のColliderも障害物に含める</param>
	/// <param name="id">有効なCharacter ControllerとColliderを持つ接地した物体</param>
	/// <param name="paths">成功時に置き換える軌跡。復帰によるテレポートは含めない</param>
	/// <returns>true:計算成功 false:対象なし、未接地、無効な設定・配置・移動。失敗時は出力を保持する</returns>
	bool BuildPaths(const Scene &scene, SceneObjectId id, Paths &paths) const;

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
