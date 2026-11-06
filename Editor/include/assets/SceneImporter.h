#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
class Scene;
struct GameConfig;

/// <summary>
/// 出力ゲームの起動設定と、Assets内の新規取り込み先を指定する
/// </summary>
struct SceneImportOptions {
	std::filesystem::path source;
	std::filesystem::path assetRoot;
	std::filesystem::path folder;
	std::filesystem::path folderName;
};

/// <summary>
/// 出力ゲームのシーンと参照素材を新規Assetsフォルダへ取り込み、元データと既存アセットを保持する
/// </summary>
class SceneImporter {
  public:
	static constexpr std::uintmax_t kMaximumSceneBytes = 64 * 1024 * 1024;

	/// <summary>
	/// 起動シーンを読み、依存構成を保ってコピーする。シーンの参照だけを取り込み先へ置き換える
	/// </summary>
	/// <param name="options">入力game.jsonと、Assets内の作成先</param>
	/// <param name="imported">成功時に全物体を置き換えるシーン。失敗時は保持する</param>
	/// <param name="createdFolder">成功時に置き換えるAssets検索先基準の作成フォルダ</param>
	/// <param name="error">失敗理由。後片付けできない場合は残った場所も含む</param>
	/// <param name="configuration">指定時は、全取り込み成功後だけ同じgame.jsonから検証した起動設定を置き換える。失敗時は保持する</param>
	/// <returns>true:取り込み成功 false:設定・参照・コピーが無効。既存項目は変更しない</returns>
	/// <remarks>COM初期化後に呼び出す。明示割り当て画像とWAVは取得元とコピー後を復号検証し、空間音声はモノラルだけを受け付ける。assetRootが絶対指定なら素材は絶対参照にする。起動設定の返却とEditorへの適用は任意。GPUリソースは作成しない。リンクやパッケージ外の参照は拒否する</remarks>
	static bool Import(const SceneImportOptions &options, Scene &imported,
	                   std::filesystem::path &createdFolder, std::string &error,
	                   GameConfig *configuration = nullptr);
};
