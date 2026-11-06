#pragma once
#include "scripting/ScriptMessage.h"
#include <string_view>
#include <vector>
class Scene;

/// <summary>
/// 再生シーンのLua通知を制限付きで保持し、前回の通知だけを更新ごとに取り出す
/// </summary>
class ScriptMailbox {
  public:
	static constexpr std::size_t kMaximumPending = 128;
	static constexpr std::size_t kMaximumFrameMessages = 128;
	static constexpr std::size_t kMaximumBytes = 64 * 1024;

	/// <summary>
	/// Lua APIを使わず通知を検証・複製する。失敗時はキューと使用量を保持する
	/// </summary>
	/// <param name="receiver">存在を呼び出し側で確認した受信者ID</param>
	/// <param name="sender">送信者ID。0はシステムからの通知</param>
	/// <param name="name">1から64バイトまでのUTF-8名</param>
	/// <param name="data">4 KiBまでのJSONオブジェクト。Luaのstateと同じ値だけを許可する</param>
	/// <param name="error">成功時は空、失敗時は理由</param>
	/// <returns>true:追加成功 false:内容・件数・容量が無効</returns>
	bool Enqueue(SceneObjectId receiver, SceneObjectId sender,
	             std::string_view name, std::string_view data,
	             std::string &error);

	/// <summary>
	/// 送信回数を初期化し、正の時間では前回までの通知を取り出す
	/// </summary>
	/// <param name="deliver">true:前回分を返す false:開始処理なので保持する</param>
	/// <returns>今回だけ配信する通知。新しく送った通知は含めない</returns>
	std::vector<ScriptMessage> BeginStep(bool deliver);

	/// <summary>
	/// 削除済みの受信者への通知だけを破棄する。送信者の削除では破棄しない
	/// </summary>
	/// <param name="scene">削除反映後の候補シーン</param>
	void RemoveMissingReceivers(const Scene &scene);

	/// <summary>
	/// 保持中の件数を取得する
	/// </summary>
	/// <returns>次の更新を待つ通知の数</returns>
	std::size_t GetPendingCount() const {
		return pending_.size();
	}

	/// <summary>
	/// 再生開始・停止・シーン遷移で通知と使用量を初期化する
	/// </summary>
	void Clear();

  private:
	std::vector<ScriptMessage> pending_;
	std::size_t bytes_ = 0, sent_ = 0;
};
