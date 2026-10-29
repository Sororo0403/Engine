#pragma once

#include <Windows.h>
#include <string>

class Window {
  private:
	inline static const std::wstring kClassName = L"GameWindow";

  public:
	/// <summary>
	/// デストラクタ
	/// </summary>
	~Window();

	/// <summary>
	/// ウィンドウを生成する
	/// </summary>
	/// <param name="hInstance">Windowsアプリケーションのインスタンスを表すハンドル</param>
	/// <param name="title">ウィンドウのタイトル</param>
	/// <param name="width">ウィンドウの幅</param>
	/// <param name="height">ウィンドウの高さ</param>
	/// <returns>true:生成成功 false:生成失敗</returns>
	bool Create(HINSTANCE hInstance, const std::wstring &title, int width,
	            int height);

	/// <summary>
	/// ウィンドウを廃棄する
	/// </summary>
	void Destroy();

	/// <summary>
	/// ウィンドウハンドルを取得
	/// </summary>
	/// <returns>ウィンドウハンドル</returns>
	HWND GetHwnd() const;

	/// <summary>
	/// ウィンドウの幅を取得
	/// </summary>
	/// <returns>ウィンドウの幅</returns>
	int GetWidth() const;

	/// <summary>
	/// ウィンドウの高さを取得
	/// </summary>
	/// <returns>ウィンドウの高さ</returns>
	int GetHeight() const;

  public:
	// コピーを禁止する
	Window(const Window &) = delete;
	Window &operator=(const Window &) = delete;
	Window() = default;

  private:
	/// <summary>
	/// ウィンドプロシージャ
	/// </summary>
	/// <param name="hwnd">ウィンドウハンドル</param>
	/// <param name="msg">メッセージ番号</param>
	/// <param name="wParam">追加情報1</param>
	/// <param name="lParam">追加情報2</param>
	/// <returns>メッセージの処理結果。未処理のメッセージはDefWindowProcWの戻り値</returns>
	static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam,
	                                LPARAM lParam);

  private:
	HWND hwnd_ = nullptr;
	HINSTANCE hInstance_ = nullptr;
	bool isClassRegistered_ = false;
	int width_ = 0;
	int height_ = 0;
};
