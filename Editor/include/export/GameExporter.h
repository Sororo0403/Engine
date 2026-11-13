#pragma once

#include "gameplay/GameConfig.h"
#include "scene/SceneObjectId.h"
#include <filesystem>
#include <string>
#include <vector>

class Scene;

/// <summary>
/// 出力先と、Release実行ファイル・制作リソースの取得元
/// </summary>
struct GameExportOptions {
	std::filesystem::path destination;
	std::filesystem::path runtimeDirectory;
	std::filesystem::path resourceDirectory;
	GameConfig config{};

	// 外部素材の許可された取得元。相対指定は実行ファイル基準。素材の参照には絶対パスを使う
	std::vector<std::filesystem::path> assetDirectories;

	// 起動シーンへの遷移を同じ出力シーンへ割り当てるための元の絶対パス
	std::filesystem::path startupScenePath;
};

/// <summary>
/// 配布前検査から直接修正できる設定
/// </summary>
enum class GameExportField {
	NONE,
	OUTPUT_FOLDER,
	TITLE,
	WIDTH,
	HEIGHT,
	STARTUP_SCENE,
};

/// <summary>
/// 配布前検査の不備と、特定できた場合の参照元・素材・Lua編集先
/// </summary>
struct GameExportIssue {
	std::string message;
	std::filesystem::path scenePath;
	SceneObjectId objectId = 0;
	std::filesystem::path scriptPath;
	std::filesystem::path assetPath;
	// Explorerで確認するファイルまたはフォルダ
	std::filesystem::path locationPath;
	GameExportField field = GameExportField::NONE;
	// 遷移先の診断から、遷移を設定した物体へ戻るための参照
	std::filesystem::path transitionScenePath;
	SceneObjectId transitionObjectId = 0;
	// Luaを移動した場合も診断の行番号を読めるよう、出力時のパスを保持する。
	std::filesystem::path scriptDiagnosticPath;
	std::string inputAction;
};

namespace GameExporter {
/// <summary>
/// 出力予定のシーン・依存ファイル・Runtimeを検査する。出力先には書き込まない
/// </summary>
/// <param name="error">検査で見つかった不備。複数の場合は空行区切り</param>
/// <param name="issues">指定時は表示する不備とシーン・物体への移動先を返す</param>
/// <remarks>COM初期化後に呼び出す。コピー後の検証は実際の出力時に行う</remarks>
bool Check(const Scene &scene, const GameExportOptions &options,
           std::string &error, std::vector<GameExportIssue> *issues = nullptr);

/// <summary>
/// シーンと依存ファイルを新規フォルダへ直接出力する。既存フォルダには書き込まない
/// </summary>
/// <param name="scene">変更しない編集シーン</param>
/// <param name="options">出力先・取得元・ゲーム設定</param>
/// <param name="error">不足ファイルや設定の失敗理由。後片付けできない場合は残った場所も含む。成功時は空</param>
/// <param name="issues">指定時は失敗理由と、特定できた参照元や素材を返す</param>
/// <returns>true:出力成功 false:失敗。今回作成したものだけを片付け、編集シーンと既存出力は保持する</returns>
/// <remarks>COM初期化後に呼び出す。RuntimeはRelease x64で、ゲーム設定とシーンの保存形式がEditorと一致するものを使用する。明示割り当て画像とWAVは取得元とコピー後の両方を復号検証する。空間音源にはモノラルWAVが必要。外部素材はフォルダごとの名前空間へ構成を保ってコピーする。モデルの依存先は同じ取得元内に収まる必要がある</remarks>
bool Export(const Scene &scene, const GameExportOptions &options,
            std::string &error, std::vector<GameExportIssue> *issues = nullptr);
} // namespace GameExporter
