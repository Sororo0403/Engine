#pragma once

#include "gizmo/TransformGizmo.h"
#include "graphics/ui/ViewportInput.h"

#include <filesystem>
#include <memory>

class ImGuiSystem;
class RenderSystem;
class RenderTexture;

/// <summary>
/// シーン画像の表示領域と描画先テクスチャを管理する
/// </summary>
class SceneViewPanel {
  public:
	/// <summary>
	/// Sceneビューを展開し、次の描画時にドックのタブを選んでフォーカスを移す
	/// </summary>
	void Show();

	/// <summary>
	/// Scene画像で確定した選択操作
	/// </summary>
	struct SelectionRequest {
		enum class Action { NONE, CLICK, BOX };
		Action action = Action::NONE;
		DirectX::XMFLOAT2 minimum{};
		DirectX::XMFLOAT2 maximum{};
		bool additive = false;
	};

	/// <summary>
	/// Scene画像へ確定したアセットの配置要求
	/// </summary>
	struct AssetDropRequest {
		std::filesystem::path path;
		DirectX::XMFLOAT2 imagePosition{};
	};

	/// <summary>
	/// 描画機能への参照を保持する
	/// </summary>
	/// <param name="render">このパネルより長く保持する描画機能</param>
	explicit SceneViewPanel(RenderSystem &render);

	~SceneViewPanel();

	SceneViewPanel(const SceneViewPanel &) = delete;
	SceneViewPanel &operator=(const SceneViewPanel &) = delete;

	/// <summary>
	/// パネルのサイズを反映し、シーン画像の表示UIを構築する。
	/// GPUの利用完了後、描画命令の記録前に呼び出す
	/// </summary>
	/// <param name="ui">フレーム開始済みのUI</param>
	/// <param name="gizmo">操作切り替えUIを表示するギズモ</param>
	/// <param name="selectionMode">単一選択または複数選択</param>
	/// <param name="editable">編集操作を許可するか。falseではツールバー・ドロップ・ギズモを止める</param>
	/// <param name="allowReadOnlySelection">編集不可でも、観察用の選択入力を許可するか。カメラ入力はいずれの場合も保持する</param>
	/// <returns>true:成功または非表示 false:描画先の作成失敗</returns>
	bool DrawUI(const ImGuiSystem &ui, TransformGizmo &gizmo,
	            TransformGizmo::SelectionMode selectionMode,
	            bool editable = true, bool allowReadOnlySelection = false);

	/// <summary>
	/// このフレームに表示したScene画像へ変形ギズモを重ねる
	/// </summary>
	/// <param name="ui">フレーム開始済みのUI</param>
	/// <param name="gizmo">表示するギズモ</param>
	/// <param name="view">描画に使用するビュー行列</param>
	/// <param name="projection">描画に使用する投影行列</param>
	/// <param name="objectId">選択物体のID</param>
	/// <param name="transform">編集する変形情報。選択なしの場合はnullptr</param>
	/// <param name="enabled">操作を受け付けるか</param>
	/// <param name="selectionMode">単一選択または複数選択</param>
	/// <returns>ギズモの操作結果</returns>
	TransformGizmo::Result
	DrawGizmo(const ImGuiSystem &ui, TransformGizmo &gizmo,
	          const DirectX::XMFLOAT4X4 &view,
	          const DirectX::XMFLOAT4X4 &projection, SceneObjectId objectId,
	          Transform *transform, bool enabled,
	          TransformGizmo::SelectionMode selectionMode) const;

	/// <summary>
	/// このフレームで描画するテクスチャを取得する
	/// </summary>
	/// <returns>描画先。パネルが非表示の場合はnullptr</returns>
	RenderTexture *GetRenderTexture() const;

	/// <summary>
	/// Scene画像に対する操作の可否を取得する
	/// </summary>
	/// <returns>DrawUIで取得した状態。非表示の場合は空の状態</returns>
	const ViewportInput &GetInput() const;

	/// <summary>
	/// このフレームのモデルのドロップを取得する
	/// </summary>
	/// <returns>配置要求。操作がない場合はパスが空</returns>
	const AssetDropRequest &GetModelDrop() const;

	/// <summary>
	/// このフレームの保存済み物体の組のドロップを取得する
	/// </summary>
	/// <returns>シーンの配置要求。操作がない場合はパスが空</returns>
	const AssetDropRequest &GetSceneDrop() const;

	/// <summary>
	/// ギズモ判定後に選択入力を更新し、ドラッグ中の矩形を表示する
	/// </summary>
	/// <param name="ui">フレーム開始済みのUI</param>
	/// <param name="enabled">選択操作を受け付けるか</param>
	/// <param name="gizmoBlocksSelection">押下をギズモの操作として扱うか</param>
	/// <returns>ボタンを離したときに確定したクリックまたは範囲選択</returns>
	SelectionRequest UpdateSelection(const ImGuiSystem &ui, bool enabled,
	                                 bool gizmoBlocksSelection);

	/// <summary>
	/// 選択操作の押下またはドラッグを継続しているか確認する
	/// </summary>
	/// <returns>true:選択操作中 false:操作なし</returns>
	bool IsSelecting() const;

	/// <summary>
	/// 確定していない選択操作を中止する
	/// </summary>
	void CancelSelection();

	/// <summary>
	/// 描画先を解放する。GPUの利用完了後に呼び出す
	/// </summary>
	void Finalize();

  private:
	bool focusRequested_ = false;

	// 参照先はSceneViewPanelより長く保持する
	RenderSystem &render_;

	std::unique_ptr<RenderTexture> texture_;
	ViewportInput input_{};
	AssetDropRequest modelDrop_;
	AssetDropRequest sceneDrop_;

	bool visible_ = false;
	bool editable_ = true;
	bool selectionEnabled_ = true;

	DirectX::XMFLOAT2 imageMinimum_{};
	DirectX::XMFLOAT2 imageSize_{};

	bool selecting_ = false;
	bool selectionDragging_ = false;
	bool selectionAdditive_ = false;
	DirectX::XMFLOAT2 selectionStart_{};
	DirectX::XMFLOAT2 selectionPressPosition_{};
	DirectX::XMFLOAT2 selectionImageMinimum_{};
	DirectX::XMFLOAT2 selectionImageSize_{};
};
