#pragma once

#include "math/Transform.h"
#include "scene/SceneObjectId.h"

/// <summary>
/// Sceneビューに表示する変形ギズモの操作を管理する
/// </summary>
class TransformGizmo {
  public:
	/// <summary>
	/// ギズモの選択対象。複数選択ではワールド座標での移動・回転・均等拡縮を使用する
	/// </summary>
	enum class SelectionMode {
		SINGLE,
		MULTIPLE,
	};

	/// <summary>
	/// ギズモ操作の結果
	/// </summary>
	struct Result {
		bool changed = false;

		// ギズモへの操作として扱い、モデル選択へ渡さないか
		bool blocksSelection = false;
	};

	/// <summary>
	/// ギズモのフレームを開始する。
	/// ImGuiのフレーム開始後に一度呼び出す
	/// </summary>
	void BeginFrame();

	/// <summary>
	/// 移動・回転・拡大縮小の切り替えUIを表示する
	/// </summary>
	/// <param name="selectionMode">単一選択または複数選択</param>
	void DrawToolbar(SelectionMode selectionMode);

	/// <summary>
	/// 現在のウィンドウへ選択物体の変形ギズモを表示する
	/// </summary>
	/// <param name="view">描画に使用するビュー行列</param>
	/// <param name="projection">描画に使用する投影行列</param>
	/// <param name="imageMinimum">Scene画像の左上のUI座標</param>
	/// <param name="imageSize">Scene画像の表示サイズ</param>
	/// <param name="objectId">選択物体のID。0の場合は選択なし</param>
	/// <param name="transform">編集する変形情報。選択なしの場合はnullptr</param>
	/// <param name="enabled">操作を受け付けるか</param>
	/// <param name="selectionMode">単一選択または複数選択</param>
	/// <returns>変形の変更と、モデル選択を止めるかの情報</returns>
	Result Draw(const DirectX::XMFLOAT4X4 &view,
	            const DirectX::XMFLOAT4X4 &projection,
	            const DirectX::XMFLOAT2 &imageMinimum,
	            const DirectX::XMFLOAT2 &imageSize, SceneObjectId objectId,
	            Transform *transform, bool enabled,
	            SelectionMode selectionMode);

	/// <summary>
	/// ギズモをドラッグしているか取得する
	/// </summary>
	/// <returns>true:ドラッグ中 false:それ以外</returns>
	bool IsUsing() const;

	/// <summary>
	/// 現在のギズモ操作を中止する
	/// </summary>
	void Cancel();

  private:
	/// <summary>
	/// ギズモで編集する変形成分
	/// </summary>
	enum class Operation {
		TRANSLATE,
		ROTATE,
		SCALE,
	};

	Operation operation_ = Operation::TRANSLATE;

	SceneObjectId objectId_ = 0;
	SelectionMode selectionMode_ = SelectionMode::SINGLE;
	bool using_ = false;
};
