#pragma once

#include "assets/AssetSources.h"
#include "camera/EditorCameraController.h"
#include "gameplay/GameConfig.h"
#include "scene/Scene.h"
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

/// <summary>
/// Editorの未保存シーンをセッションごとに記録し、終了したセッションから復旧する
/// </summary>
class SceneRecovery {
  public:
	static constexpr float kIntervalSeconds = 30.0f;

	/// <summary>
	/// 復旧ファイルの一覧表示用データ
	/// </summary>
	struct Entry {
		std::filesystem::path path;
		std::filesystem::path sourceFile;
		std::filesystem::path assetDirectory;
		std::string capturedUtc;
		std::string error;

		/// <summary>
		/// 現在のプロジェクトで復旧できる候補か確認する。未接続の場合は読み取れる候補を許可する
		/// </summary>
		bool CanRestore(const std::filesystem::path &projectAssets) const;
	};

	/// <summary>
	/// 元ファイルと独立して読み込む復旧内容。シーンと制作時の設定をまとめて保持する
	/// </summary>
	struct Snapshot {
		Scene scene;
		AssetSources assets;
		GameConfig game;
		std::filesystem::path sourceFile;
		std::string capturedUtc;
		std::optional<EditorCameraController::ViewState> view;
		std::optional<std::vector<SceneObjectId>> selection;
	};

	SceneRecovery();
	~SceneRecovery();
	SceneRecovery(const SceneRecovery &) = delete;
	SceneRecovery &operator=(const SceneRecovery &) = delete;

	/// <summary>
	/// 専用の新規セッションを作り、他のEditorから復旧対象として扱われないようロックする
	/// </summary>
	/// <param name="directory">絶対パス、または実行ファイル基準の復旧フォルダ</param>
	/// <param name="error">失敗理由。失敗時は以前のセッションを保持する</param>
	/// <returns>true:初期化成功 false:作成・ロック失敗、または既に使用中</returns>
	bool Initialize(const std::filesystem::path &directory, std::string &error);

	/// <summary>
	/// 編集シーンとAssets・ゲーム設定を、元ファイルを変更せず復旧用ファイルへ置き換え保存する
	/// </summary>
	/// <param name="scene">読み取り専用の編集シーン</param>
	/// <param name="sourceFile">元の保存先の絶対パス。未保存は空</param>
	/// <param name="assets">制作時の素材の取得元</param>
	/// <param name="game">制作時のゲーム起動設定</param>
	/// <param name="error">失敗理由。保存失敗時は以前の復旧内容を保持する</param>
	/// <param name="view">記録時のSceneビューの視点。指定なしはnullptr</param>
	/// <param name="selection">記録時の選択順。空は未選択</param>
	/// <returns>true:記録成功 false:未初期化、無効なデータ、保存先との競合、または書き込み失敗</returns>
	bool Record(const Scene &scene, const std::filesystem::path &sourceFile,
	            const AssetSources &assets, const GameConfig &game,
	            std::string &error,
	            const EditorCameraController::ViewState *view = nullptr,
	            std::span<const SceneObjectId> selection = {});

	/// <summary>
	/// 他のEditorが使用していない復旧ファイルを、新しい順で取得する
	/// </summary>
	/// <param name="entries">成功時だけ置き換える候補一覧。破損ファイルはerrorを設定して含める</param>
	/// <param name="error">列挙自体の失敗理由</param>
	/// <returns>true:列挙成功 false:フォルダの読み取り失敗</returns>
	bool Enumerate(std::vector<Entry> &entries, std::string &error) const;

	/// <summary>
	/// 使用中でない候補の全内容を読み込む。既存のシーンファイルへは書き込まない
	/// </summary>
	/// <param name="path">同じ復旧フォルダの候補パス</param>
	/// <param name="snapshot">全項目の検証成功時だけ置き換える復旧内容</param>
	/// <param name="error">失敗理由</param>
	/// <returns>true:読み込み成功 false:使用中・対象外・破損・設定が無効</returns>
	bool Read(const std::filesystem::path &path, Snapshot &snapshot,
	          std::string &error) const;

	/// <summary>
	/// 使用中でない候補1件を削除する。破損ファイルも明示的な削除の対象にできる
	/// </summary>
	/// <param name="path">このフォルダの候補パス。他の場所や使用中のセッションは拒否する</param>
	/// <param name="error">失敗理由</param>
	/// <returns>true:削除成功、または既にない false:対象外・使用中・削除失敗</returns>
	bool Remove(const std::filesystem::path &path, std::string &error) const;

	/// <summary>
	/// 現在のセッションが書いた復旧ファイルだけを消去する
	/// </summary>
	/// <param name="protectedFile">削除しない編集中の保存先。空は未保存</param>
	/// <param name="error">失敗理由。既存ファイルとの競合時は保持する</param>
	/// <returns>true:消去成功、または記録なし false:競合・削除失敗</returns>
	bool Clear(const std::filesystem::path &protectedFile, std::string &error);

	/// <summary>
	/// セッションのロックを解放する。復旧ファイルは保持する
	/// </summary>
	void Release();

	/// <summary>
	/// 自動記録できるセッションを保持しているか確認する
	/// </summary>
	bool IsReady() const;

	/// <summary>
	/// このセッションの復旧先を取得する。作成前は空。解放後も最後の復旧先を保持する
	/// </summary>
	const std::filesystem::path &GetCurrentPath() const;

  private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};
