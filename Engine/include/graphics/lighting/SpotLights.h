#pragma once

#include "graphics/lighting/SpotLightData.h"
#include "graphics/buffer/ConstantBuffer.h"
#include <array>
#include <cstdint>
#include <span>

/// <summary>
/// 1描画ワールドのスポットライトをまとめてGPUへ転送する
/// </summary>
class SpotLights {
  public:
	/// <summary>
	/// 空の光源配列で定数バッファを初期化する
	/// </summary>
	/// <param name="pool">描画基盤の共有定数バッファ</param>
	/// <returns>true:成功 false:未指定・初期化済み・確保失敗</returns>
	bool Initialize(ConstantBufferPool &pool);

	/// <summary>
	/// 全光源を転送し、削除された要素も空に戻す
	/// </summary>
	/// <param name="lights">上限以内の有効なワールド座標の光源</param>
	/// <returns>true:成功 false:未初期化・上限超過・無効な光源</returns>
	bool Update(std::span<const SpotLightData> lights);

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
		std::array<SpotLightData, SpotLightData::kMaximumLights> lights{};
	};

	ConstantBuffer<Constants> constantBuffer_;
};
