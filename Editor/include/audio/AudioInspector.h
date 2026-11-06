#pragma once

#include "scene/SceneObjectId.h"
#include <filesystem>
#include <string>

class AssetDatabase;
class EditorHistory;
class ImGuiSystem;
struct SceneObject;

/// <summary>
/// 音源とリスナーの設定・参照・削除を、編集履歴とともに管理する
/// </summary>
class AudioInspector {
  public:
	/// <summary>
	/// WAV選択・ドロップ・音量・減衰距離・再生設定と、リスナーの削除を表示する
	/// </summary>
	/// <param name="ui">フレーム開始済みのUI</param>
	/// <param name="id">対象物体のID</param>
	/// <param name="object">編集する物体</param>
	/// <param name="history">連続・個別の変更を記録する履歴</param>
	/// <param name="assets">WAVの選択候補。未指定時もドロップと解除は受け付ける</param>
	/// <returns>true:変更あり false:対象なし・変更なし・無効な入力</returns>
	bool Draw(const ImGuiSystem &ui, SceneObjectId id, SceneObject &object,
	          EditorHistory &history, const AssetDatabase *assets = nullptr);

	/// <summary>
	/// Play中の音源・リスナー設定を変更せず表示する
	/// </summary>
	/// <param name="ui">フレーム開始済みのUI</param>
	/// <param name="object">表示する再生用の物体</param>
	void DrawReadOnly(const ImGuiSystem &ui, const SceneObject &object) const;

	/// <summary>
	/// 直前の描画でAssetsへの表示を要求した音声を取得する
	/// </summary>
	/// <returns>要求したパス。要求なしは空。次のDrawまたはClearまで有効</returns>
	const std::filesystem::path &GetRevealRequest() const;

	/// <summary>
	/// 選択に紐付けたエラーと表示要求を解除する
	/// </summary>
	void Clear();

  private:
	SceneObjectId objectId_ = 0;
	std::filesystem::path reveal_;
	std::string error_;
};
