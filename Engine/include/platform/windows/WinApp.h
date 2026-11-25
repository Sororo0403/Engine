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
	bool Initialize(HINSTANCE hInstance, const std::wstring &title, int width,
	                int height, bool fullscreen = false);

	/// <summary>
	/// Windowsメッセージを処理する
	/// </summary>
	/// <returns>true:継続 false:終了要求あり</returns>
	bool ProcessMessages();

	/// <summary>
	/// ウィンドウを閉じる要求を取得し、要求をクリアする
	/// </summary>
	/// <returns>true:要求あり false:要求なし</returns>
	bool ConsumeCloseRequest();

	/// <summary>
	/// メインウィンドウのタイトルを変更する
	/// </summary>
	/// <param name="title">変更後のタイトル</param>
	/// <returns>true:成功 false:変更失敗</returns>
	bool SetTitle(const std::wstring &title);

	/// <summary>
	/// アプリケーションの終了処理
	/// </summary>
	void Finalize();

	/// <summary>
	/// ウィンドウハンドルを取得
	/// </summary>
	/// <returns>ウィンドウハンドル</returns>
	HWND GetHwnd() const;

	/// <summary>
	/// クライアント領域の幅を取得する
	/// </summary>
	/// <returns>幅(ピクセル)</returns>
	int GetWidth() const;

	/// <summary>
	/// クライアント領域の高さを取得する
	/// </summary>
	/// <returns>高さ(ピクセル)</returns>
	int GetHeight() const;

  private:
	Window window_;
};
