#pragma once

#include "graphics/camera/CameraPose.h"
#include "history/AlignmentAxis.h"
#include "history/ColliderCommand.h"
#include "history/CommandGroup.h"
#include "history/ComponentCommand.h"
#include "history/ModelPathCommand.h"
#include "history/ObjectCommand.h"
#include "history/ObjectRename.h"
#include "history/RenameCommand.h"
#include "history/TransformCommand.h"
#include "history/VisibilityCommand.h"
#include "scene/SceneObject.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
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
	/// 物体を複製し、その追加を履歴へ記録する。
	/// 編集中の変形情報は先に確定し、Primary Cameraは既存の物体を優先する。
	/// 複製範囲外への参照は元の物体を指す
	/// </summary>
	/// <param name="objectId">複製元の物体ID</param>
	/// <returns>成功:複製した物体のID 対象なし:0</returns>
	SceneObjectId DuplicateObject(SceneObjectId objectId);

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
	bool SetCollider(SceneObjectId objectId,
	                 std::optional<BoxCollider> collider);

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
	/// Cameraの位置と向きをSceneビューへ合わせ、追従オフセットも1操作として記録する。
	/// 拡大率・投影設定・追従速度は保持し、編集中の内容は先に確定する
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
	/// 選択物体の表示状態をまとめて変更し、1操作として記録する
	/// </summary>
	/// <param name="objectIds">重複のない対象ID一覧</param>
	/// <param name="visible">変更後の表示状態</param>
	/// <returns>true:変更成功、または変更なし false:対象なし、重複、存在しないIDあり</returns>
	bool SetVisibility(std::span<const SceneObjectId> objectIds, bool visible);

	/// <summary>
	/// 指定物体へ同じ変形情報を適用し、1操作として記録する。編集中の内容は先に確定する
	/// </summary>
	/// <param name="objectIds">重複のない対象ID一覧</param>
	/// <param name="transform">適用する有効な変形情報</param>
	/// <returns>true:成功、または変更なし false:対象や変形情報が無効</returns>
	bool SetTransforms(std::span<const SceneObjectId> objectIds,
	                   const Transform &transform);

	/// <summary>
	/// 全対象へ同じ移動量を加え、1操作として記録する。編集中の内容は先に確定する
	/// </summary>
	/// <param name="objectIds">重複のない対象ID一覧</param>
	/// <param name="translation">ワールド座標での移動量</param>
	/// <returns>true:成功、または変更なし false:対象や値が無効。全件を変更しない</returns>
	bool TranslateObjects(std::span<const SceneObjectId> objectIds,
	                      const DirectX::XMFLOAT3 &translation);

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

  private:
	// 単独の編集操作、または複数の編集操作を保持する
	using Command =
	    std::variant<TransformCommand, ObjectCommand, RenameCommand,
	                 ModelPathCommand, VisibilityCommand, ColliderCommand,
	                 CharacterCommand, FollowCameraCommand, RespawnCommand,
	                 CameraCommand, CommandGroup>;

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
		std::variant<Transform, std::string, std::optional<BoxCollider>,
		             std::optional<CharacterSettings>,
		             std::optional<FollowCameraSettings>,
		             std::optional<CameraComponent>,
		             std::optional<RespawnSettings>>
		    before;
	};

	/// <summary>
	/// 複数物体の編集開始時点の変形情報
	/// </summary>
	struct TransformSnapshot {
		SceneObjectId objectId = 0;
		Transform before;
	};

  private:
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
	Scene &scene_;

	std::vector<Entry> entries_;
	std::size_t cursor_ = 0;

	std::optional<PendingEdit> pending_;
	std::optional<std::vector<TransformSnapshot>> pendingTransforms_;

	std::uint64_t currentState_ = 0;
	std::uint64_t savedState_ = 0;
	std::uint64_t nextState_ = 1;
};
