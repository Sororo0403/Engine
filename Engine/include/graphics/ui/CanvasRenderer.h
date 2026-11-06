#pragma once
#include <memory>
#include <string>
class Scene;
class D3D12Graphics;

/// <summary>
/// シーンのCanvasを、3Dの後へアルファ合成する。ImGuiを使用しない
/// </summary>
class CanvasRenderer {
  public:
	enum class Failure { FAIL, SKIP_ELEMENT };
	/// <summary>
	/// GPU参照を保持する。画像・文字・パイプラインは初回利用まで作成しない
	/// </summary>
	/// <param name="graphics">この描画クラスより長く保持する描画基盤</param>
	/// <param name="failure">FAIL:失敗を返す SKIP_ELEMENT:編集用に未設定・読み込み不能の要素を省略する</param>
	explicit CanvasRenderer(D3D12Graphics &graphics,
	                        Failure failure = Failure::FAIL);
	/// <summary>
	/// キャッシュと描画リソースを解放する。GPU完了後に破棄すること
	/// </summary>
	~CanvasRenderer();
	CanvasRenderer(const CanvasRenderer &) = delete;
	CanvasRenderer &operator=(const CanvasRenderer &) = delete;
	/// <summary>
	/// UIを配置し、変更された画像と文字だけをGPUへ準備する
	/// </summary>
	/// <param name="scene">描画対象のシーン</param>
	/// <param name="width">描画先の幅</param>
	/// <param name="height">描画先の高さ</param>
	/// <param name="error">失敗理由。成功時は空</param>
	/// <returns>true:準備成功 false:設定・画像・文字・GPU準備が無効。失敗後は描画しない</returns>
	/// <remarks>COM初期化済み、GPU完了後、BeginFrame前に呼ぶ。画像1辺4096・表示素材合計128 MiBを上限とする</remarks>
	bool Update(const Scene &scene, int width, int height, std::string &error);
	/// <summary>
	/// 最後のUpdateで準備したUIを、設定済みの描画先とビューポートへ重ねる
	/// </summary>
	/// <remarks>同じ描画基盤のBeginFrameからEndFrameの間に呼ぶ。深度を参照・変更しない</remarks>
	void Draw() const;

  private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};
