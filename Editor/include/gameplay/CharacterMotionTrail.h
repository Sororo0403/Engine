#pragma once

#include "graphics/render/DebugLine.h"
#include "scene/SceneObjectId.h"
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>

class EditorPlaySession;
class SceneRenderer;

/// <summary>
/// 再生中の主人公の原点を更新フレームごとに記録し、Sceneビューだけに軌跡を描く
/// </summary>
class CharacterMotionTrail {
  public:
	static constexpr std::size_t kMaximumSegments = 2048;

	/// <summary>
	/// 記録と次の線の始点を破棄する。再生状態やシーンは変更しない
	/// </summary>
	void Clear() noexcept;

	/// <summary>
	/// 確定した再生状態を取り込む。同じフレームや同じ位置では線を増やさない。
	/// 再実行・主人公の変更では記録を初期化し、落下復帰の前後は線をつながない
	/// </summary>
	/// <param name="session">参照する再生状態。停止中・主人公なし・非有限な位置では記録を消す</param>
	void Record(const EditorPlaySession &session);

	/// <summary>
	/// 保持している線の数を取得する。上限を超えると最も古い線を捨てる
	/// </summary>
	/// <returns>0からkMaximumSegmentsまでの線数</returns>
	std::size_t GetLineCount() const noexcept;

	/// <summary>
	/// 古い順の線を取得する
	/// </summary>
	/// <param name="index">0から始まる線の番号</param>
	/// <returns>次のRecord・Clearまで有効な線。範囲外はnullptr</returns>
	const DebugLine *GetLine(std::size_t index) const noexcept;

	/// <summary>
	/// 接地中は緑、空中は橙の記録線をScene描画へ登録する
	/// </summary>
	/// <param name="renderer">更新済みのScene描画先。再生用のGame描画先は渡さない</param>
	/// <returns>true:登録成功または線なし false:線の登録失敗</returns>
	bool Update(SceneRenderer &renderer) const;

  private:
	std::deque<DebugLine> lines_;
	std::optional<DirectX::XMFLOAT3> previous_;
	SceneObjectId character_ = 0;
	std::uint64_t frame_ = 0;
	std::uint64_t respawns_ = 0;
};
