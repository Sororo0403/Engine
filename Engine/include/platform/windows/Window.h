#pragma once

#include <Windows.h>

class Window {
public:
	/// <summary>
	/// ウィンドウを生成する
	/// </summary>
	/// <param name="hInstance">Windowsアプリケーションのインスタンスを表すハンドル</param>
	/// <returns>true:生成成功 false:生成失敗</returns>
	bool Create(HINSTANCE hInstance);
private:
	HWND hwnd_ = nullptr;
};

