#pragma once

#include "graphics/buffer/ConstantBuffer.h"
#include <DirectXMath.h>

#include <d3d12.h>

class Camera;
class Mesh;
class Material;

class MeshInstance {
  private:
	/// <summary>
	/// GPUへ渡す、頂点位置と法線の変換行列
	/// </summary>
	struct TransformationConstants {
		DirectX::XMFLOAT4X4 worldViewProjection;
		DirectX::XMFLOAT4X4 worldInverseTranspose;
	};

  public:
	/// <summary>
	/// 使用するメッシュと材質を指定し、物体用の定数バッファを作成する
	/// </summary>
	/// <param name="device">D3D12デバイス</param>
	/// <param name="mesh">初期化済みのメッシュ。この物体より長く保持すること</param>
	/// <param name="material">初期化済みの材質。この物体より長く保持すること</param>
	/// <returns>true:成功 false:引数が無効、初期化済み、または作成失敗</returns>
	bool Initialize(ID3D12Device *device, const Mesh &mesh,
	                const Material &material);

	/// <summary>
	/// 頂点位置と法線の変換行列を定数バッファへ書き込む
	/// </summary>
	/// <param name="camera">更新済みのカメラ</param>
	/// <param name="world">親子階層を合成した有限な可逆アフィン行列</param>
	/// <returns>true:成功 false:未初期化、または行列が無効</returns>
	bool Update(const Camera &camera, const DirectX::XMFLOAT4X4 &world);

	/// <summary>
	/// 使用するメッシュを取得する
	/// </summary>
	/// <returns>メッシュへの借用ポインタ。未初期化の場合はnullptr</returns>
	const Mesh *GetMesh() const;

	/// <summary>
	/// 使用する材質を取得する
	/// </summary>
	/// <returns>材質への借用ポインタ。未初期化の場合はnullptr</returns>
	const Material *GetMaterial() const;

	/// <summary>
	/// 物体用の定数バッファのGPUアドレスを取得する
	/// </summary>
	/// <returns>GPUアドレス。未初期化の場合は0</returns>
	D3D12_GPU_VIRTUAL_ADDRESS GetConstantBufferAddress() const;

	/// <summary>
	/// 定数バッファを解放する。GPUの利用完了後に呼び出す
	/// </summary>
	void Finalize();

  private:
	const Mesh *mesh_ = nullptr;
	const Material *material_ = nullptr;

	ConstantBuffer<TransformationConstants> constantBuffer_;
};
