#pragma once

#include "gameplay/GameConfig.h"
#include <filesystem>
#include <string>

class Scene;

/// <summary>
/// 出力先と、Release実行ファイル・制作リソースの取得元
/// </summary>
struct GameExportOptions {
	std::filesystem::path destination;
	std::filesystem::path runtimeDirectory;
	std::filesystem::path resourceDirectory;
	GameConfig config{};
};

namespace GameExporter {
/// <summary>
/// シーンと必要な依存ファイルを新しいフォルダへ出力する。既存フォルダには書き込まない
/// </summary>
/// <param name="scene">変更しない編集シーン</param>
/// <param name="options">出力先・取得元・ゲーム設定</param>
/// <param name="error">不足ファイルや設定の失敗理由。成功時は空</param>
/// <returns>true:出力成功 false:失敗。編集シーンと既存出力は保持する</returns>
bool Export(const Scene &scene, const GameExportOptions &options,
            std::string &error);
} // namespace GameExporter
