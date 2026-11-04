#pragma once

#include "scene/SceneObjectId.h"
#include <DirectXMath.h>
#include <array>

struct SceneObject;
class Scene;

/// <summary>
/// 形状を描画できない物体へ編集用カメラを寄せるための範囲を求める
/// </summary>
class ObjectFrameBounds {
  public:
	using Corners = std::array<DirectX::XMFLOAT3, 8>;

	/// <summary>
	/// Colliderがあればワールド座標の形状を使用し、なければ原点を囲む単位ボックスを作る。
	/// 原点の単位ボックスは物体の回転・拡大率に依存せず、描画の表示状態も参照しない
	/// </summary>
	/// <param name="object">親を持たない編集物体。親付きの物体はscene・idのオーバーロードを使用する</param>
	/// <param name="corners">成功時に置き換える8頂点。失敗時は保持する</param>
	/// <param name="missingModel">モデルの読み込みを省略している物体か</param>
	/// <returns>true:取得成功 false:描画可能なモデルあり、変形やColliderが無効、または範囲を表現できない</returns>
	static bool Get(const SceneObject &object, Corners &corners,
	                bool missingModel = false);

	/// <summary>
	/// 親子合成後のCollider、またはワールド原点を囲む範囲を取得する
	/// </summary>
	/// <param name="scene">物体と祖先を持つシーン</param>
	/// <param name="id">既存物体のID</param>
	/// <param name="corners">成功時だけ置き換える8頂点</param>
	/// <param name="missingModel">モデルが読み込めない物体も対象とするか</param>
	/// <returns>true:成功 false:物体・親子関係・形状が無効、または描画可能なモデルあり</returns>
	static bool Get(const Scene &scene, SceneObjectId id, Corners &corners,
	                bool missingModel = false);

  private:
	/// <summary>
	/// 指定したワールド行列から、Colliderまたは固定サイズの原点範囲を組み立てる
	/// </summary>
	/// <param name="object">表示設定とローカル形状を参照する物体</param>
	/// <param name="world">物体の有効な合成済みワールド行列</param>
	/// <param name="corners">成功時だけ置き換える8頂点</param>
	/// <param name="missingModel">読み込み不能なモデルも対象とするか</param>
	/// <returns>true:成功 false:表示対象なし、または形状・範囲が無効</returns>
	static bool Get(const SceneObject &object, const DirectX::XMFLOAT4X4 &world,
	                Corners &corners, bool missingModel);

	static constexpr float kOriginHalfExtent = 0.5f;
};
