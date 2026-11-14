#pragma once
#include "files/SceneRecovery.h"
#include <filesystem>
#include <string>
#include <vector>

class ImGuiSystem;

/// <summary>
/// 使用中でないシーンの復旧候補を選択し、復旧・保留・明示的な削除を行う
/// </summary>
class SceneRecoveryDialog {
  public:
	enum class Result { NONE, CLOSED, RESTORE };

	/// <summary>
	/// 復旧候補の列挙結果を保持し、選択画面の表示を予約する
	/// </summary>
	/// <param name="entries">使用中でない記録の一覧</param>
	/// <param name="projectAssets">現在のプロジェクトのAssetsフォルダ。未接続なら空</param>
	/// <param name="error">列挙失敗時に表示する理由。候補がなくても保留で閉じられる</param>
	void Show(std::vector<SceneRecovery::Entry> entries,
	          std::filesystem::path projectAssets,
	          std::string error = {});

	/// <summary>
	/// 復旧候補の選択画面を描画する。Escapeでは記録を消さずに閉じる
	/// </summary>
	/// <param name="ui">フレーム開始済みのUI</param>
	/// <param name="recovery">候補の削除を行う同じ復旧フォルダのセッション</param>
	/// <param name="protectedFile">削除を許可しない、現在編集中の保存先</param>
	/// <returns>画面を閉じた、または指定候補の復旧要求。実際の復旧は呼び出し側で行う</returns>
	Result Draw(const ImGuiSystem &ui, SceneRecovery &recovery,
	            const std::filesystem::path &protectedFile);

	/// <summary>
	/// RESTOREが返された候補のパスを取得する
	/// </summary>
	const std::filesystem::path &GetSelectedPath() const;

	/// <summary>
	/// 復旧候補と選択を保持したまま一覧を開き直す
	/// </summary>
	void Reopen(std::string error = {});

  private:
	/// <summary>
	/// 復旧できる候補を先頭に並べ、前の選択が残っていれば保持する
	/// </summary>
	void SetEntries(std::vector<SceneRecovery::Entry> entries);

	std::vector<SceneRecovery::Entry> entries_;
	std::filesystem::path projectAssets_;
	std::filesystem::path selectedPath_;
	std::string error_;
	std::size_t selected_ = 0;
	bool openRequested_ = false;
	bool confirmingDelete_ = false;
};
