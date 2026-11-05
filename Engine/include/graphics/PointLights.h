#pragma once

#include "graphics/PointLightData.h"
#include "graphics/buffer/ConstantBuffer.h"
#include <array>
#include <cstdint>
#include <span>

/// <summary>
/// 1描画ワールドの点光源をまとめてGPUへ転送する
/// </summary>
class PointLights {
  public:
	/// <summary>
	/// 空の点光源配列で定数バッファを初期化する
	/// </summary>
	/// <param name="device">描画デバイス</param>
	/// <returns>true:成功 false:未指定、初期化済み、または確保失敗</returns>
	bool Initialize(ID3D12Device *device);

	/// <summary>
	/// 全点光源を転送する。削除された要素も空に戻す
	/// </summary>
	/// <param name="lights">上限以内の有効なワールド座標の光源</param>
	/// <returns>true:成功 false:未初期化、上限超過、または無効な光源</returns>
	bool Update(std::span<const PointLightData> lights);

	/// <summary>
	/// 描画用の定数バッファのGPUアドレスを取得する
	/// </summary>
	/// <returns>GPUアドレス。未初期化時は0</returns>
	D3D12_GPU_VIRTUAL_ADDRESS GetConstantBufferAddress() const;

	/// <summary>
	/// GPUの利用完了後にバッファを解放する
	/// </summary>
	void Finalize();

  private:
	struct Constants {
		std::uint32_t count = 0;
		std::array<std::uint32_t, 3> padding{};
		std::array<PointLightData, PointLightData::kMaximumLights> lights{};
	};

	ConstantBuffer<Constants> constantBuffer_;
};
