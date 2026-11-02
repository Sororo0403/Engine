#pragma once

#include <DirectXMath.h>
#include <array>

struct SceneObject;

/// <summary>
/// 描画用のMeshとMesh Rendererがそろっていない物体へ編集用カメラを寄せるための範囲を求める
/// </summary>
class ObjectFrameBounds {
  public:
	using Corners = std::array<DirectX::XMFLOAT3, 8>;

	/// <summary>
	/// Colliderがあればワールド座標の形状を使用し、なければ原点を囲む単位ボックスを作る。
	/// 原点の単位ボックスは物体の回転・拡大率に依存せず、描画の表示状態も参照しない
	/// </summary>
	/// <param name="object">描画用のMeshとMesh Rendererがそろっていない編集物体。値は変更しない</param>
	/// <param name="corners">成功時に置き換える8頂点。失敗時は保持する</param>
	/// <returns>true:取得成功 false:描画用のMeshとMesh Rendererあり、変形やColliderが無効、または範囲を表現できない</returns>
	static bool Get(const SceneObject &object, Corners &corners);

  private:
	static constexpr float kOriginHalfExtent = 0.5f;
};
