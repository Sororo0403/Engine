#pragma once

#include <DirectXMath.h>

class SceneRenderer;

/// <summary>
/// Sceneビューに表示するグリッドと座標軸を管理する
/// </summary>
class SceneGrid {
  public:
	/// <summary>
	/// 今回の描画へグリッドと座標軸の線を登録する
	/// </summary>
	/// <param name="renderer">最後の更新が完了したシーン描画機能</param>
	/// <returns>true:成功 false:線の登録失敗</returns>
	bool Update(SceneRenderer &renderer) const;

  private:
	static constexpr int kHalfLineCount = 20;
	static constexpr int kMajorLineInterval = 5;

	static constexpr float kSpacing = 1.0f;
	static constexpr float kHeight = 0.0f;

	static_assert(kHalfLineCount > 0);
	static_assert(kMajorLineInterval > 0);
	static_assert(kSpacing > 0.0f);

	DirectX::XMFLOAT4 minorColor_{0.25f, 0.25f, 0.25f, 1.0f};
	DirectX::XMFLOAT4 majorColor_{0.4f, 0.4f, 0.4f, 1.0f};

	DirectX::XMFLOAT4 xAxisColor_{0.9f, 0.25f, 0.25f, 1.0f};
	DirectX::XMFLOAT4 zAxisColor_{0.25f, 0.5f, 0.95f, 1.0f};
};
