#pragma once

#include <DirectXMath.h>
#include <array>
#include <span>

struct Ray;
class Scene;

/// <summary>
/// モデルをSceneの面や地面へ配置する位置と移動量を求める
/// </summary>
class ModelPlacement {
  public:
	/// <summary>
	/// 配置先の位置と、モデルを押し出す面の外向き法線
	/// </summary>
	struct Surface {
		DirectX::XMFLOAT3 position{};
		DirectX::XMFLOAT3 normal{};
		bool onSurface = false;
	};

	/// <summary>
	/// 最も手前のCollider面を取得する。面がなければ地面、またはレイ上の手前を使用する
	/// </summary>
	/// <param name="ray">描画範囲内の正規化されたワールド座標レイ</param>
	/// <param name="scene">配置先のColliderを持つ編集シーン。モデルの表示状態には依存しない</param>
	/// <param name="surface">成功時に設定する位置と法線。Colliderは物理判定と同じ各軸に平行なボックスを使用する</param>
	/// <param name="planeHeight">Collider面がない場合の水平な作業平面の高さ。有限値</param>
	/// <returns>true:成功 false:レイまたは高さが無効。失敗時は出力を保持する</returns>
	static bool GetSurface(const Ray &ray, const Scene &scene, Surface &surface,
	                       float planeHeight = 0.0f);

	/// <summary>
	/// 回転なし・等倍で追加するモデルの外接ボックスを、配置先の面へ合わせる
	/// </summary>
	/// <param name="surface">取得した配置先。面なしの場合は位置をそのまま使用する</param>
	/// <param name="bounds">モデルのローカル座標の8頂点</param>
	/// <param name="translation">成功時に設定するモデルの移動成分</param>
	/// <returns>true:成功 false:値が無効、または面に接する位置を表現できない。失敗時は出力を保持する</returns>
	static bool
	GetSurfaceTranslation(const Surface &surface,
	                      const std::array<DirectX::XMFLOAT3, 8> &bounds,
	                      DirectX::XMFLOAT3 &translation);

	/// <summary>
	/// レイと地面の交点を求める。交差しない場合はレイ上の手前に配置する
	/// </summary>
	/// <param name="ray">描画範囲内の正規化されたワールド座標レイ</param>
	/// <param name="position">成功した場合に設定する配置位置</param>
	/// <param name="planeHeight">水平な作業平面の高さ。有限値。交差しない場合は従来の手前配置を使用する</param>
	/// <returns>true:成功 false:レイまたは高さが無効。失敗時は出力を保持する</returns>
	static bool GetPosition(const Ray &ray, DirectX::XMFLOAT3 &position,
	                        float planeHeight = 0.0f);

	/// <summary>
	/// 全ボックスの最下端を地面へ合わせる、共通の上下移動量を求める。微小な誤差は移動なしとする
	/// </summary>
	/// <param name="bounds">回転・拡縮を反映済みのワールド座標の8頂点一覧</param>
	/// <param name="translation">成功した場合に設定する移動量。XとZは0</param>
	/// <returns>true:成功 false:対象なし、または値が無効。失敗時は出力を変更しない</returns>
	static bool GetGroundTranslation(
	    std::span<const std::array<DirectX::XMFLOAT3, 8>> bounds,
	    DirectX::XMFLOAT3 &translation);

  private:
	static constexpr float kSurfaceClearance = 0.0001f;
	static constexpr float kRoundoffFactor = 4.0f;
	static constexpr float kGroundTolerance = 0.00001f;
	static constexpr float kDefaultDistance = 3.0f;
	static constexpr float kParallelTolerance = 0.000001f;
	static constexpr float kDirectionTolerance = 0.001f;
};
