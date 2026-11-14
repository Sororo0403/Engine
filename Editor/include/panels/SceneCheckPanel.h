#pragma once

#include "gameplay/SceneValidation.h"
#include "gameplay/GameActionBinding.h"
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>
class ImGuiSystem;
class Scene;

/// <summary>
/// 編集シーンの再生設定とアセット参照を手動検証し、結果と対象物体への選択・フレーム要求を表示する
/// </summary>
class SceneCheckPanel {
  public:
	/// <summary>
	/// 描画後に実行する、問題のある物体や素材を表示する要求
	/// </summary>
	struct Request {
		std::vector<SceneObjectId> objectIds;
		bool frame = false;
		bool inspect = false;
		std::filesystem::path assetPath;
		std::filesystem::path scriptPath;
		std::string scriptDiagnostic;
		std::filesystem::path scenePath;
		SceneObjectId destinationObjectId = 0;
		std::filesystem::path modelReloadPath;
		std::filesystem::path textureReloadPath;
		std::string inputAction;
	};

	/// <summary>
	/// パネルを開き、次の描画で現在のシーンを検証する
	/// </summary>
	void Show();

	/// <summary>
	/// パネルが開いていれば、フォーカスを移さずに再検査を予約する
	/// </summary>
	void RequestRecheckIfOpen();

	/// <summary>
	/// 検証結果とパネルの表示状態をクリアする
	/// </summary>
	void Reset();

	/// <summary>
	/// 前の結果を破棄し、開いたシーンの検索語を復元する。パネルが開いていれば再検査を予約する
	/// </summary>
	/// <param name="previous">切り替え前のシーン。未保存なら空</param>
	/// <param name="current">開いたシーンのパス</param>
	void OnSceneOpened(const std::filesystem::path &previous,
	                   const std::filesystem::path &current);

	/// <summary>
	/// 別名保存で現在の検索語を元のシーンと新しい保存先に記録する
	/// </summary>
	void OnSceneSavedAs(const std::filesystem::path &previous,
	                    const std::filesystem::path &current);

	/// <summary>
	/// 移動したシーンに検索語を引き継ぎ、移動後の参照を再検査する
	/// </summary>
	void OnAssetsMoved(
	    std::span<const std::pair<std::filesystem::path, std::filesystem::path>> moves);

	/// <summary>
	/// 現在の編集シーンの再生設定・モデル・材質・依存ファイルを検証し、全ての結果をまとめて置き換える
	/// </summary>
	/// <param name="scene">変更しない検証対象</param>
	/// <param name="fallback">Primary Cameraがない場合のSceneカメラ姿勢</param>
	/// <param name="bindings">現在のゲーム設定にある名前付き操作</param>
	void Check(const Scene &scene, const CameraPose &fallback,
	           std::span<const GameActionBinding> bindings = {});

	/// <summary>
	/// 最後の検証結果を、共有・記録用のUTF-8テキストとして取得する
	/// </summary>
	/// <param name="filteredScene">指定時は現在の検索に一致する指摘だけを含める</param>
	/// <returns>件数、メッセージ、関連する物体IDとパス。未実行は空、検証失敗時は失敗理由</returns>
	std::string GetReport(const Scene *filteredScene = nullptr) const;

	/// <summary>
	/// 最後の検証結果を検索して表示する。一括操作は表示中の結果だけを対象とし、シーンは変更しない
	/// </summary>
	/// <param name="ui">フレーム開始済みのUI</param>
	/// <param name="scene">検証対象と選択先を確認する編集シーン</param>
	/// <param name="fallback">現在のSceneカメラ姿勢</param>
	/// <param name="bindings">現在のゲーム設定にある名前付き操作</param>
	/// <param name="editStateId">現在の確定済み編集状態の識別子</param>
	/// <returns>選択する既存物体の一覧とフレーム・Inspector表示の要求。要求なしは空の一覧。Inspector表示は単独の物体への明示操作のみ</returns>
	Request Draw(const ImGuiSystem &ui, const Scene &scene,
	             const CameraPose &fallback,
	             std::span<const GameActionBinding> bindings = {},
	             std::uint64_t editStateId = 0);

  private:
	/// <summary>
	/// メッセージ・素材パス・現在の物体名・IDのいずれかが検索文字列を含むか調べる
	/// </summary>
	/// <param name="issue">最後の検証結果</param>
	/// <param name="scene">現在の物体名を確認するシーン</param>
	/// <returns>true:部分一致、または検索が空 false:一致しない。英字の大文字と小文字は区別する</returns>
	bool MatchesFilter(const SceneValidationIssue &issue,
	                   const Scene &scene,
	                   const std::filesystem::path &searchPath) const;

	/// <summary>
	/// 検索に一致する検証結果から、現在も存在する物体を重複せず列挙する
	/// </summary>
	/// <param name="scene">削除済みの物体を除くための現在のシーン</param>
	/// <returns>検証結果に現れる順の物体ID。物体を指さない問題は除く</returns>
	std::vector<SceneObjectId> GetIssueObjects(
	    const Scene &scene, const std::filesystem::path &searchPath) const;

	std::vector<SceneValidationIssue> issues_;
	std::vector<GameActionBinding> checkedBindings_;
	std::uint64_t checkedEditStateId_ = 0;
	std::optional<std::size_t> lastIssue_;
	std::optional<std::size_t> scrollToIssue_;
	std::string error_;
	std::string assetActionError_;
	std::string filter_;
	struct SceneCheckPosition {
		std::string filter;
		std::optional<SceneValidationIssue> issue;
	};
	std::map<std::filesystem::path, SceneCheckPosition> scenePositions_;
	std::optional<SceneValidationIssue> issueToRestore_;
	bool open_ = false;
	bool checkRequested_ = false;
	bool focusRequested_ = false;
	bool checked_ = false;
};
