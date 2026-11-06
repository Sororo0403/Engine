#pragma once

/// <summary>
/// 物体のワールド位置と向きを音の受信姿勢にする。同時に有効なリスナーは1つまで
/// </summary>
struct AudioListenerComponent {
	/// <summary>
	/// 受信位置はTransformから取得し、このコンポーネント自身には追加設定を持たない
	/// </summary>
	/// <returns>常にtrue。重複とワールド姿勢はシーン全体で検証する</returns>
	bool IsValid() const {
		return true;
	}

	/// <summary>
	/// 設定を持たないリスナー同士を比較する
	/// </summary>
	/// <param name="other">比較するリスナー</param>
	/// <returns>常にtrue</returns>
	bool operator==(const AudioListenerComponent &other) const = default;
};
