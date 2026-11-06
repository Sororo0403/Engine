#pragma once

#include "animation/ModelAnimationData.h"
#include <memory>
#include <optional>

/// <summary>
/// 1回の評価結果。ノードとメッシュの番号は共有データと一致する
/// </summary>
struct ModelAnimationPose {
	std::vector<DirectX::XMFLOAT4X4> nodes;
	std::vector<std::vector<DirectX::XMFLOAT4X4>> bones;
};

/// <summary>
/// 検証済みの共有データを参照し、時刻ごとのノードとスキン行列を計算する
/// </summary>
class ModelAnimationSampler {
  public:
	/// <summary>
	/// データを検証し、成功した場合だけ共有参照を置き換える
	/// </summary>
	/// <param name="data">以後は変更しない骨・形状・クリップ</param>
	/// <param name="error">失敗理由</param>
	/// <returns>true:成功 false:データなし、または無効</returns>
	bool Initialize(std::shared_ptr<const ModelAnimationData> data,
	                std::string &error);

	/// <summary>
	/// 指定時刻の行列を評価し、すべて成功した場合だけ結果を置き換える
	/// </summary>
	/// <param name="clip">クリップ番号。nulloptはバインド姿勢</param>
	/// <param name="seconds">速度反映済みの有限な0以上の秒数</param>
	/// <param name="loop">true:末尾で先頭へ戻る false:末尾の姿勢を保持する</param>
	/// <param name="pose">成功時に設定するモデル座標の行列</param>
	/// <param name="error">失敗理由</param>
	/// <returns>true:成功 false:未初期化・時刻・番号・評価結果が無効</returns>
	bool Sample(std::optional<std::size_t> clip, double seconds, bool loop,
	            ModelAnimationPose &pose, std::string &error) const;

	/// <summary>
	/// 評価した行列で位置・法線をCPU変形する。重みなしの頂点は所属ノードへ従う
	/// </summary>
	/// <param name="meshIndex">共有データのメッシュ番号</param>
	/// <param name="pose">同じデータから評価した行列</param>
	/// <param name="mesh">成功時だけ置き換えるモデル座標の形状</param>
	/// <param name="error">失敗理由</param>
	/// <returns>true:成功 false:未初期化・番号・姿勢・法線の変換が無効</returns>
	bool Deform(std::size_t meshIndex, const ModelAnimationPose &pose,
	            MeshData &mesh, std::string &error) const;

  private:
	std::shared_ptr<const ModelAnimationData> data_;
};
