#pragma once

#include "scene/SceneObjectId.h"
#include <filesystem>
#include <string>
#include <vector>

class Scene;
class EditorHistory;

/// <summary>
/// 1つの根と全子孫をPrefabアセットとして保存し、独立した物体の組として配置する
/// </summary>
class PrefabAsset {
  public:
	/// <summary>
	/// Prefab専用の標準ファイル名か確認する。内容の検証は行わない
	/// </summary>
	/// <param name="path">確認するファイル名</param>
	/// <returns>true:.prefab.json形式 false:それ以外</returns>
	static bool IsPrefabPath(const std::filesystem::path &path);

	/// <summary>
	/// 根をワールド原点へ移した、範囲外の物体参照を持たない階層を作る
	/// </summary>
	/// <param name="source">変更しない編集シーン</param>
	/// <param name="root">保存する根。子孫も含める</param>
	/// <param name="result">成功時だけ置き換える独立した階層</param>
	/// <param name="error">成功時は空、失敗時は理由</param>
	/// <returns>true:成功 false:対象・階層・変形・参照が無効。結果と元のシーンを保持する</returns>
	static bool Capture(const Scene &source, SceneObjectId root, Scene &result,
	                    std::string &error);

	/// <summary>
	/// 階層を新しいPrefabファイルへ保存する。モデル・画像・WAVの参照は保存先基準にする
	/// </summary>
	/// <param name="source">変更しない編集シーン</param>
	/// <param name="root">保存する根</param>
	/// <param name="path">新規の.prefab.json。親フォルダは事前に作成する</param>
	/// <param name="error">成功時は空、失敗時は理由</param>
	/// <returns>true:保存成功 false:検証または保存失敗。既存ファイルは上書きしない</returns>
	static bool Save(const Scene &source, SceneObjectId root,
	                 const std::filesystem::path &path, std::string &error);

	/// <summary>
	/// Prefabの階層・内部参照を検証し、素材参照を絶対パスへ解決する
	/// </summary>
	/// <param name="path">絶対パスまたは実行ファイル基準のPrefab</param>
	/// <param name="result">成功時だけ置き換える階層。根の保存時のIDを保持する</param>
	/// <param name="error">成功時は空、失敗時は理由</param>
	/// <param name="nextSourceId">指定時は次の元IDを成功時だけ設定する。旧形式では最大IDから求める</param>
	/// <returns>true:読み込み成功 false:形式・設定・参照が無効。以前の結果を保持する</returns>
	static bool Load(const std::filesystem::path &path, Scene &result,
	                 std::string &error, SceneObjectId *nextSourceId = nullptr);

	/// <summary>
	/// 原点を準備済みの階層をPrefab形式v2へ変換し、削除した元IDを再利用しない番号を保存する
	/// </summary>
	/// <param name="source">Prefabリンクを含まない、単独の根を持つ階層</param>
	/// <param name="root">保存する根のID</param>
	/// <param name="path">素材参照の基準に使うPrefab保存先</param>
	/// <param name="text">成功時だけ置き換えるJSON</param>
	/// <param name="error">失敗理由</param>
	/// <param name="nextSourceId">次に使う元ID。0は最大IDの次を選ぶ</param>
	/// <returns>true:変換成功 false:設定・根・参照が無効。ファイルには書き込まない</returns>
	static bool Serialize(const Scene &source, SceneObjectId root,
	                      const std::filesystem::path &path, std::string &text,
	                      std::string &error, SceneObjectId nextSourceId = 0);

	/// <summary>
	/// 素材と階層を検証し、内部IDを付け替えて1回のUndoで取り消せる組を作る
	/// </summary>
	/// <param name="source">読み込み・配置済みのPrefab階層</param>
	/// <param name="history">配置先の履歴。シーンの変更と選択は呼び出し側で管理する</param>
	/// <param name="error">成功時は空、失敗時は理由</param>
	/// <returns>追加したID一覧。失敗時は空で、配置先のシーンと履歴を保持する</returns>
	static std::vector<SceneObjectId> Instantiate(const Scene &source,
	                                              EditorHistory &history,
	                                              std::string &error);
};
