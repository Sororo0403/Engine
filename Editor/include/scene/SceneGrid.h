#pragma once

#include <DirectXMath.h>

class SceneRenderer;

/// <summary>
/// Sceneビューに表示するグリッドと座標軸を管理する
/// </summary>
class SceneGrid {
  public:
	/// <summary>
	/// グリッドの線間隔・原点から各端までのセル数・強調線の周期
	/// </summary>
	struct Settings {
		float spacing = 1.0f;
		int halfLineCount = 20;
		int majorLineInterval = 5;

		/// <summary>
		/// 設定の値がすべて一致するか確認する
		/// </summary>
		/// <param name="other">比較する設定</param>
		/// <returns>true:一致 false:不一致</returns>
		bool operator==(const Settings &other) const = default;
	};

	static constexpr int kMaximumHalfLineCount = 200;
	static constexpr int kMaximumMajorLineInterval = 200;
	static constexpr float kMaximumSpacing = 10000.0f;

	/// <summary>
	/// 有効なグリッド設定を反映する
	/// </summary>
	/// <param name="settings">正の線間隔と、1以上で上限以下のセル数・周期</param>
	/// <returns>true:成功 false:値が無効。現在の設定は保持する</returns>
	bool SetSettings(const Settings &settings);

	/// <summary>
	/// 現在のグリッド設定を取得する
	/// </summary>
	/// <returns>線間隔・セル数・強調線の周期</returns>
	Settings GetSettings() const;

	/// <summary>
	/// 今回の描画へグリッドと座標軸の線を登録する
	/// </summary>
	/// <param name="renderer">最後の更新が完了したシーン描画機能</param>
	/// <returns>true:成功 false:線の登録失敗</returns>
	bool Update(SceneRenderer &renderer) const;

  private:
	static constexpr float kHeight = 0.0f;
	Settings settings_;

	DirectX::XMFLOAT4 minorColor_{0.25f, 0.25f, 0.25f, 1.0f};
	DirectX::XMFLOAT4 majorColor_{0.4f, 0.4f, 0.4f, 1.0f};

	DirectX::XMFLOAT4 xAxisColor_{0.9f, 0.25f, 0.25f, 1.0f};
	DirectX::XMFLOAT4 zAxisColor_{0.25f, 0.5f, 0.95f, 1.0f};
};
