#pragma once

#include <DirectXMath.h>
#include <cstdint>
#include <memory>
#include <string_view>

class D3D12Graphics;
class WinApp;
class Window;

/// <summary>
/// デバッグUIの初期化・操作・描画を管理する
/// </summary>
class DebugUI {
  public:
	DebugUI();
	~DebugUI();

	DebugUI(const DebugUI &) = delete;
	DebugUI &operator=(const DebugUI &) = delete;

	/// <summary>
	/// デバッグUIと描画用リソースを初期化する
	/// </summary>
	/// <param name="app">UIを表示するアプリケーション</param>
	/// <param name="graphics">初期化済みの描画基盤。このクラスより長く保持すること</param>
	/// <param name="framesInFlight">描画バッファ数。GPU処理中のフレーム数以上にする</param>
	/// <returns>true:成功 false:引数が無効、初期化済み、または作成失敗</returns>
	bool Initialize(const WinApp &app, D3D12Graphics &graphics,
	                int framesInFlight = 2);

	/// <summary>
	/// UIのフレームを開始する。この後でウィジェットを作成する
	/// </summary>
	/// <returns>true:成功 false:未初期化、またはフレーム開始済み</returns>
	bool BeginFrame();

	/// <summary>
	/// UIの描画命令を記録する。3D描画後、EndFrameより前に呼び出す
	/// </summary>
	/// <returns>true:成功 false:未初期化、フレームの状態が無効、または描画処理失敗</returns>
	bool Render();

	/// <summary>
	/// UIウィンドウを開始する。戻り値にかかわらずEndWindowを呼び出す
	/// </summary>
	/// <param name="title">ウィンドウのタイトル</param>
	/// <returns>true:内容を構築できる false:折りたたみ中などで内容を構築しない</returns>
	bool BeginWindow(const char *title) const;

	/// <summary>
	/// BeginWindowで開始したUIウィンドウを終了する
	/// </summary>
	void EndWindow() const;

	/// <summary>
	/// チェックボックスを表示する
	/// </summary>
	/// <param name="label">項目のラベル</param>
	/// <param name="value">編集する値</param>
	/// <returns>true:値が変更された false:変更なし</returns>
	bool Checkbox(const char *label, bool &value) const;

	/// <summary>
	/// 折りたたみ可能な見出しを表示する
	/// </summary>
	/// <param name="label">見出しのラベル</param>
	/// <param name="defaultOpen">初期状態を展開済みにするか</param>
	/// <returns>true:展開中 false:折りたたみ中</returns>
	bool CollapsingHeader(const char *label, bool defaultOpen = true) const;

	/// <summary>
	/// 指定範囲内で値を調整するスライダーを表示する
	/// </summary>
	/// <param name="label">項目のラベル</param>
	/// <param name="value">編集する値</param>
	/// <param name="minimum">最小値</param>
	/// <param name="maximum">最大値</param>
	/// <param name="format">値の表示書式</param>
	/// <returns>true:値が変更された false:変更なし</returns>
	bool SliderFloat(const char *label, float &value, float minimum,
	                 float maximum, const char *format = "%.2f") const;

	/// <summary>
	/// 指定範囲内で3成分の値をドラッグ調整する
	/// </summary>
	/// <param name="label">項目のラベル</param>
	/// <param name="value">編集する値</param>
	/// <param name="speed">ドラッグによる変化量</param>
	/// <param name="minimum">各成分の最小値</param>
	/// <param name="maximum">各成分の最大値</param>
	/// <param name="format">値の表示書式</param>
	/// <returns>true:値が変更された false:変更なし</returns>
	bool DragFloat3(const char *label, DirectX::XMFLOAT3 &value, float speed,
	                float minimum, float maximum,
	                const char *format = "%.2f") const;

	/// <summary>
	/// RGBの色編集UIを表示する
	/// </summary>
	/// <param name="label">項目のラベル</param>
	/// <param name="color">編集するRGB</param>
	/// <returns>true:値が変更された false:変更なし</returns>
	bool ColorEdit3(const char *label, DirectX::XMFLOAT3 &color) const;

	/// <summary>
	/// RGBAの色編集UIを表示する
	/// </summary>
	/// <param name="label">項目のラベル</param>
	/// <param name="color">編集するRGBA</param>
	/// <returns>true:値が変更された false:変更なし</returns>
	bool ColorEdit4(const char *label, DirectX::XMFLOAT4 &color) const;

	/// <summary>
	/// 後続の項目を識別するIDを追加する。使用後はPopIDを呼び出す
	/// </summary>
	/// <param name="id">項目を区別する番号</param>
	void PushID(int id) const;

	/// <summary>
	/// PushIDで追加したIDを取り除く
	/// </summary>
	void PopID() const;

	/// <summary>
	/// 区切り線を表示する
	/// </summary>
	void Separator() const;

	/// <summary>
	/// 書式として解釈せず、そのまま文字列を表示する
	/// </summary>
	/// <param name="text">表示する文字列</param>
	void Text(std::string_view text) const;

	/// <summary>
	/// UIがキーボード入力を使用しているか取得する
	/// </summary>
	/// <returns>true:UI操作を優先する false:それ以外</returns>
	bool WantsKeyboard() const;

	/// <summary>
	/// UIがマウス入力を使用しているか取得する
	/// </summary>
	/// <returns>true:UI操作を優先する false:それ以外</returns>
	bool WantsMouse() const;

	/// <summary>
	/// UIを終了する。GPUの利用完了後、ウィンドウを破棄する前に呼び出す
	/// </summary>
	void Finalize();

  private:
	friend class Window;

	/// <summary>
	/// ウィンドウから受け取ったメッセージをUIへ渡す
	/// </summary>
	/// <param name="nativeWindow">メッセージを受け取ったウィンドウ</param>
	/// <param name="message">メッセージ番号</param>
	/// <param name="wParam">追加情報1</param>
	/// <param name="lParam">追加情報2</param>
	/// <returns>true:処理済み false:通常のウィンドウ処理を続ける</returns>
	static bool HandleMessage(void *nativeWindow, std::uint32_t message,
	                          std::uintptr_t wParam, std::intptr_t lParam);

	struct Impl;
	std::unique_ptr<Impl> impl_;
};
