#pragma once

/// <summary>
/// 物体のワールド位置と向きを音の受信姿勢にする
/// </summary>
struct AudioListenerComponent {
	bool preferred = false;

	/// <summary>
	/// 受信位置はTransformから取得する
	/// </summary>
	/// <returns>常にtrue。ワールド姿勢は収集時に検証する</returns>
	bool IsValid() const {
		return true;
	}

	/// <summary>
	/// リスナーの設定を比較する
	/// </summary>
	/// <param name="other">比較するリスナー</param>
	/// <returns>true:設定が一致</returns>
	bool operator==(const AudioListenerComponent &other) const = default;
};
