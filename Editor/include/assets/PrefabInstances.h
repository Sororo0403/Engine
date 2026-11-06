#pragma once

#include "scene/SceneObjectId.h"
#include <filesystem>
#include <string>
#include <vector>
class Scene;
class EditorHistory;

/// <summary>
/// 元アセット・元ID・基準値を保持し、配置後の編集を保ったPrefab更新を履歴へ記録する
/// </summary>
class PrefabInstances {
  public:
	/// <summary>
	/// 物体が属する有効なPrefabの根を取得する
	/// </summary>
	/// <param name="scene">確認するシーン</param>
	/// <param name="member">根または子のID</param>
	/// <returns>所属する根。未リンクまたは根がない場合は0</returns>
	static SceneObjectId GetRoot(const Scene &scene, SceneObjectId member);

	/// <summary>
	/// 配置済みのアセット階層へリンクを付け、新しいIDで1回のUndoとして追加する
	/// </summary>
	/// <param name="asset">保存元のPrefabパス</param>
	/// <param name="placed">読み込み・配置済みの独立したアセット階層</param>
	/// <param name="history">配置先の履歴</param>
	/// <param name="error">失敗理由</param>
	/// <returns>追加したID。失敗時は空で、シーンと履歴を保持する</returns>
	static std::vector<SceneObjectId>
	Instantiate(const std::filesystem::path &asset, const Scene &placed,
	            EditorHistory &history, std::string &error);

	/// <summary>
	/// 最新アセットを反映し、通常は個別の値・追加・削除を保つ。根の配置は常に保つ
	/// </summary>
	/// <param name="history">編集対象の履歴</param>
	/// <param name="member">更新する根または子</param>
	/// <param name="error">失敗理由</param>
	/// <param name="revert">true:根のTransform以外の個別編集を取り消す</param>
	/// <param name="allInstances">true:同じアセットの全配置を1操作で更新する</param>
	/// <returns>true:成功または同値 false:設定・素材・階層が無効。失敗時は全物体と履歴を保持する</returns>
	static bool Refresh(EditorHistory &history, SceneObjectId member,
	                    std::string &error, bool revert = false,
	                    bool allInstances = false);

	/// <summary>
	/// 現在の階層をアセットへ保存し、同じアセットの配置を更新する。外部変更がある場合は保存しない
	/// </summary>
	/// <param name="history">編集対象の履歴</param>
	/// <param name="member">保存する根または子</param>
	/// <param name="error">失敗理由。保存後の同期だけが失敗した場合は、その状態を明記する</param>
	/// <returns>true:保存と同期の成功 false:未リンク・外部変更・書き込みや同期失敗</returns>
	/// <remarks>ファイル保存はシーンのUndoでは取り消さない。根の配置はアセットへ適用しない</remarks>
	static bool SaveToAsset(EditorHistory &history, SceneObjectId member,
	                        std::string &error);

	/// <summary>
	/// 根と全所属物体を、配置・内部参照を保った通常の物体へ戻す
	/// </summary>
	/// <param name="history">編集対象の履歴</param>
	/// <param name="member">解除する根または子</param>
	/// <param name="error">失敗理由</param>
	/// <returns>true:解除成功 false:未リンクまたは変更失敗。1回のUndoでリンクを復元できる</returns>
	static bool Unpack(EditorHistory &history, SceneObjectId member,
	                   std::string &error);
};
