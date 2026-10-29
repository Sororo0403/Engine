#pragma once

#include "platform/windows/Window.h"

class WinApp {
public:
	/// <summary>
	/// アプリケーションの初期化処理
	/// </summary>
	/// <param name="hInstance">Windowsアプリケーションのインスタンスを表すハンドル</param>
	/// <param name="title">ウィンドウのタイトル</param>
	/// <param name="width">ウィンドウの幅</param>
	/// <param name="height">ウィンドウの高さ</param>
	/// <returns>true:初期化成功 false:初期化失敗</returns>
	bool Initialize(HINSTANCE hInstance, const std::wstring &title, int width, int height);

	/// <summary>
	/// Windowsメッセージを処理する
	/// </summary>
	/// <returns>true:継続 false:終了要求あり</returns>
	bool ProcessMessages();

	/// <summary>
	/// アプリケーションの終了処理
	/// </summary>
	void Finalize();

private:
	Window window_;
};
