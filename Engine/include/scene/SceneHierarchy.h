#pragma once

#include "scene/SceneObjectId.h"
#include "scene/SceneWorldTransform.h"

#include <map>
#include <span>
#include <vector>

class Scene;

namespace SceneHierarchy {
using WorldTransforms = std::map<SceneObjectId, SceneWorldTransform>;

/// <summary>
/// 親を先に評価する順序を作り、親の存在と循環のないことを確認する
/// </summary>
/// <param name="scene">確認するシーン。変更しない</param>
/// <param name="order">成功時だけ置き換える、全物体の評価順</param>
/// <returns>true:有効 false:親が存在しない、または循環している</returns>
bool GetEvaluationOrder(const Scene &scene, std::vector<SceneObjectId> &order);

/// <summary>
/// 直下の子を物体IDの昇順で列挙する
/// </summary>
/// <param name="scene">確認するシーン</param>
/// <param name="parent">親のID。0はルート物体の一覧</param>
/// <returns>直下の子。存在しなければ空</returns>
std::vector<SceneObjectId> GetChildren(const Scene &scene,
                                       SceneObjectId parent);

/// <summary>
/// 指定した物体と全子孫を、親から先の順序で列挙する
/// </summary>
/// <param name="scene">読み取り専用のシーン</param>
/// <param name="id">既存の物体ID</param>
/// <param name="objects">成功時だけ置き換える、対象と全子孫のID一覧</param>
/// <returns>true:成功 false:物体または親子関係が無効</returns>
bool GetSubtree(const Scene &scene, SceneObjectId id,
                std::vector<SceneObjectId> &objects);

/// <summary>
/// 選択内に祖先がある物体を除き、操作を一度だけ適用する最上位の対象を取得する
/// </summary>
/// <param name="scene">親子関係を参照するシーン</param>
/// <param name="ids">重複のない既存物体のID一覧</param>
/// <param name="roots">成功時だけ置き換える対象。入力順を保持する</param>
/// <returns>true:成功 false:対象なし、重複、不在ID、または無効な親子関係</returns>
bool GetSelectionRoots(const Scene &scene, std::span<const SceneObjectId> ids,
                       std::vector<SceneObjectId> &roots);

/// <summary>
/// 選択した物体とその全子孫を重複なく、親から先の順序で取得する
/// </summary>
/// <param name="scene">親子関係を参照するシーン</param>
/// <param name="ids">重複のない既存物体のID一覧</param>
/// <param name="objects">成功時だけ置き換える、親から先の順序の対象一覧</param>
/// <returns>true:成功 false:選択や親子関係が無効</returns>
bool ExpandSelection(const Scene &scene, std::span<const SceneObjectId> ids,
                     std::vector<SceneObjectId> &objects);

/// <summary>
/// 全物体のローカル変形を親から順に合成する
/// </summary>
/// <param name="scene">読み取り専用のシーン</param>
/// <param name="transforms">成功時だけ置き換えるワールド変形の対応表</param>
/// <returns>true:成功 false:親子関係や変形が無効、または逆行列を有限値で表現できない</returns>
bool BuildWorldTransforms(const Scene &scene, WorldTransforms &transforms);

/// <summary>
/// 指定物体と祖先だけをたどり、ワールド行列と向きを取得する
/// </summary>
/// <param name="scene">読み取り専用のシーン</param>
/// <param name="id">既存の物体ID</param>
/// <param name="transform">成功時だけ置き換える合成結果</param>
/// <returns>true:成功 false:物体・親子関係・変形が無効</returns>
bool GetWorldTransform(const Scene &scene, SceneObjectId id,
                       SceneWorldTransform &transform);

/// <summary>
/// 循環を作らずに親を変更する。失敗時は親とローカル変形を保持する
/// </summary>
/// <param name="scene">変更するシーン</param>
/// <param name="id">親を変更する既存物体</param>
/// <param name="parent">新しい親。0はルート</param>
/// <param name="keepWorld">true:ワールド行列を維持 false:ローカル変形を維持</param>
/// <returns>true:変更成功、または同じ親 false:不正な親子関係、または必要なローカル変形がSRTで表現できない</returns>
bool SetParent(Scene &scene, SceneObjectId id, SceneObjectId parent,
               bool keepWorld = true);

/// <summary>
/// 親を維持したまま、指定したワールド行列をローカルSRTへ変換して反映する
/// </summary>
/// <param name="scene">変更するシーン</param>
/// <param name="id">既存の物体ID</param>
/// <param name="matrix">反映するワールド行列</param>
/// <returns>true:成功 false:関係や行列が無効、またはローカルSRTで表現できない。失敗時は現在値を保持する</returns>
bool SetWorldTransform(Scene &scene, SceneObjectId id,
                       const DirectX::XMFLOAT4X4 &matrix);

/// <summary>
/// ローカルの回転と拡縮を保ち、ワールド座標で位置を指定する
/// </summary>
/// <param name="scene">変更するシーン</param>
/// <param name="id">既存の物体ID</param>
/// <param name="position">有限なワールド座標</param>
/// <returns>true:成功 false:親子関係や位置が無効。失敗時は現在値を保持する</returns>
bool SetWorldPosition(Scene &scene, SceneObjectId id,
                      const DirectX::XMFLOAT3 &position);
/// <summary>
/// ワールドの位置と向きをローカルへ変換する。ローカルの拡縮は保持する
/// </summary>
/// <param name="scene">変更するシーン</param>
/// <param name="id">既存の物体ID</param>
/// <param name="position">有限なワールド位置</param>
/// <param name="rotation">拡縮を含めないワールドの単位クォータニオン</param>
/// <returns>true:成功 false:親子関係や姿勢が無効。失敗時は現在値を保持する</returns>
bool SetWorldPose(Scene &scene, SceneObjectId id,
                  const DirectX::XMFLOAT3 &position,
                  const DirectX::XMFLOAT4 &rotation);
} // namespace SceneHierarchy
