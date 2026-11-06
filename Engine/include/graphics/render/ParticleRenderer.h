#pragma once
#include "particles/ParticleDrawBatch.h"
#include <DirectXMath.h>
#include <memory>
#include <span>
#include <string>
class D3D12Graphics;
class MaterialManager;

/// <summary>
/// ワールドごとの粒子をカメラ向きに並べ、奥から透明合成する
/// </summary>
class ParticleRenderer {
  public:
	/// <summary>
	/// 所有側の描画基盤と画像管理への参照を保持する。GPU作成は必要時に行う
	/// </summary>
	/// <param name="graphics">この描画クラスより長く保持する描画基盤</param>
	/// <param name="materials">同じデバイスの画像管理</param>
	ParticleRenderer(D3D12Graphics &graphics, MaterialManager &materials);
	/// <summary>
	/// GPU利用完了後に画像の保持と描画リソースを解放する
	/// </summary>
	~ParticleRenderer();
	ParticleRenderer(const ParticleRenderer &) = delete;
	ParticleRenderer &operator=(const ParticleRenderer &) = delete;

	/// <summary>
	/// 将来発生する粒子の画像を準備し、再生中に元ファイルが消えても保持する
	/// </summary>
	/// <param name="paths">空を含んでもよい画像参照</param>
	/// <param name="error">失敗理由</param>
	/// <returns>true:成功 false:画像・容量・読み込みが無効</returns>
	bool PrepareImages(std::span<const std::filesystem::path> paths,
	                   std::string &error);

	/// <summary>
	/// GPU完了後・描画前に粒子を検証し、カメラの奥から並べて転送する
	/// </summary>
	/// <param name="batches">ワールド座標で複製した粒子</param>
	/// <param name="view">描画カメラのビュー行列</param>
	/// <param name="projection">描画カメラの投影行列</param>
	/// <param name="error">失敗理由</param>
	/// <returns>true:準備成功 false:データ・画像・GPU準備が無効</returns>
	bool Update(std::span<const ParticleDrawBatch> batches,
	            const DirectX::XMFLOAT4X4 &view,
	            const DirectX::XMFLOAT4X4 &projection, std::string &error);

	/// <summary>
	/// 現在の3D描画先の深度を参照し、深度を書き換えずに粒子を合成する
	/// </summary>
	void Draw() const;

	/// <summary>
	/// GPU完了後に粒子と、事前準備した画像の保持を解放する
	/// </summary>
	void Clear();

  private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};
