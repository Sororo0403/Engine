#pragma once

#include "gameplay/GameConfig.h"
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
};

namespace GameExporter {
/// <summary>
/// シーンと依存ファイルを新規フォルダへ直接出力する。既存フォルダには書き込まない
/// </summary>
/// <param name="scene">変更しない編集シーン</param>
/// <param name="options">出力先・取得元・ゲーム設定</param>
/// <param name="error">不足ファイルや設定の失敗理由。後片付けできない場合は残った場所も含む。成功時は空</param>
/// <returns>true:出力成功 false:失敗。今回作成したものだけを片付け、編集シーンと既存出力は保持する</returns>
/// <remarks>COM初期化後に呼び出す。RuntimeはRelease x64で、ゲーム設定とシーンの保存形式がEditorと一致するものを使用する。明示割り当て画像は取得元とコピー後の両方を取り込みと同じ条件で復号検証する。外部素材はフォルダごとの名前空間へ構成を保ってコピーする。モデルの依存先は同じ取得元内に収まる必要がある</remarks>
bool Export(const Scene &scene, const GameExportOptions &options,
            std::string &error);
} // namespace GameExporter
