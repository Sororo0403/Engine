#pragma once

#include "camera/EditorCameraController.h"
#include <array>
#include <cstddef>
#include <optional>

/// <summary>
/// シーンとは独立した5枠の作業用カメラの視点を保持する
/// </summary>
class EditorCameraBookmarks {
  public:
	static constexpr std::size_t kSlotCount = 5;

	/// <summary>
	/// 指定枠へ検証済みの視点を保存する
	/// </summary>
	/// <param name="slot">0から始まる枠番号</param>
	/// <param name="state">現在のカメラから取得した視点</param>
	/// <returns>true:成功 false:枠または視点が無効。以前の保存値を保持する</returns>
	bool Set(std::size_t slot, const EditorCameraController::ViewState &state);

	/// <summary>
	/// 保存した視点を取得する
	/// </summary>
	/// <param name="slot">0から始まる枠番号</param>
	/// <returns>保存値。空または範囲外の場合はnullptr</returns>
	const EditorCameraController::ViewState *Get(std::size_t slot) const;

	/// <summary>
	/// 指定枠を空にする
	/// </summary>
	/// <param name="slot">0から始まる枠番号</param>
	/// <returns>true:保存値を消去 false:空または範囲外</returns>
	bool Clear(std::size_t slot);

	/// <summary>
	/// 全ての保存値を、カメラへ復元できるか確認する
	/// </summary>
	/// <returns>true:全件有効または空 false:無効な視点あり</returns>
	bool IsValid() const;

	bool operator==(const EditorCameraBookmarks &other) const = default;

  private:
	std::array<std::optional<EditorCameraController::ViewState>, kSlotCount>
	    slots_;
};
