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
	/// ギズモで編集する変形成分
	/// </summary>
	enum class Operation {
		TRANSLATE,
		ROTATE,
		SCALE,
	};

	/// <summary>
	/// 単一物体の移動・回転に使用する座標系
	/// </summary>
	enum class Space {
		LOCAL,
		WORLD,
	};

	/// <summary>
	/// 座標系の設定を変更する。ドラッグ中は切り替えない
	/// </summary>
	/// <param name="space">物体のローカル座標、またはワールド座標</param>
	/// <returns>true:設定成功 false:ドラッグ中、または値が無効</returns>
	bool SetSpace(Space space);

	/// <summary>
	/// 単一物体の移動・回転用に保持している座標系の設定を取得する
	/// </summary>
	/// <returns>座標系の設定。複数選択と拡縮の座標系には影響しない</returns>
	Space GetSpace() const;

	/// <summary>
	/// ギズモの操作種別を設定する。ドラッグ中は切り替えない
	/// </summary>
	/// <param name="operation">移動・回転・拡大縮小の種別</param>
	/// <returns>true:設定成功 false:ドラッグ中、または無効な種別</returns>
	bool SetOperation(Operation operation);

	/// <summary>
	/// ギズモのスナップ間隔
	/// </summary>
	struct SnapSettings {
		DirectX::XMFLOAT3 translation{1.0f, 1.0f, 1.0f};
		float rotationDegrees = 15.0f;
		float scaleRatio = 0.1f;
	};

	/// <summary>
	/// スナップの有効・無効を設定する。ドラッグ中は切り替えない
	/// </summary>
	/// <param name="enabled">スナップを使用するか</param>
	/// <returns>true:設定成功 false:ドラッグ中</returns>
	bool SetSnapping(bool enabled);

	/// <summary>
	/// 有限で正のスナップ間隔を設定する。回転角は360度以下とする
	/// </summary>
	/// <param name="settings">移動・回転・拡縮の間隔</param>
	/// <returns>true:設定成功 false:ドラッグ中、または無効な間隔</returns>
	bool SetSnapSettings(const SnapSettings &settings);

	/// <summary>
	/// 現在の操作種別を取得する
	/// </summary>
	/// <returns>移動・回転・拡縮の種別</returns>
	Operation GetOperation() const;

	/// <summary>
	/// スナップの有効状態を取得する
	/// </summary>
	/// <returns>true:有効 false:無効</returns>
	bool IsSnapping() const;

	/// <summary>
	/// 現在のスナップ間隔を取得する
	/// </summary>
	/// <returns>移動・回転・拡縮の間隔</returns>
	SnapSettings GetSnapSettings() const;

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
	Operation operation_ = Operation::TRANSLATE;
	Space space_ = Space::LOCAL;

	SceneObjectId objectId_ = 0;
	SelectionMode selectionMode_ = SelectionMode::SINGLE;
	bool using_ = false;
	bool snapping_ = false;
	SnapSettings snapSettings_;
};
