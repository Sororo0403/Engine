#pragma once

#include "graphics/camera/CameraPose.h"
#include "graphics/lighting/LightingSettings.h"
#include "history/ActiveCommand.h"
#include "history/AlignmentAxis.h"
#include "history/ColliderCommand.h"
#include "history/CommandGroup.h"
#include "history/ComponentCommand.h"
#include "history/ModelPathCommand.h"
#include "history/ObjectCommand.h"
#include "history/ObjectRename.h"
#include "history/ParentCommand.h"
#include "history/RenameCommand.h"
#include "history/ScenePatchCommand.h"
#include "history/TextureSlotsCommand.h"
#include "history/TransformCommand.h"
#include "history/VisibilityCommand.h"
#include "scene/SceneObject.h"
#include "scene/SceneWorldTransform.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

class Scene;

/// <summary>
/// シーンの編集履歴と保存地点を管理する
/// </summary>
class EditorHistory {
  public:
	/// <summary>
	/// この履歴が編集するシーンを読み取る
	/// </summary>
	/// <returns>履歴と同じ寿命を持つシーン</returns>
	const Scene &GetScene() const {
		return scene_;
	}

	/// <summary>
	/// 履歴を適用するシーンへの参照を保持する
	/// </summary>
	/// <param name="scene">この履歴より長く保持するシーン</param>
	explicit EditorHistory(Scene &scene) noexcept;

	EditorHistory(const EditorHistory &) = delete;
	EditorHistory &operator=(const EditorHistory &) = delete;

	/// <summary>
	/// 変更前の値を保持し、連続した変形編集を開始する
	/// </summary>
	/// <param name="objectId">編集する物体のID</param>
	/// <param name="before">編集を開始する前の変形情報</param>
	/// <returns>true:開始成功 false:物体がない、または値が無効</returns>
	bool BeginTransform(SceneObjectId objectId, const Transform &before);

	/// <summary>
	/// 編集中の名前・変形情報・コンポーネント設定を1操作として記録する。
	/// 変更がなければ履歴には追加しない
	/// </summary>
	void CommitEdit();

	/// <summary>
	/// 連続した編集の途中か確認する
	/// </summary>
	/// <returns>true:編集中 false:編集なし</returns>
	bool IsEditing() const;

	/// <summary>
	/// 元に戻せる操作があるか確認する
	/// </summary>
	/// <returns>true:実行可能 false:編集中、または履歴なし</returns>
	bool CanUndo() const;

	/// <summary>
	/// やり直せる操作があるか確認する
	/// </summary>
	/// <returns>true:実行可能 false:編集中、または履歴なし</returns>
	bool CanRedo() const;

	/// <summary>
	/// 次に元に戻す操作の表示名を取得する
	/// </summary>
	/// <returns>操作の表示名。Undoできない場合は空</returns>
	std::string_view GetUndoDescription() const;

	/// <summary>
	/// 次にやり直す操作の表示名を取得する
	/// </summary>
	/// <returns>操作の表示名。Redoできない場合は空</returns>
	std::string_view GetRedoDescription() const;

	/// <summary>
	/// 履歴内の状態の表示情報。編集中の値は含まない
	/// </summary>
	struct State {
		std::string_view description;
		bool current = false;
		bool saved = false;
	};

	/// <summary>
	/// 履歴の開始地点を含む状態数を取得する
	/// </summary>
	/// <returns>記録済みの操作数＋1</returns>
	std::size_t GetStateCount() const;

	/// <summary>
	/// 現在の確定済み編集状態を識別する。履歴をリセットするまで有効
	/// </summary>
	std::uint64_t GetCurrentStateId() const noexcept {
		return currentState_;
	}

	/// <summary>
	/// 履歴の指定地点の表示情報を取得する
	/// </summary>
	/// <param name="index">開始地点を0とした状態番号</param>
	/// <returns>状態の表示情報。範囲外はnullopt。表示名は静的な寿命を持つ</returns>
	std::optional<State> GetState(std::size_t index) const;

	/// <summary>
	/// 指定された履歴の状態へまとめて移動する。途中で失敗した場合はシーンを変更しない
	/// </summary>
	/// <param name="index">開始地点を0とした移動先の状態番号</param>
	/// <returns>true:成功、または現在地点 false:編集中、範囲外、または復元失敗</returns>
	/// <remarks>シーンとの不一致があれば履歴を無効化する。確保失敗時は履歴を保持する</remarks>
	bool MoveTo(std::size_t index);

	/// <summary>
	/// 直前の操作を元に戻す
	/// </summary>
	/// <returns>true:成功 false:実行不可、または履歴とシーンが一致しない</returns>
	bool Undo();

	/// <summary>
	/// 元に戻した操作をやり直す
	/// </summary>
	/// <returns>true:成功 false:実行不可、または履歴とシーンが一致しない</returns>
	bool Redo();

	/// <summary>
	/// 保存地点から変更されているか確認する
	/// </summary>
	/// <returns>true:変更あり false:保存地点と一致</returns>
	bool IsModified() const;

	/// <summary>
	/// 編集を確定し、現在の状態を保存地点として記録する
	/// </summary>
	void MarkSaved();

	/// <summary>
	/// 履歴対象外の変更を記録し、それ以前の履歴を無効にする
	/// </summary>
	void Invalidate();

	/// <summary>
	/// 履歴と編集状態をクリアし、変更なしの状態へ戻す
	/// </summary>
	void Reset();

	/// <summary>
	/// 確定済みの履歴を複製し、移動前のパスを参照する値を書き換える
	/// </summary>
	std::unique_ptr<EditorHistory>
	PrepareAssetRewrite(const SceneAssetReferences::ObjectRewrite &rewrite) const;

	/// <summary>
	/// 素材移動が成功したあと、準備済みの履歴を反映する。保存地点と操作位置は保持する
	/// </summary>
	void ApplyAssetRewrite(EditorHistory &prepared) noexcept;

	/// <summary>
	/// UndoとRedoが保持する物体・コンポーネントの値を読み取る
	/// </summary>
	void VisitStoredObjects(const std::function<void(const SceneObject &)> &visit) const;

	/// <summary>
	/// 物体を作成し、その追加を履歴へ記録する。
	/// 編集中の変形情報は先に確定する
	/// </summary>
	/// <param name="name">物体の名前</param>
	/// <param name="modelPath">モデルの参照先。空の場合はモデルなし</param>
	/// <returns>作成した物体のID</returns>
	SceneObjectId CreateObject(std::string name = "Object",
	                           std::filesystem::path modelPath = {});

	/// <summary>
	/// 全物体を作成し、1操作として履歴へ記録する。編集中の内容は先に確定する。
	/// 既存のPrimary Cameraを優先し、なければ入力中の最初のPrimaryだけを保持する
	/// </summary>
	/// <param name="objects">作成する物体のデータ一覧</param>
	/// <param name="sourceIds">入力順の重複しない複製元ID。指定時は入力内への参照を作成先へ付け替える。空なら付け替えなし</param>
	/// <remarks>複製元IDの指定時、入力範囲外への参照は作成前のシーンに対象が存在するときだけ保持する</remarks>
	/// <returns>作成順のID一覧。対象なし、またはデータが無効な場合は空</returns>
	std::vector<SceneObjectId>
	CreateObjects(std::vector<SceneObject> objects,
	              std::span<const SceneObjectId> sourceIds = {});

	/// <summary>
	/// 指定した物体を削除し、残る物体からの追従参照の解除も1操作として記録する。
	/// 編集中の変形情報は先に確定する
	/// </summary>
	/// <param name="objectId">削除する物体のID</param>
	/// <returns>true:削除成功 false:物体が存在しない</returns>
	bool DeleteObject(SceneObjectId objectId);

	/// <summary>
	/// 選択内の最上位の物体を新しい親へ移し、1回のUndo・Redoとして記録する
	/// </summary>
	/// <param name="ids">重複のない選択ID。選択された祖先がある子は親と一緒に移動する</param>
	/// <param name="parent">新しい親。0はルート</param>
	/// <param name="keepWorld">true:ワールド行列を維持 false:ローカル変形を維持</param>
	/// <returns>true:成功または変更なし false:対象・関係・変形が無効。失敗時は状態を保持する</returns>
	bool SetParent(std::span<const SceneObjectId> ids, SceneObjectId parent,
	               bool keepWorld = true);

	/// <summary>
	/// 物体を複製し、その追加を履歴へ記録する。
	/// 編集中の変形情報は先に確定し、Primary Cameraは既存の物体を優先する。
	/// 複製範囲外への参照は元の物体を指す
	/// </summary>
	/// <param name="objectId">複製元の物体ID</param>
	/// <returns>成功:複製した物体のID 対象なし:0</returns>
	SceneObjectId DuplicateObject(SceneObjectId objectId);

	/// <summary>
	/// 初期設定へ戻すコンポーネントの範囲
	/// </summary>
	enum class ComponentSettings {
		CHARACTER,
		FOLLOW_CAMERA,
		RESPAWN,
		CAMERA_LENS,
	};

	/// <summary>
	/// 指定した設定を初期値へ戻し、1操作として記録する。連続編集は先に確定する。
	/// Follow Cameraの追従先と、CameraのPrimaryは保持する
	/// </summary>
	/// <param name="id">設定を戻す物体ID</param>
	/// <param name="settings">リセットする設定の範囲</param>
	/// <returns>true:変更成功または同値 false:対象や設定が無効。失敗時はリセットを適用しない</returns>
	/// <remarks>履歴の領域を変更前に確保する。同値ではRedo側の履歴を保持する</remarks>
	bool ResetComponentSettings(SceneObjectId id, ComponentSettings settings);

	/// <summary>
	/// 現在の名前を保持し、連続した名前編集を開始する
	/// </summary>
	/// <param name="objectId">編集する物体のID</param>
	/// <returns>true:開始成功 false:物体がない</returns>
	bool BeginRename(SceneObjectId objectId);

	/// <summary>
	/// 衝突形状を追加・変更・削除し、1操作として記録する
	/// </summary>
	/// <param name="objectId">対象ID</param>
	/// <param name="collider">変更後の形状。未指定は削除</param>
	/// <returns>true:成功または変更なし false:対象なし、または形状が無効</returns>
	bool SetCollider(SceneObjectId objectId, std::optional<Collider> collider);

	/// <summary>
	/// 1物体の衝突形状の変更内容
	/// </summary>
	struct ColliderEdit {
		SceneObjectId objectId = 0;
		std::optional<Collider> collider;
	};

	/// <summary>
	/// 複数物体の衝突形状を一括変更し、1回のUndo操作として記録する
	/// </summary>
	/// <param name="edits">重複しない対象IDと変更後の形状。未指定の形状は削除</param>
	/// <returns>true:全件成功または同値 false:対象なし、重複、または変更前後の形状が無効</returns>
	/// <remarks>変更を適用すると物体一覧を置き換えるため、保持している物体参照は取得し直す</remarks>
	bool SetColliders(std::span<const ColliderEdit> edits);

	/// <summary>
	/// 衝突形状の連続編集を開始する
	/// </summary>
	/// <param name="objectId">衝突形状を持つ対象のID</param>
	/// <returns>true:開始成功 false:対象または衝突形状なし</returns>
	bool BeginCollider(SceneObjectId objectId);

	/// <summary>
	/// Cameraコンポーネントを追加・変更・削除して履歴へ記録する。
	/// Primaryを指定する場合は他のCameraを解除し、全変更を1操作にまとめる
	/// </summary>
	/// <param name="objectId">対象ID</param>
	/// <param name="value">変更後。未指定は削除</param>
	/// <returns>true:成功または同値 false:対象または設定が無効</returns>
	/// <remarks>Primary指定時は物体一覧を置き換えるため、保持している物体参照は取得し直す</remarks>
	bool SetCamera(SceneObjectId objectId,
	               std::optional<CameraComponent> value);

	/// <summary>
	/// 復帰設定を全対象へ適用し、1操作として記録する。Respawnがない物体には追加する
	/// </summary>
	/// <param name="ids">重複のない物体ID。Transformと他のコンポーネントは保持する</param>
	/// <param name="settings">有効な落下高度・開始位置の使用設定・明示的な復帰位置</param>
	/// <returns>true:全件成功または同値 false:対象・変更前後の設定が無効。失敗時は全対象を保持する</returns>
	/// <remarks>編集中の値は先に確定する。成功時は物体参照を取得し直す</remarks>
	bool SetRespawnSettings(std::span<const SceneObjectId> ids,
	                        const RespawnSettings &settings);

	/// <summary>
	/// 親と追従先の開始姿勢を解決してCameraをSceneビューへ合わせ、追従オフセットも記録する。
	/// 親・ローカル拡大率・Primary・追従速度は保持し、編集中の内容は先に確定する
	/// </summary>
	/// <param name="objectId">Cameraを持つ対象ID</param>
	/// <param name="pose">Sceneビューの姿勢。Follow Cameraの上方向はワールドYを基準とする</param>
	/// <returns>true:成功または同値 false:対象・姿勢・追従先・オフセットが無効。配置は変更しない</returns>
	bool AlignCameraView(SceneObjectId objectId, const CameraPose &pose);

	/// <summary>
	/// カメラの連続編集を開始する
	/// </summary>
	/// <param name="objectId">対象ID</param>
	/// <returns>true:開始成功 false:対象またはCameraなし</returns>
	bool BeginCamera(SceneObjectId objectId);

	/// <summary>
	/// Respawnコンポーネントを追加・変更・削除して履歴へ記録する
	/// </summary>
	/// <param name="objectId">対象ID</param>
	/// <param name="value">新しい設定。未指定は削除</param>
	/// <returns>true:成功または同値 false:対象または設定が無効</returns>
	bool SetRespawn(SceneObjectId objectId,
	                std::optional<RespawnSettings> value);

	/// <summary>
	/// Respawnの連続編集を開始する
	/// </summary>
	/// <param name="objectId">対象ID</param>
	/// <returns>true:開始成功 false:対象またはコンポーネントなし</returns>
	bool BeginRespawn(SceneObjectId objectId);

	/// <summary>
	/// モデルの色の追加・変更・削除を1回の履歴操作として適用する
	/// </summary>
	/// <param name="objectId">編集する物体</param>
	/// <param name="value">有効なRGB設定。nulloptは削除</param>
	/// <returns>true:変更成功または同値 false:対象や設定が無効</returns>
	bool SetColorTint(SceneObjectId objectId, std::optional<ColorTint> value);

	/// <summary>
	/// 物体の材質スロットへの割り当てを1回の履歴として変更する
	/// </summary>
	/// <param name="id">編集する物体</param>
	/// <param name="slot">モデルの材質スロット番号</param>
	/// <param name="path">画像ファイルの参照先。空は白いテクスチャへ戻す</param>
	/// <returns>true:成功または同値 false:対象や設定が無効</returns>
	bool SetTexturePath(SceneObjectId id, std::uint32_t slot,
	                    const std::filesystem::path &path);

	/// <summary>
	/// 全対象の同じ材質スロットを変更し、1回のUndo操作へまとめる。編集中の値は先に確定する
	/// </summary>
	/// <param name="ids">重複しない対象ID。全物体に有効なMesh Rendererが必要</param>
	/// <param name="slot">変更するスロット番号。モデルで未使用の番号も保持できる</param>
	/// <param name="path">画像ファイル。空はそのスロットの明示的な割り当てだけを解除する</param>
	/// <returns>true:全件成功または同値 false:対象・設定が無効。失敗時は割り当てを変更しない</returns>
	bool SetTexturePaths(std::span<const SceneObjectId> ids, std::uint32_t slot,
	                     const std::filesystem::path &path);

	/// <summary>
	/// 色の連続編集を開始し、編集前の設定を保持する
	/// </summary>
	/// <param name="objectId">色の設定を持つ物体</param>
	/// <returns>true:開始成功 false:対象や色の設定なし</returns>
	bool BeginColorTint(SceneObjectId objectId);

	/// <summary>
	/// 点光源の連続編集を開始する。現在の編集は先に確定する
	/// </summary>
	/// <param name="objectId">点光源を持つ物体</param>
	/// <returns>true:開始 false:物体または点光源なし</returns>
	bool BeginPointLight(SceneObjectId objectId);

	/// <summary>
	/// 音源の追加・設定・削除を1回の履歴に記録する。未割り当ての参照は空で保持できる
	/// </summary>
	/// <param name="objectId">既存の物体</param>
	/// <param name="value">有効な設定。nulloptは削除</param>
	/// <returns>true:成功または同値 false:対象または設定が無効</returns>
	bool SetAudioSource(SceneObjectId objectId,
	                    std::optional<AudioSourceComponent> value);

	/// <summary>
	/// 音量と減衰距離の連続編集を開始する。現在の編集は先に確定する
	/// </summary>
	/// <param name="objectId">音源を持つ物体</param>
	/// <returns>true:開始 false:対象または音源なし</returns>
	bool BeginAudioSource(SceneObjectId objectId);

	/// <summary>
	/// リスナーの追加・削除を1回の履歴に記録する。有効リスナーの重複はScene CheckとPlayで検出する
	/// </summary>
	/// <param name="objectId">既存の物体</param>
	/// <param name="value">リスナー設定。nulloptは削除</param>
	/// <returns>true:成功または同値 false:対象なし</returns>
	bool SetAudioListener(SceneObjectId objectId,
	                      std::optional<AudioListenerComponent> value);

	/// <summary>
	/// 点光源の追加・設定・削除を1回の履歴に記録する
	/// </summary>
	/// <param name="objectId">既存の物体</param>
	/// <param name="value">有効な設定。nulloptは削除</param>
	/// <returns>true:成功または同値 false:対象または設定が無効</returns>
	bool SetPointLight(SceneObjectId objectId,
	                   std::optional<PointLightComponent> value);

	/// <summary>
	/// スポットライトの追加・設定・削除を1回の履歴に記録する
	/// </summary>
	/// <param name="objectId">既存の物体</param>
	/// <param name="value">有効な設定。nulloptは削除</param>
	/// <returns>true:成功または同値 false:対象または設定が無効</returns>
	bool SetSpotLight(SceneObjectId objectId,
	                  std::optional<SpotLightComponent> value);

	/// <summary>
	/// 色・強さ・範囲・照射角の連続編集を開始する。現在の編集は先に確定する
	/// </summary>
	/// <param name="objectId">スポットライトを持つ物体</param>
	/// <returns>true:開始 false:物体または光源なし</returns>
	bool BeginSpotLight(SceneObjectId objectId);

	/// <summary>
	/// Mesh Rendererの連続編集を開始し、UV・画像・表示状態の編集前の値を保持する
	/// </summary>
	/// <param name="objectId">Mesh Rendererを持つ物体</param>
	/// <returns>true:開始成功 false:対象またはMesh Rendererなし</returns>
	bool BeginMeshRenderer(SceneObjectId objectId);

	/// <summary>
	/// 選択物体のUVだけを1操作でそろえる。画像・表示状態・他コンポーネントは保持する
	/// </summary>
	/// <param name="ids">重複しない対象ID。全物体に有効なMesh Rendererが必要</param>
	/// <param name="scale">全対象へ適用する有限なUV倍率</param>
	/// <param name="offset">全対象へ適用する有限なUV移動量</param>
	/// <returns>true:全件成功または同値 false:空・対象・値が無効。全対象を変更しない</returns>
	/// <remarks>成功時は物体参照を取得し直す。連続編集中の内容は先に確定する</remarks>
	bool SetTextureUVs(std::span<const SceneObjectId> ids,
	                   const std::array<float, 2> &scale,
	                   const std::array<float, 2> &offset);

	/// <summary>
	/// FollowCameraコンポーネントを追加・変更・削除して履歴へ記録する
	/// 対象の不在や自己参照・循環する追従先は設定せず、0は追従先の解除として扱う
	/// </summary>
	/// <param name="objectId">対象ID</param>
	/// <param name="value">新しい設定。未指定は削除</param>
	/// <returns>true:成功または同値 false:対象または設定が無効</returns>
	bool SetFollowCamera(SceneObjectId objectId,
	                     std::optional<FollowCameraSettings> value);

	/// <summary>
	/// FollowCameraの連続編集を開始する
	/// </summary>
	/// <param name="objectId">対象ID</param>
	/// <returns>true:開始成功 false:対象またはコンポーネントなし</returns>
	bool BeginFollowCamera(SceneObjectId objectId);

	/// <summary>
	/// Characterコンポーネントを追加・変更・削除して履歴へ記録する
	/// </summary>
	/// <param name="objectId">対象ID</param>
	/// <param name="value">新しい設定。未指定は削除</param>
	/// <returns>true:成功または同値 false:対象または設定が無効</returns>
	bool SetCharacter(SceneObjectId objectId,
	                  std::optional<CharacterSettings> value);

	/// <summary>
	/// Characterの連続編集を開始する
	/// </summary>
	/// <param name="objectId">対象ID</param>
	/// <returns>true:開始成功 false:対象またはコンポーネントなし</returns>
	bool BeginCharacter(SceneObjectId objectId);

	/// <summary>
	/// 指定した全物体を削除し、残る物体からの追従参照の解除も1操作として記録する。
	/// 編集中の内容は先に確定し、Undoでは物体と参照をまとめて復元する
	/// </summary>
	/// <param name="objectIds">重複のない削除対象ID一覧</param>
	/// <returns>true:全件削除成功 false:対象なし、重複、存在しないID、または解除する追従設定が無効</returns>
	bool DeleteObjects(std::span<const SceneObjectId> objectIds);

	/// <summary>
	/// 指定した全物体を複製し、1操作として履歴へ記録する。
	/// 編集中の内容は先に確定し、Primary Cameraは既存の物体を優先する。
	/// 複製する物体どうしの参照は、対応する複製先へ付け替える
	/// </summary>
	/// <param name="objectIds">重複のない複製元ID一覧</param>
	/// <returns>入力順に対応する複製先ID一覧。対象が無効な場合は空</returns>
	std::vector<SceneObjectId>
	DuplicateObjects(std::span<const SceneObjectId> objectIds);

	/// <summary>
	/// 全物体の現在の変形情報を保持し、連続した変形編集を開始する。
	/// 編集中の内容は先に確定する
	/// </summary>
	/// <param name="objectIds">重複のない編集対象ID一覧</param>
	/// <returns>true:開始成功 false:対象なし、重複、または変形情報が無効</returns>
	bool BeginTransforms(std::span<const SceneObjectId> objectIds);

	/// <summary>
	/// ギズモの移動・回転・均等拡縮を、編集開始時点の全物体へ適用する
	/// </summary>
	/// <param name="beforePivot">編集開始時点のギズモの変形情報。拡大率は正</param>
	/// <param name="afterPivot">現在のギズモの変形情報。各軸の倍率は同じ正の値</param>
	/// <returns>true:全件適用成功 false:複数編集なし、対象なし、または値が無効</returns>
	bool UpdateTransforms(const Transform &beforePivot,
	                      const Transform &afterPivot);

	/// <summary>
	/// 全物体の位置・回転・拡大率を初期値へ戻し、1操作として記録する
	/// </summary>
	/// <param name="objectIds">重複のない編集対象ID一覧</param>
	/// <returns>true:成功、または変更なし false:対象なし、重複、またはデータが無効</returns>
	bool ResetTransforms(std::span<const SceneObjectId> objectIds);

	/// <summary>
	/// モデル参照を差し替え、その変更を1操作として履歴に記録する
	/// </summary>
	/// <param name="objectId">対象物体のID</param>
	/// <param name="modelPath">変更先のモデル。空の場合は参照を解除する</param>
	/// <returns>true:変更成功、または変更なし false:物体が存在しない</returns>
	bool SetModelPath(SceneObjectId objectId, std::filesystem::path modelPath);

	/// <summary>
	/// 既存のMeshをまとめて差し替え、他のコンポーネントを保持して1操作として記録する
	/// </summary>
	/// <param name="ids">重複のない対象ID一覧。全物体に有効なMeshが必要</param>
	/// <param name="modelPath">差し替えるモデル。空の参照は指定できない</param>
	/// <returns>true:全件成功または変更なし false:対象・Mesh・パスが無効。全件変更しない</returns>
	bool SetModelPaths(std::span<const SceneObjectId> ids,
	                   const std::filesystem::path &modelPath);

	/// <summary>
	/// Meshコンポーネントを追加・変更・削除する。描画設定は保持する
	/// </summary>
	/// <param name="id">対象の物体</param>
	/// <param name="value">設定するMesh。nulloptは削除</param>
	/// <returns>true:変更成功または同値 false:対象や設定が無効</returns>
	bool SetMesh(SceneObjectId id, std::optional<MeshComponent> value);

	/// <summary>
	/// Mesh Rendererを追加・変更・削除する。Meshの参照は保持する
	/// </summary>
	/// <param name="id">対象の物体</param>
	/// <param name="value">設定する描画情報。nulloptは削除</param>
	/// <returns>true:変更成功または同値 false:対象や設定が無効</returns>
	bool SetMeshRenderer(SceneObjectId id,
	                     std::optional<MeshRendererComponent> value);

	/// <summary>
	/// 選択物体の表示状態をまとめて変更し、1操作として記録する
	/// </summary>
	/// <param name="objectIds">重複のない対象ID一覧</param>
	/// <param name="visible">変更後の表示状態</param>
	/// <returns>true:変更成功、または変更なし false:対象なし、重複、存在しないIDあり</returns>
	bool SetVisibility(std::span<const SceneObjectId> objectIds, bool visible);

	/// <summary>
	/// 指定した物体自身の有効設定をまとめて変更し、1操作として記録する。子の設定は上書きしない
	/// </summary>
	/// <param name="objectIds">重複のない既存ID一覧</param>
	/// <param name="active">変更後のローカル有効設定</param>
	/// <returns>true:変更成功または同値 false:対象なし、重複、不在ID、または履歴との不一致</returns>
	bool SetActive(std::span<const SceneObjectId> objectIds, bool active);

	/// <summary>
	/// Trigger Actionsの追加・変更・削除を1操作として記録する。未割り当ての対象は0で保持できる
	/// </summary>
	/// <param name="id">既存の物体ID</param>
	/// <param name="value">変更後の設定。nulloptは削除</param>
	/// <returns>true:成功または同値 false:対象・操作種別・操作数・参照先が無効</returns>
	bool SetTriggerActions(SceneObjectId id,
	                       std::optional<TriggerActionsComponent> value);

	/// <summary>
	/// 指定物体へ同じ変形情報を適用し、1操作として記録する。編集中の内容は先に確定する
	/// </summary>
	/// <param name="objectIds">重複のない対象ID一覧</param>
	/// <param name="transform">適用する有効な変形情報</param>
	/// <returns>true:成功、または変更なし false:対象や変形情報が無効</returns>
	bool SetTransforms(std::span<const SceneObjectId> objectIds,
	                   const Transform &transform);

	/// <summary>
	/// 物体ごとに指定する変更後の変形情報
	/// </summary>
	struct TransformEdit {
		SceneObjectId objectId = 0;
		Transform transform{};
	};

	/// <summary>
	/// 物体ごとに異なる変形情報を一括適用し、1操作として記録する。編集中の値は先に確定する
	/// </summary>
	/// <param name="edits">重複しない対象IDと有効な変形情報。回転は正規化して使用する</param>
	/// <returns>true:全件成功または同値 false:対象・値が無効。失敗時は全対象を保持する</returns>
	/// <remarks>成功時は物体参照を取得し直す。Transform以外のコンポーネントは保持する</remarks>
	bool SetTransforms(std::span<const TransformEdit> edits);

	/// <summary>
	/// 選択内の最上位の対象へ同じワールド移動量を加え、子孫を一度だけ移動して記録する
	/// </summary>
	/// <param name="objectIds">重複のない対象ID一覧</param>
	/// <param name="translation">ワールド座標での移動量</param>
	/// <returns>true:成功、または変更なし false:対象や値が無効。全件を変更しない</returns>
	bool TranslateObjects(std::span<const SceneObjectId> objectIds,
	                      const DirectX::XMFLOAT3 &translation);

	/// <summary>
	/// 親を保ったままワールド位置を指定し、ローカル変形の変更を履歴へ記録する
	/// </summary>
	/// <param name="id">既存の物体ID</param>
	/// <param name="position">有限なワールド位置</param>
	/// <returns>true:成功または同値 false:対象・位置・変形が無効。失敗時は状態を保持する</returns>
	bool SetWorldPosition(SceneObjectId id, const DirectX::XMFLOAT3 &position);

	/// <summary>
	/// 各物体の原点をワールド座標のグリッドへ揃え、全変更を1操作として記録する
	/// </summary>
	/// <param name="objectIds">重複のない対象ID。編集中の内容は先に確定する</param>
	/// <param name="step">各軸の正で有限の間隔。中間点は0から遠い方へ丸める</param>
	/// <returns>true:全件成功または同値 false:対象・値が無効。失敗時は全対象を保持する</returns>
	/// <remarks>回転・拡大率・他コンポーネントは保持する。成功時は物体参照を取得し直す</remarks>
	bool SnapObjectsToGrid(std::span<const SceneObjectId> objectIds,
	                       const DirectX::XMFLOAT3 &step);

	/// <summary>
	/// 全対象の名前を変更し、1操作として記録する。編集中の内容は先に確定する
	/// </summary>
	/// <param name="renames">重複のない対象と変更前後の名前。変更後の名前は空にしない</param>
	/// <returns>true:成功、または変更なし false:対象や名前が無効、またはプレビューから名前が変わっている</returns>
	bool RenameObjects(std::span<const ObjectRename> renames);

	/// <summary>
	/// 全対象の原点を基準物体の指定軸の座標へそろえ、1操作として記録する。編集中の内容は先に確定する
	/// </summary>
	/// <param name="objectIds">重複のない対象ID一覧</param>
	/// <param name="referenceId">一覧に含まれる基準物体のID</param>
	/// <param name="axis">ワールド座標のX・Y・Zのいずれか</param>
	/// <returns>true:成功、または変更なし false:対象や値が無効。全件を変更しない</returns>
	bool AlignObjects(std::span<const SceneObjectId> objectIds,
	                  SceneObjectId referenceId, AlignmentAxis axis);

	/// <summary>
	/// 指定軸の座標順で両端を固定し、間の原点を等間隔に配置して1操作として記録する
	/// </summary>
	/// <param name="objectIds">重複のない3件以上の対象ID一覧。編集中の内容は先に確定する</param>
	/// <param name="axis">ワールド座標のX・Y・Zのいずれか。同じ座標ではID順を使用する</param>
	/// <returns>true:成功、または変更なし false:対象や値が無効。全件を変更しない</returns>
	bool DistributeObjects(std::span<const SceneObjectId> objectIds,
	                       AlignmentAxis axis);

	/// <summary>
	/// 作業シーンへの変更を検証し、追加・変更・削除を1回の履歴操作として確定する
	/// </summary>
	/// <param name="operation">作業用コピーだけを変更する処理。失敗時はfalse</param>
	/// <param name="description">静的な寿命を持つ履歴の表示名</param>
	/// <returns>true:成功または同値 false:編集中、操作失敗、または無効なデータ。シーンと履歴を保持する</returns>
	bool ApplySceneChanges(const std::function<bool(Scene &)> &operation,
	                       std::string_view description);

	/// <summary>
	/// 経路移動を追加・変更・削除し、1回の履歴へ記録する
	/// </summary>
	/// <param name="id">編集する物体</param>
	/// <param name="value">設定。nulloptは削除</param>
	/// <returns>true:変更成功または同値 false:対象または値が無効</returns>
	bool SetMotionPath(SceneObjectId id,
	                   std::optional<MotionPathComponent> value);

	/// <summary>
	/// 経路の連続編集を開始する
	/// </summary>
	/// <param name="id">経路を持つ物体</param>
	/// <returns>true:開始成功 false:対象または経路なし</returns>

	bool BeginMotionPath(SceneObjectId id);

	/// <summary>
	/// 変形アニメーションを追加・変更・削除し、1回の履歴へ記録する
	/// </summary>
	/// <param name="id">編集する物体</param>
	/// <param name="value">キーと再生設定。nulloptは削除</param>
	/// <returns>true:変更成功または同値 false:対象または値が無効</returns>
	bool
	SetTransformAnimation(SceneObjectId id,
	                      std::optional<TransformAnimationComponent> value);

	/// <summary>
	/// 変形アニメーションの連続編集を開始する
	/// </summary>
	/// <param name="id">コンポーネントを持つ物体</param>
	/// <returns>true:開始成功 false:対象またはコンポーネントなし</returns>

	bool BeginTransformAnimation(SceneObjectId id);

	/// <summary>
	/// 粒子の発生元を追加・変更・削除し、1回の履歴へ記録する
	/// </summary>
	/// <param name="id">編集する物体</param>
	/// <param name="value">発生・画像・色の設定。nulloptは削除</param>
	/// <returns>true:変更成功または同値 false:対象または値が無効</returns>
	bool SetParticleEmitter(SceneObjectId id,
	                        std::optional<ParticleEmitterComponent> value);

	/// <summary>
	/// 粒子の発生設定の連続編集を開始する
	/// </summary>
	/// <param name="id">発生元を持つ物体</param>
	/// <returns>true:開始成功 false:対象または発生元なし</returns>
	bool BeginParticleEmitter(SceneObjectId id);

	/// <summary>
	/// モデル内クリップの再生設定を追加・変更・削除し、1回の履歴へ記録する
	/// </summary>
	/// <param name="id">編集する物体</param>
	/// <param name="value">設定。nulloptは削除</param>
	/// <returns>true:変更成功または同値 false:対象または設定が無効</returns>
	bool SetAnimator(SceneObjectId id, std::optional<AnimatorComponent> value);

	/// <summary>
	/// モデル内クリップの速度などの連続編集を開始する
	/// </summary>
	/// <param name="id">Animatorを持つ物体</param>
	/// <returns>true:開始成功 false:対象またはAnimatorなし</returns>
	bool BeginAnimator(SceneObjectId id);

	/// <summary>
	/// Lua Scriptの追加・設定・削除を、初期状態を正規化して1操作として記録する
	/// </summary>
	/// <param name="id">対象物体</param>
	/// <param name="value">設定。nulloptは削除</param>
	/// <returns>true:反映成功または同じ値 false:設定・対象が無効</returns>
	bool SetScript(SceneObjectId id, std::optional<ScriptComponent> value);

	/// <summary>
	/// Canvasの追加・設定・削除を1操作として記録する
	/// </summary>
	/// <param name="id">編集する物体ID</param>
	/// <param name="value">設定。nulloptは削除</param>
	/// <returns>true:反映成功または同じ値 false:対象・設定が無効</returns>
	bool SetCanvas(SceneObjectId id, std::optional<CanvasComponent> value);
	/// <summary>
	/// Canvasの連続編集の開始値を保持する
	/// </summary>
	/// <param name="id">編集する物体ID</param>
	/// <returns>true:編集開始 false:対象またはコンポーネントなし</returns>
	bool BeginCanvas(SceneObjectId id);

	/// <summary>
	/// Rect Transformの追加・設定・削除を1操作として記録する
	/// </summary>
	/// <param name="id">編集する物体ID</param>
	/// <param name="value">設定。nulloptは削除</param>
	/// <returns>true:反映成功または同じ値 false:対象・設定が無効</returns>
	bool SetRectTransform(SceneObjectId id,
	                      std::optional<RectTransformComponent> value);
	/// <summary>
	/// Rect Transformの連続編集の開始値を保持する
	/// </summary>
	/// <param name="id">編集する物体ID</param>
	/// <returns>true:編集開始 false:対象またはコンポーネントなし</returns>
	bool BeginRectTransform(SceneObjectId id);

	/// <summary>
	/// UI Textの追加・設定・削除を1操作として記録する
	/// </summary>
	/// <param name="id">編集する物体ID</param>
	/// <param name="value">設定。nulloptは削除</param>
	/// <returns>true:反映成功または同じ値 false:対象・設定が無効</returns>
	bool SetText(SceneObjectId id, std::optional<TextComponent> value);
	/// <summary>
	/// UI Textの連続編集の開始値を保持する
	/// </summary>
	/// <param name="id">編集する物体ID</param>
	/// <returns>true:編集開始 false:対象またはコンポーネントなし</returns>
	bool BeginText(SceneObjectId id);

	/// <summary>
	/// UI Imageの追加・設定・削除を1操作として記録する
	/// </summary>
	/// <param name="id">編集する物体ID</param>
	/// <param name="value">設定。nulloptは削除</param>
	/// <returns>true:反映成功または同じ値 false:対象・設定が無効</returns>
	bool SetImage(SceneObjectId id, std::optional<ImageComponent> value);
	/// <summary>
	/// UI Imageの連続編集の開始値を保持する
	/// </summary>
	/// <param name="id">編集する物体ID</param>
	/// <returns>true:編集開始 false:対象またはコンポーネントなし</returns>
	bool BeginImage(SceneObjectId id);

	/// <summary>
	/// UI Buttonの追加・設定・削除を1回の履歴へ記録する
	/// </summary>
	/// <param name="id">編集する物体</param>
	/// <param name="value">設定。nulloptは削除</param>
	/// <returns>true:反映成功または同じ値 false:対象・設定が無効</returns>
	bool SetButton(SceneObjectId id, std::optional<ButtonComponent> value);
	/// <summary>
	/// ボタン表示色の連続編集の開始値を保持する
	/// </summary>
	/// <param name="id">編集する物体</param>
	/// <returns>true:開始成功 false:対象またはButtonなし</returns>
	bool BeginButton(SceneObjectId id);

	/// <summary>
	/// Rigid Bodyの追加・変更・削除を1回の履歴へ記録する
	/// </summary>
	/// <param name="id">編集する物体ID</param>
	/// <param name="value">設定。nulloptは削除</param>
	/// <returns>true:反映成功または同じ値 false:対象・設定が無効</returns>
	bool SetRigidBody(SceneObjectId id, std::optional<RigidBodySettings> value);

	/// <summary>
	/// 剛体設定の連続編集の開始値を保持する
	/// </summary>
	/// <param name="id">編集する物体ID</param>
	/// <returns>true:開始成功 false:対象またはコンポーネントなし</returns>
	bool BeginRigidBody(SceneObjectId id);

	/// <summary>
	/// シーン固有の照明設定を変更し、履歴に登録する
	/// </summary>
	bool SetSceneLighting(std::optional<LightingSettings> lighting,
	                      const FogSettings &fog);

  private:
	struct SceneLightingCommand {
		std::optional<LightingSettings> before;
		std::optional<LightingSettings> after;
		FogSettings fogBefore;
		FogSettings fogAfter;

		bool Undo(Scene &scene) const;
		bool Redo(Scene &scene) const;
	};

	// 単独の編集操作、または複数の編集操作を保持する
	using Command = std::variant<
	    TransformCommand, ObjectCommand, ParentCommand, RenameCommand,
	    ModelPathCommand, VisibilityCommand, ActiveCommand, ColliderCommand,
	    CharacterCommand, FollowCameraCommand, TriggerActionsCommand,
	    ScriptCommand, PointLightCommand, SpotLightCommand, MotionPathCommand,
	    TransformAnimationCommand, ParticleEmitterCommand, AnimatorCommand,
	    CanvasCommand, ButtonCommand, RigidBodyCommand, RectTransformCommand,
	    TextCommand, ImageCommand, AudioSourceCommand, AudioListenerCommand,
	    RespawnCommand, CameraCommand, MeshRendererCommand, ColorTintCommand,
	    TextureSlotsCommand, CommandGroup, ScenePatchCommand, SceneLightingCommand>;

	/// <summary>
	/// 操作と、その前後の状態を識別する情報
	/// </summary>
	struct Entry {
		Command command;

		std::uint64_t beforeState = 0;
		std::uint64_t afterState = 0;
		std::string_view description;
	};

	/// <summary>
	/// 連続した編集を開始した時点の情報
	/// </summary>
	struct PendingEdit {
		SceneObjectId objectId = 0;

		// 変形・名前・各コンポーネントの編集開始時点の値
		std::variant<
		    Transform, std::string, std::optional<Collider>,
		    std::optional<CharacterSettings>,
		    std::optional<FollowCameraSettings>, std::optional<CameraComponent>,
		    std::optional<RespawnSettings>, std::optional<ColorTint>,
		    std::optional<PointLightComponent>,
		    std::optional<SpotLightComponent>,
		    std::optional<MotionPathComponent>,
		    std::optional<TransformAnimationComponent>,
		    std::optional<ParticleEmitterComponent>,
		    std::optional<AnimatorComponent>, std::optional<CanvasComponent>,
		    std::optional<ButtonComponent>, std::optional<RigidBodySettings>,
		    std::optional<RectTransformComponent>, std::optional<TextComponent>,
		    std::optional<ImageComponent>,

		    std::optional<AudioSourceComponent>,
		    std::optional<MeshRendererComponent>>
		    before;
	};

	/// <summary>
	/// 複数物体の編集開始時点の変形情報
	/// </summary>
	struct TransformSnapshot {
		SceneObjectId objectId = 0;
		Transform before;
		// 共有ギズモでは開始時の親とワールド行列も保持する。
		SceneObjectId parentId = 0;
		SceneWorldTransform world{};
	};

  private:
	/// <summary>
	/// ワールド位置をローカル変形へ変換し、全変更を1つの履歴として反映する
	/// </summary>
	/// <param name="positions">既存IDとワールド位置。空は無効</param>
	/// <param name="description">履歴に保持する、静的な寿命を持つ表示名</param>
	/// <returns>true:成功または同値 false:位置・関係・変形が無効。失敗時は全物体を保持する</returns>
	bool ApplyWorldPositions(
	    const std::map<SceneObjectId, DirectX::XMFLOAT3> &positions,
	    std::string_view description);

	/// <summary>
	/// 新しい状態の識別番号を発行する
	/// </summary>
	/// <returns>過去の状態と重複しない番号</returns>
	std::uint64_t AllocateState();

	/// <summary>
	/// 次の操作を記録する領域と状態番号を確保する。
	/// シーンを変更する前に呼び出す
	/// </summary>
	/// <returns>次の操作に割り当てる状態番号</returns>
	std::uint64_t PrepareAppend();

	/// <summary>
	/// シーンへ反映済みの操作を、確保済みの領域へ記録する
	/// </summary>
	/// <param name="command">反映済みの操作</param>
	/// <param name="nextState">PrepareAppendで取得した状態番号</param>
	/// <param name="description">静的な寿命を持つ操作の表示名</param>
	void AppendPrepared(Command command, std::uint64_t nextState,
	                    std::string_view description) noexcept;

	/// <summary>
	/// 物体の全データを使って作成し、追加操作を履歴へ記録する
	/// </summary>
	/// <param name="object">作成する物体のデータ</param>
	/// <returns>作成した物体のID</returns>
	/// <remarks>呼び出し前にCommitEditを呼び出す</remarks>
	SceneObjectId CreateObjectWithHistory(SceneObject object);

	/// <summary>
	/// 編集開始時点から値が変わっているか確認する
	/// </summary>
	/// <returns>true:変更あり、または対象がない false:変更なし</returns>
	bool HasPendingChanges() const;

  private:
	// 整列後の端に許容するワールド座標の誤差
	static constexpr double kColliderAlignmentTolerance = 0.0001;

	Scene &scene_;

	std::vector<Entry> entries_;
	std::size_t cursor_ = 0;

	std::optional<PendingEdit> pending_;
	std::optional<std::vector<TransformSnapshot>> pendingTransforms_;

	std::uint64_t currentState_ = 0;
	std::uint64_t savedState_ = 0;
	std::uint64_t nextState_ = 1;
};
