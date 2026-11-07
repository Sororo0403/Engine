#pragma once

#include "physics/TriggerEvent.h"
#include "scene/SceneObjectId.h"
#include <DirectXMath.h>
#include <map>
#include <vector>
class Scene;
class CharacterController;
class TriggerWorld;

/// <summary>
/// 経路の実行位置を編集データから分離し、衝突で止まる移動床と乗客の移動を処理する
/// </summary>
class SceneMotion {
  public:
	/// <summary>
	/// 全経路の原点を準備する。継続時は現在位置の外部変更だけ原点へ加える
	/// </summary>
	/// <param name="scene">開始または有効状態を変更するシーン</param>
	/// <param name="preserve">true:経過時間を保持 false:開始時へ戻す</param>
	/// <returns>true:準備成功 false:有効な経路の設定が無効</returns>
	bool Configure(const Scene &scene, bool preserve);

	/// <summary>
	/// 120Hz以下の短い区間で経路を進め、衝突・乗客を確認して安全な候補だけ反映する
	/// </summary>
	/// <param name="scene">作業中の再生用シーン。失敗時は呼び出し側のトランザクションで破棄する</param>
	/// <param name="seconds">有限な0以上の秒数。1フレームは0.1秒を上限とする</param>
	/// <param name="controller">主人公が有効な場合のコントローラー。主人公なしはnullptr</param>
	/// <param name="character">有効な主人公のID。なしは0</param>
	/// <param name="triggers">指定時は各移動区間の接触を追跡する。eventsと同時に指定する</param>
	/// <param name="events">区間内のEnter/Exitを発生順に追加する。未指定は接触を追跡しない</param>
	/// <returns>true:更新または障害物で停止 false:設定・形状・階層が無効</returns>
	/// <remarks>固体は障害物や空中の主人公を押し潰さず停止する。Triggerと描画だけの物体は衝突で止まらない。無効な枝の時間は進めない</remarks>
	bool Advance(Scene &scene, float seconds, CharacterController *controller,
	             SceneObjectId character, TriggerWorld *triggers = nullptr,
	             std::vector<TriggerEvent> *events = nullptr);

	/// <summary>
	/// 経路の現在位置を保ったまま再生または一時停止する
	/// </summary>
	/// <param name="id">準備済みのMotion Pathを持つ物体</param>
	/// <param name="playing">true:再生 false:一時停止</param>
	/// <returns>true:成功 false:経路なし</returns>
	bool SetPlaying(SceneObjectId id, bool playing);

  private:
	struct State {
		DirectX::XMFLOAT3 origin{};
		double seconds = 0;
		bool playing = true;
	};

	std::map<SceneObjectId, State> states_;
	static constexpr double kMaximumStep = 1.0 / 120.0;
	static constexpr float kMaximumFrameSeconds = 0.1f;
};
