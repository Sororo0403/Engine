#pragma once

#include <cstddef>
#include <cstdint>

class RenderWorld;

/// <summary>
/// 描画物体を識別する情報を保持する
/// </summary>
class RenderObjectHandle {
  public:
	RenderObjectHandle() = default;

	/// <summary>
	/// 同じ描画物体の識別情報を持つか比較する
	/// </summary>
	/// <param name="other">比較するハンドル</param>
	/// <returns>true:一致 false:不一致</returns>
	bool operator==(const RenderObjectHandle &other) const = default;

  private:
	friend class RenderWorld;

	// 生成元のワールドを識別する。0は未設定
	std::uint64_t worldId_ = 0;

	// ワールド内の物体を格納する位置
	std::size_t index_ = 0;

	// 同じ格納位置で作り直された物体を区別する。0は未設定
	std::uint64_t generation_ = 0;
};
