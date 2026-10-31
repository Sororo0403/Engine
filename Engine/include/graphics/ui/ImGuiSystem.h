#pragma once

#include "graphics/ui/ViewportInput.h"

#include <DirectXMath.h>
#include <cstdint>
#include <memory>
#include <string_view>

struct DockLayout;

class D3D12Graphics;
class RenderTexture;
class WinApp;
class Window;

/// <summary>
/// ImGuiの初期化・UI操作・描画を管理する
/// </summary>
class ImGuiSystem {
  public:
	ImGuiSystem();
	~ImGuiSystem();

	ImGuiSystem(const ImGuiSystem &) = delete;
	ImGuiSystem &operator=(const ImGuiSystem &) = delete;

	/// <summary>
	/// UIと描画用リソースを初期化する
	/// </summary>
	/// <param name="app">UIを表示するアプリケーション</param>
	/// <param name="graphics">初期化済みの描画基盤。このクラスより長く保持すること</param>
	/// <returns>true:成功 false:引数が無効、初期化済み、または作成失敗</returns>
	bool Initialize(const WinApp &app, D3D12Graphics &graphics);

	/// <summary>
	/// UIのフレームを開始する。
	/// 前フレームのGPU処理完了後に呼び出す
	/// </summary>
	/// <returns>true:成功 false:未初期化、またはフレーム開始済み</returns>
	bool BeginFrame();

	/// <summary>
	/// UIの描画命令を記録する。3D描画後、EndFrameより前に呼び出す
	/// </summary>
	/// <returns>true:成功 false:未初期化、フレームの状態が無効、または描画処理失敗</returns>
	bool Render();

	/// <summary>
	/// ウィンドウのドッキングを有効にする。
	/// UIの初期化後、フレーム開始前に呼び出す
	/// </summary>
	/// <returns>true:成功 false:未初期化、またはフレーム開始済み</returns>
	bool EnableDocking() const;

	/// <summary>
	/// メインウィンドウ全体にドッキング領域を作成する。
	/// 各パネルのBeginWindowより前に、毎フレーム呼び出す
	/// </summary>
	/// <param name="layout">領域の識別情報と初期配置</param>
	/// <param name="resetLayout">保存済みの配置を破棄し、初期配置へ戻すか</param>
	void DrawDockSpace(const DockLayout &layout,
	                   bool resetLayout = false) const;

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
	/// ボタンを表示する
	/// </summary>
	/// <param name="label">ボタンのラベル</param>
	/// <returns>true:押された false:押されていない</returns>
	bool Button(const char *label) const;

	/// <summary>
	/// 選択可能な項目を表示する
	/// </summary>
	/// <param name="label">項目のラベル</param>
	/// <param name="selected">現在選択されているか</param>
	/// <returns>true:押された false:押されていない</returns>
	bool Selectable(const char *label, bool selected) const;

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
	/// 次に開始するウィンドウの初期サイズを設定する
	/// </summary>
	/// <param name="width">幅</param>
	/// <param name="height">高さ</param>
	void SetNextWindowSize(float width, float height) const;

	/// <summary>
	/// 現在のウィンドウで使用できる内容領域のサイズを取得する
	/// </summary>
	/// <returns>横方向・縦方向の使用可能サイズ</returns>
	DirectX::XMFLOAT2 GetContentSize() const;

	/// <summary>
	/// 最後に表示した画像に対する操作の可否を取得する
	/// </summary>
	/// <returns>画像と、その画像を表示しているウィンドウの状態</returns>
	/// <remarks>Imageの直後、EndWindowより前に呼び出す</remarks>
	ViewportInput GetViewportInput() const;

	/// <summary>
	/// 描画先テクスチャを画像として表示する
	/// </summary>
	/// <param name="texture">表示するテクスチャ</param>
	/// <param name="width">表示する幅</param>
	/// <param name="height">表示する高さ</param>
	/// <remarks>
	/// 呼び出しからフレームのGPU処理完了まで、
	/// テクスチャのリサイズ・破棄を行わないこと
	/// </remarks>
	void Image(const RenderTexture &texture, float width, float height) const;

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
