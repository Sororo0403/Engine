#pragma once

/// <summary>
/// 左・中央・右の3領域へ配置するウィンドウの情報
/// </summary>
struct DockLayout {
	// ドッキング領域を識別する名前
	const char *name = nullptr;

	// BeginWindowに渡すタイトルと同じ名前を指定する
	const char *leftWindow = nullptr;
	const char *centerWindow = nullptr;
	const char *rightWindow = nullptr;

	// 全体の幅に対する割合。合計は1未満にする
	float leftRatio = 0.20f;
	float rightRatio = 0.25f;
};
