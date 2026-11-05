#pragma once

#include "graphics/DirectionalLightData.h"
#include "graphics/PointLightData.h"
#include "graphics/camera/CameraPose.h"
#include "graphics/camera/CameraSettings.h"
#include "graphics/material/ColorTint.h"
#include "graphics/material/MaterialData.h"
#include "graphics/material/MaterialHandle.h"
#include "graphics/model/ModelHandle.h"
#include "graphics/render/DebugLine.h"
#include "graphics/render/RenderObjectHandle.h"
#include "math/Transform.h"

#include <DirectXMath.h>
#include <array>
#include <cstddef>
#include <memory>
#include <span>

class D3D12Graphics;
class MeshRenderer;
class RenderSystem;
class DebugLineRenderer;
struct Ray;

/// <summary>
/// シーンごとの描画物体・カメラ・ライトを管理する
/// </summary>
class RenderWorld {
  public:
	~RenderWorld();

	RenderWorld(const RenderWorld &) = delete;
	RenderWorld &operator=(const RenderWorld &) = delete;

	/// <summary>
	/// 共有モデルから描画物体を作成する。GPUの利用完了後、描画命令の記録前に呼び出す
	/// </summary>
	/// <param name="model">同じ描画基盤で読み込んだモデル</param>
	/// <returns>成功:物体のハンドル 失敗:未設定のハンドル</returns>
	RenderObjectHandle CreateObject(const ModelHandle &model);

	/// <summary>
	/// ハンドルが、このワールドに存在する物体を指しているか取得する
	/// </summary>
	/// <param name="handle">確認するハンドル</param>
	/// <returns>true:物体あり false:未設定、削除済み、または別ワールド</returns>
	bool Contains(RenderObjectHandle handle) const;

	/// <summary>
	/// 描画物体を削除する。GPUの利用完了後、描画命令の記録前に呼び出す
	/// </summary>
	/// <param name="handle">削除する物体のハンドル</param>
	/// <returns>true:削除成功 false:ハンドルが無効</returns>
	bool DestroyObject(RenderObjectHandle handle);

	/// <summary>
	/// 編集用のSRTを取得し、行列指定を解除する。無効なハンドルやSRTで表せない行列の場合は例外
	/// </summary>
	/// <param name="handle">物体のハンドル</param>
	/// <returns>物体削除まで有効な変形情報への参照</returns>
	Transform &GetTransform(RenderObjectHandle handle);

	/// <summary>
	/// 読み取り専用のSRTを取得する。無効なハンドルやSRTで表せない行列の場合は例外
	/// </summary>
	/// <param name="handle">物体のハンドル</param>
	/// <returns>物体削除まで有効な変形情報への読み取り専用参照</returns>
	const Transform &GetTransform(RenderObjectHandle handle) const;

	/// <summary>
	/// SRTを設定し、ワールド行列による指定を解除する
	/// </summary>
	/// <param name="handle">このワールドの物体</param>
	/// <param name="transform">有効な拡大率・回転・移動</param>
	/// <returns>true:成功 false:物体または変形が無効。失敗時は現在値を保持する</returns>
	bool SetTransform(RenderObjectHandle handle, const Transform &transform);

	/// <summary>
	/// せん断を含むワールド行列を、そのまま描画と選択判定へ設定する
	/// </summary>
	/// <param name="handle">このワールドの物体</param>
	/// <param name="matrix">有限な可逆アフィン行列</param>
	/// <returns>true:成功 false:物体または行列が無効。失敗時は現在値を保持する</returns>
	bool SetWorldMatrix(RenderObjectHandle handle,
	                    const DirectX::XMFLOAT4X4 &matrix);

	/// <summary>
	/// 現在のCPU側のワールド行列を取得する。更新前でも取得できる
	/// </summary>
	/// <param name="handle">このワールドの物体</param>
	/// <param name="matrix">成功時だけ置き換える行列</param>
	/// <returns>true:成功 false:物体または変形が無効</returns>
	bool GetWorldMatrix(RenderObjectHandle handle,
	                    DirectX::XMFLOAT4X4 &matrix) const;

	/// <summary>
	/// 物体の材質数を取得する。無効なハンドルの場合は例外を送出する
	/// </summary>
	/// <param name="handle">物体のハンドル</param>
	/// <returns>材質数</returns>
	std::size_t GetMaterialCount(RenderObjectHandle handle) const;

	/// <summary>
	/// 描画物体の材質スロットに独立した共有材質を割り当てる。GPU完了後に呼び出す
	/// </summary>
	/// <param name="handle">このワールドの描画物体</param>
	/// <param name="slot">モデル内の材質スロット番号</param>
	/// <param name="material">同じデバイスの共有材質。未設定はモデルの既定材質へ戻す</param>
	/// <returns>true:成功 false:物体・スロット・描画基盤が無効</returns>
	bool SetMaterial(RenderObjectHandle handle, std::size_t slot,
	                 const MaterialHandle &material);

	/// <summary>
	/// 物体全体の材質へ掛ける色を設定する。元の材質や共有モデルは変更しない
	/// </summary>
	/// <param name="handle">このワールドの描画物体</param>
	/// <param name="tint">RGBが0から1の範囲にある有効な設定</param>
	/// <returns>true:設定成功 false:物体や設定が無効。失敗時は現在の設定を保持する</returns>
	bool SetColorTint(RenderObjectHandle handle, const ColorTint &tint);

	/// <summary>
	/// 物体に保持している色の設定を取得する。無効なハンドルの場合は例外を送出する
	/// </summary>
	/// <param name="handle">このワールドの描画物体</param>
	/// <returns>現在の色の設定</returns>
	ColorTint GetColorTint(RenderObjectHandle handle) const;

	/// <summary>
	/// 編集用の材質情報を取得する。無効なハンドルや材質番号の場合は例外を送出する
	/// </summary>
	/// <param name="handle">物体のハンドル</param>
	/// <param name="materialIndex">0以上、GetMaterialCount()未満の材質番号</param>
	/// <returns>物体削除まで有効な材質情報への参照</returns>
	MaterialData &GetMaterial(RenderObjectHandle handle,
	                          std::size_t materialIndex);

	/// <summary>
	/// 読み取り専用の材質情報を取得する。無効なハンドルや材質番号の場合は例外を送出する
	/// </summary>
	/// <param name="handle">物体のハンドル</param>
	/// <param name="materialIndex">0以上、GetMaterialCount()未満の材質番号</param>
	/// <returns>物体削除まで有効な材質情報への読み取り専用参照</returns>
	const MaterialData &GetMaterial(RenderObjectHandle handle,
	                                std::size_t materialIndex) const;

	/// <summary>
	/// 編集用のライト情報を取得する
	/// </summary>
	/// <returns>ライト情報への参照</returns>
	DirectionalLightData &GetLight();

	/// <summary>
	/// ワールド座標の点光源を一括設定する。更新成功前の設定では描画しない
	/// </summary>
	/// <param name="lights">最大32灯の有効な光源。空は全消灯</param>
	/// <returns>true:成功 false:未初期化、上限超過、または無効な設定。失敗時は現在値を保持する</returns>
	bool SetPointLights(std::span<const PointLightData> lights);

	/// <summary>
	/// 読み取り専用のライト情報を取得する
	/// </summary>
	/// <returns>ライト情報への読み取り専用参照</returns>
	const DirectionalLightData &GetLight() const;

	/// <summary>
	/// カメラの位置と注視点を同じ量だけ移動する。無効な移動量の場合は例外を送出する
	/// </summary>
	/// <param name="offset">各成分が有限値の移動量</param>
	void MoveCamera(const DirectX::XMFLOAT3 &offset);

	/// <summary>
	/// 全物体を削除し、カメラとライト情報を戻す。GPUの利用完了後に呼び出す
	/// </summary>
	void Clear();

	/// <summary>
	/// 描画に使用するカメラの姿勢を設定する
	/// </summary>
	/// <param name="pose">設定する姿勢</param>
	/// <returns>true:成功 false:姿勢が無効</returns>
	bool SetCameraPose(const CameraPose &pose);

	/// <summary>
	/// 描画カメラの投影設定を次の更新へ反映する
	/// </summary>
	/// <param name="settings">有効な視野角と表示距離</param>
	/// <returns>true:成功 false:未初期化、または設定が無効</returns>
	bool SetCameraSettings(const CameraSettings &settings);

	/// <summary>
	/// 画像内の位置から、最も近くで交差する描画物体を取得する
	/// </summary>
	/// <param name="imageX">画像内の横位置。左端0、右端1</param>
	/// <param name="imageY">画像内の縦位置。上端0、下端1</param>
	/// <param name="distance">指定時はレイ始点から交点までのワールド距離を設定する。交差なしの場合は保持する</param>
	/// <param name="excluded">交差判定から除く描画物体。他ワールド・削除済みのハンドルは影響しない</param>
	/// <returns>除外されていない物体のハンドル。交差なし、または更新前の場合は未設定</returns>
	/// <remarks>Update成功後、Drawより前に呼び出す</remarks>
	RenderObjectHandle
	Pick(float imageX, float imageY, float *distance = nullptr,
	     std::span<const RenderObjectHandle> excluded = {}) const;

	/// <summary>
	/// 更新済みの描画カメラから、画像内の位置を通るレイを作成する
	/// </summary>
	/// <param name="imageX">画像内の横位置。左端0、右端1</param>
	/// <param name="imageY">画像内の縦位置。上端0、下端1</param>
	/// <param name="ray">成功した場合に設定するワールド座標のレイ</param>
	/// <returns>true:成功 false:更新前、または位置が無効</returns>
	bool CreatePickingRay(float imageX, float imageY, Ray &ray) const;

	/// <summary>
	/// 更新済みの描画カメラの行列を取得する
	/// </summary>
	/// <param name="view">成功した場合に設定するビュー行列</param>
	/// <param name="projection">成功した場合に設定する投影行列</param>
	/// <returns>true:成功 false:未初期化、または更新前</returns>
	/// <remarks>Update成功後、Drawより前に呼び出す</remarks>
	bool GetCameraMatrices(DirectX::XMFLOAT4X4 &view,
	                       DirectX::XMFLOAT4X4 &projection) const;

	/// <summary>
	/// 物体を囲むボックスの頂点をワールド座標で取得する
	/// </summary>
	/// <param name="handle">対象物体のハンドル</param>
	/// <param name="corners">成功した場合に設定する8頂点</param>
	/// <returns>true:成功 false:物体、モデル、または変形情報が無効</returns>
	bool GetObjectBounds(RenderObjectHandle handle,
	                     std::array<DirectX::XMFLOAT3, 8> &corners) const;

	/// <summary>
	/// 今回の描画で表示する線を登録する
	/// </summary>
	/// <param name="line">ワールド座標の線</param>
	/// <returns>true:成功 false:更新前、または座標・色が無効</returns>
	/// <remarks>最後のUpdate成功後に登録し、全て登録後にUpdateDebugLinesを呼び出す</remarks>
	bool AddDebugLine(const DebugLine &line);

	/// <summary>
	/// 登録された全ての線をGPU用の頂点バッファへ転送する。
	/// GPUの利用完了後、BeginFrame前に呼び出す
	/// </summary>
	/// <returns>true:成功 false:更新前、または転送失敗</returns>
	/// <remarks>最後のAddDebugLine後、Drawより前に呼び出す</remarks>
	bool UpdateDebugLines();

  private:
	friend class RenderSystem;

	explicit RenderWorld(D3D12Graphics &graphics);

	/// <summary>
	/// ワールドが使用するGPUリソースを初期化する
	/// </summary>
	/// <returns>true:成功 false:初期化済み、または作成失敗</returns>
	bool Initialize();

	/// <summary>
	/// カメラ・ライト・物体のGPU用データを更新する
	/// </summary>
	/// <param name="width">描画領域の幅</param>
	/// <param name="height">描画領域の高さ</param>
	/// <returns>true:成功 false:未初期化、描画領域が無効、または更新失敗</returns>
	bool Update(int width, int height);

	/// <summary>
	/// 全物体と登録された線の描画命令を記録する
	/// </summary>
	/// <param name="renderer">初期化済みのメッシュ描画クラス</param>
	/// <param name="debugRenderer">初期化済みの線描画クラス</param>
	void Draw(MeshRenderer &renderer, DebugLineRenderer &debugRenderer) const;

	/// <summary>
	/// 指定した描画基盤を使用しているか取得する
	/// </summary>
	/// <param name="graphics">確認する描画基盤</param>
	/// <returns>true:一致 false:不一致</returns>
	bool Uses(const D3D12Graphics &graphics) const;

  private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};
