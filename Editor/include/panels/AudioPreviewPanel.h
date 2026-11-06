#pragma once

#include "audio/AudioPreview.h"
#include <filesystem>
#include <string>

class ImGuiSystem;

/// <summary>
/// WAVの形式・長さと、シーンから独立した試聴操作を表示する
/// </summary>
class AudioPreviewPanel {
  public:
	/// <summary>
	/// 試聴の終了まで存続する音声管理と再生基盤を受け取る
	/// </summary>
	/// <param name="clips">共有するWAVキャッシュ</param>
	/// <param name="audio">初期化済みの再生基盤</param>
	AudioPreviewPanel(AudioClipManager &clips, AudioSystem &audio);

	/// <summary>
	/// WAVを読み直して確認画面を開く。別の音声なら現在の試聴を先に解除する
	/// </summary>
	/// <param name="path">実行ファイル基準または絶対パス。空は画面を閉じる</param>
	void Open(const std::filesystem::path &path);

	/// <summary>
	/// 音声情報・再読み込み・試聴・音量・終了を表示する。自動再生は行わない
	/// </summary>
	/// <param name="ui">フレーム開始済みのUI</param>
	void Draw(const ImGuiSystem &ui);

	/// <summary>
	/// Play開始前に自分の試聴だけを停止する。画面・読み込み済み音声・設定は保持する
	/// </summary>
	void Stop();

	/// <summary>
	/// 画面を閉じ、自分の試聴・音声・設定と表示要求を解除する
	/// </summary>
	void Clear();

  private:
	AudioPreview preview_;
	std::filesystem::path path_;
	std::string label_, error_;
	bool open_ = false, focusRequested_ = false, reloadRequested_ = false;
};
