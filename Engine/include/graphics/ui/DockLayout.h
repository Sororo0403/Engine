#pragma once

/// <summary>
/// ドッキング領域の識別情報と初期配置
/// </summary>
struct DockLayout {
	const char *name = nullptr;

	const char *leftWindow = nullptr;
	const char *centerWindow = nullptr;
	const char *rightWindow = nullptr;

	// 全体の幅に対する割合。合計は1未満にする
	float leftRatio = 0.20f;
	float rightRatio = 0.25f;

	// nullptrの場合は下部領域を作成しない
	const char *bottomWindow = nullptr;

	// 全体の高さに対する下部領域の割合
	float bottomRatio = 0.25f;
};
