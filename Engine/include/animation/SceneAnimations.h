#pragma once
#include "animation/AnimationPlaybackState.h"
#include "animation/TransformAnimationComponent.h"
#include "scene/SceneObjectId.h"
#include <map>
#include <optional>
#include <string>
class Scene;

/// <summary>
/// 開始姿勢・再生時間・停止状態を編集データから分離し、物体ごとに保持する
/// </summary>
class SceneAnimations {
  public:
	enum class Operation { PLAY, PAUSE, STOP, SEEK };

	/// <summary>
	/// 物体の再生状態を準備し、新しい有効な自動再生だけ最初のキーを反映する
	/// </summary>
	/// <param name="scene">作業用シーン。呼び出し側が全処理の成功後に確定する</param>
	/// <param name="preserve">true:継続中の時計と外部からの姿勢変更を保持 false:初期化する</param>
	/// <param name="error">失敗理由</param>
	/// <param name="invalidObject">指定時は問題の物体</param>
	/// <returns>true:成功 false:設定・合成姿勢が無効。呼び出し側はシーンの候補を破棄する</returns>
	bool Configure(Scene &scene, bool preserve, std::string &error,
	               SceneObjectId *invalidObject = nullptr);

	/// <summary>
	/// 有効で再生中の時計だけを進め、現在のキーをローカル姿勢へ反映する
	/// </summary>
	/// <param name="scene">作業用の再生シーン</param>
	/// <param name="seconds">有限な0以上の秒数。0秒は変更せず、1回の上限は0.1秒</param>
	/// <param name="error">失敗理由</param>
	/// <returns>true:成功 false:設定・対象・合成姿勢が無効。呼び出し側は候補を破棄する</returns>
	bool Advance(Scene &scene, float seconds, std::string &error);

	/// <summary>
	/// 候補の再生状態と姿勢へ開始・一時停止・停止・シークを反映する
	/// </summary>
	/// <param name="scene">同じ候補の物体</param>
	/// <param name="id">アニメーションを持つ物体</param>
	/// <param name="operation">実行する操作。停止は開始姿勢へ戻す</param>
	/// <param name="seconds">シーク位置。0以上、クリップの長さ以下</param>
	/// <param name="restart">PLAYで先頭から開始するか。falseは現在位置から再開する</param>
	/// <param name="error">失敗理由</param>
	/// <returns>true:成功 false:対象・操作・時間・姿勢が無効。失敗時は変更しない</returns>
	bool Control(Scene &scene, SceneObjectId id, Operation operation,
	             double seconds, bool restart, std::string &error);

	/// <summary>
	/// 時計を進めず、クリップ内の位置と再生状態を取得する
	/// </summary>
	/// <param name="id">再生シーンの物体</param>
	/// <returns>状態。対象なしはnullopt。無効な枝の時計は停止しているが再生指定自体は保持する</returns>
	std::optional<AnimationPlaybackState> GetState(SceneObjectId id) const;

  private:
	struct State {
		TransformAnimationComponent settings;
		Transform base, last;
		double cursor = 0;
		bool playing = false, pendingStart = false, applied = false;
	};

	/// <summary>
	/// 現在位置の相対姿勢を開始姿勢へ合成し、有効な場合だけ最終姿勢を更新する
	/// </summary>
	/// <param name="state">候補の再生状態</param>
	/// <param name="transform">成功時だけ更新する姿勢</param>
	/// <returns>true:成功 false:補間・合成が無効</returns>
	static bool Apply(State &state, Transform &transform);
	/// <summary>
	/// スクリプトなどによる外部変更を、新しい開始姿勢へ反映する
	/// </summary>
	/// <param name="state">現在位置を保持する再生状態</param>
	/// <param name="transform">外部で変更されたローカル姿勢</param>
	/// <returns>true:成功 false:相対姿勢を逆算できない</returns>
	static bool Rebase(State &state, const Transform &transform);
	std::map<SceneObjectId, State> states_;
};
