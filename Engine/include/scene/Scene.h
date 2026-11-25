#pragma once

#include "graphics/lighting/FogSettings.h"
#include "graphics/lighting/LightingSettings.h"
#include "scene/SceneObject.h"
#include "scene/SceneObjectId.h"

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>

/// <summary>
/// 背景を保持している区間を識別する
/// </summary>

/// <summary>
/// シーン上の物体データの作成・削除・取得を管理する
/// </summary>
class Scene {
  public:
	Scene() = default;

	/// <summary>
	/// 物体と発行済みIDの上限を複製し、削除済みIDの再利用を防ぐ
	/// </summary>
	/// <param name="source">変更しない複製元</param>
	/// <remarks>このシーンで既に発行したIDの上限も保持する。失敗時は全状態を保持する</remarks>
	void CopyFrom(const Scene &source);

	/// <summary>
	/// 検証済みの作業用シーンと物体・発行済みIDを交換する
	/// </summary>
	void Swap(Scene &other) noexcept;

	/// <summary>
	/// 検証済みの作業用シーンを反映し、発行済みIDの上限を保持する
	/// </summary>
	/// <param name="candidate">反映するシーン。反映前の内容を受け取る</param>
	void AdoptValidated(Scene &candidate) noexcept;

	Scene(const Scene &) = delete;
	Scene &operator=(const Scene &) = delete;

	/// <summary>
	/// 物体を作成する。描画リソースは作成しない
	/// </summary>
	/// <param name="name">物体の名前</param>
	/// <param name="modelPath">モデルの参照先。空の場合はモデルなし</param>
	/// <returns>このシーン内で有効な物体のID</returns>
	SceneObjectId CreateObject(std::string name = "Object",
	                           std::filesystem::path modelPath = {});

	/// <summary>
	/// 指定した物体データに新しいIDを割り当てて作成する。
	/// 描画リソースは作成しない
	/// </summary>
	/// <param name="object">名前・変形情報・モデルの参照先</param>
	/// <returns>このシーン内で新しく発行した物体のID</returns>
	SceneObjectId CreateObject(SceneObject object);

	/// <summary>
	/// 指定した物体を削除する
	/// </summary>
	/// <param name="id">削除する物体のID</param>
	/// <returns>true:削除成功 false:物体が存在しない、または子がある。階層の削除では子から順に削除する</returns>
	bool DestroyObject(SceneObjectId id);

	/// <summary>
	/// 指定した物体が存在するか取得する
	/// </summary>
	/// <param name="id">確認する物体のID</param>
	/// <returns>true:物体あり false:物体なし</returns>
	bool Contains(SceneObjectId id) const;

	/// <summary>
	/// 編集用の物体データを取得する
	/// </summary>
	/// <param name="id">取得する物体のID</param>
	/// <returns>物体削除まで有効なポインター。存在しない場合はnullptr</returns>
	SceneObject *FindObject(SceneObjectId id);

	/// <summary>
	/// 読み取り専用の物体データを取得する
	/// </summary>
	/// <param name="id">取得する物体のID</param>
	/// <returns>物体削除まで有効なポインター。存在しない場合はnullptr</returns>
	const SceneObject *FindObject(SceneObjectId id) const;

	/// <summary>
	/// 全物体の一覧を取得する
	/// </summary>
	/// <returns>物体のIDとデータの対応</returns>
	const std::map<SceneObjectId, SceneObject> &GetObjects() const;

	/// <summary>
	/// 全物体とシーン固有の照明設定を削除する。発行済みのIDは再利用しない
	/// </summary>
	void Clear();

	/// <summary>
	/// 全物体を指定したデータに置き換える。取得済みのポインターは無効になる
	/// </summary>
	/// <param name="objects">置き換え後の物体一覧。IDには0と最大値を使用できない。親は同じ一覧に存在し、循環しないこと</param>
	void ReplaceObjects(std::map<SceneObjectId, SceneObject> objects);

	/// <summary>
	/// 発行済みのIDを使って、削除した物体を復元する。
	/// 描画リソースは作成しない
	/// </summary>
	/// <param name="id">このシーンで発行済みの物体ID</param>
	/// <param name="object">復元する物体データ</param>
	/// <returns>true:復元成功 false:未発行のID、同じIDの物体が存在する、または親が存在しない</returns>
	bool RestoreObject(SceneObjectId id, const SceneObject &object);

	/// <summary>
	/// 一時シーンへ操作を適用し、成功した場合だけ現在のシーンへ反映する
	/// </summary>
	/// <param name="operation">引数で渡される一時シーンだけを変更する処理</param>
	/// <returns>true:反映成功 false:操作未指定、または操作失敗</returns>
	/// <remarks>成功後、取得済みの物体ポインターとイテレーターは無効になる</remarks>
	bool ApplyTransaction(const std::function<bool(Scene &)> &operation);

	/// <summary>
	/// シーン固有の照明設定を取得する。未設定ならプロジェクトの設定を使用する
	/// </summary>
	const std::optional<LightingSettings> &GetLighting() const {
		return lighting_;
	}

	/// <summary>
	/// シーン固有の照明設定を変更する。無効な値は適用しない
	/// </summary>
	bool SetLighting(std::optional<LightingSettings> lighting);

	/// <summary>
	/// シーンの距離霧を取得する
	/// </summary>
	const FogSettings &GetFog() const { return fog_; }

	/// <summary>
	/// 有効な距離霧の設定を保存する
	/// </summary>
	bool SetFog(const FogSettings &fog);

	/// <summary>
	/// 旧形式の平行光がオブジェクトへの移行待ちか確認する
	/// </summary>
	bool UsesLegacyDirectionalLight() const { return legacyDirectionalLight_; }

	/// <summary>
	/// 旧形式の平行光をオブジェクトへ移行する必要があるか設定する
	/// </summary>
	void SetUseLegacyDirectionalLight(bool enabled) {
		legacyDirectionalLight_ = enabled;
	}

  private:
	std::optional<LightingSettings> lighting_;
	FogSettings fog_;
	bool legacyDirectionalLight_ = false;
	std::map<SceneObjectId, SceneObject> objects_;
	SceneObjectId nextObjectId_ = 1;
};
