#pragma once
#include "scene/SceneObjectId.h"
#include <filesystem>
#include <string>
#include <string_view>
class Scene;

/// <summary>
/// EditorとRuntimeで共有するPrefabの階層検証・保存形式・素材パス解決を扱う
/// </summary>
class PrefabSerializer {
  public:
	/// <summary>
	/// 呼び出し側で取得・検証したPrefabのバイト列を読み込み、素材パスを保存先基準で解決する
	/// </summary>
	/// <param name="text">Prefab形式v1またはv2のJSON</param>
	/// <param name="path">素材パスの基準になるPrefabファイル名</param>
	/// <param name="result">成功時だけ置き換える階層</param>
	/// <param name="error">成功時は空、失敗時は理由</param>
	/// <param name="nextSourceId">指定時は次の元IDを成功時だけ設定する</param>
	/// <returns>true:成功 false:形式・階層・設定・カウンターが無効。出力を保持する</returns>
	static bool Deserialize(std::string_view text,
	                        const std::filesystem::path &path, Scene &result,
	                        std::string &error,
	                        SceneObjectId *nextSourceId = nullptr);

	/// <summary>
	/// Prefab専用の標準ファイル名か確認する。内容の検証は行わない
	/// </summary>
	/// <param name="path">確認するファイル名</param>
	/// <returns>true:.prefab.json形式 false:それ以外</returns>
	static bool IsPrefabPath(const std::filesystem::path &path);

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
	/// 単独の根・内部参照・コンポーネント設定を検証する。ファイルには触れない
	/// </summary>
	/// <param name="scene">変更しないPrefab階層。Prefabリンクは含めない</param>
	/// <param name="root">成功時だけ設定する単独の根のID</param>
	/// <param name="error">成功時は空、失敗時は理由</param>
	/// <returns>true:有効 false:空・複数の根・範囲外の参照・設定が無効。rootは保持する</returns>
	static bool Validate(const Scene &scene, SceneObjectId &root,
	                     std::string &error);
};
