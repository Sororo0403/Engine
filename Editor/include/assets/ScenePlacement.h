#pragma once

#include "assets/ModelPlacement.h"
#include <optional>

class Scene;
class ModelManager;
struct Ray;

/// <summary>
/// 保存した物体の組を、相互の配置関係を保ったままScene上の面へ移動する
/// </summary>
class ScenePlacement {
  public:
	/// <summary>
	/// 最も手前のCollider面・地面・レイ上の手前へ、組全体の形状の中心を合わせて配置する
	/// </summary>
	/// <param name="objects">移動する独立したシーン。成功時だけ変形とRespawnの世界座標を更新する</param>
	/// <param name="target">配置先。形状の取得だけに使い、変更しない</param>
	/// <param name="ray">Scene画像から作成した正規化済みレイ</param>
	/// <param name="models">表示するモデルの範囲を取得する描画基盤。GPUの利用完了後に呼ぶ</param>
	/// <param name="grid">指定時は組の中心を面に沿う軸だけスナップする。各軸の間隔は正の有限値</param>
	/// <returns>true:全物体の配置成功 false:空、無効な設定、モデル取得失敗、または配置を表現できない</returns>
	/// <remarks>形状はColliderと表示モデルを優先し、なければ原点を使う。回転・拡大率・ローカル設定は保持する</remarks>
	static bool Place(Scene &objects, const Scene &target, const Ray &ray,
	                  ModelManager &models,
	                  std::optional<DirectX::XMFLOAT3> grid = std::nullopt);

  private:
	static constexpr float kSurfaceClearance = 0.0001f;
	static constexpr float kRoundoffFactor = 8.0f;
};
