#pragma once
#include "animation/AnimatorComponent.h"
#include "animation/AnimatorDrawPose.h"
#include "animation/AnimatorPlaybackState.h"
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <span>
class Scene;

/// <summary>
/// モデルの共有データと、物体ごとのクリップ・時計・評価姿勢を編集シーンから分離する
/// </summary>
class SceneAnimators {
  public:
	static constexpr std::size_t kMaximumModels = 128;
	static constexpr std::size_t kMaximumVertices = 4000000;
	static constexpr std::size_t kMaximumKeys = 2000000;
	using LoadModel = std::function<std::shared_ptr<const ModelAnimationData>(
	    const std::filesystem::path &, std::string &)>;
	enum class Operation { PLAY, CROSS_FADE, PAUSE, STOP, SEEK, SPEED, LOOP };

	/// <summary>
	/// 物体と宣言済みPrefabのモデル・クリップを検証し、成功後だけ再生状態を更新する
	/// </summary>
	/// <param name="scene">設定を読むシーン。値は変更しない</param>
	/// <param name="preserve">true:同じ設定の時計とLuaの指定を保持 false:初期化</param>
	/// <param name="error">失敗理由</param>
	/// <param name="invalidObject">指定時は問題の物体。Prefab内の不正は0</param>
	/// <param name="load">指定時は描画側と同じ共有データを取得する。空はCPU読み込み</param>
	/// <param name="prefabs">再生中の生成に備えて先に読み込むPrefab</param>
	/// <param name="allowLoad">falseは既に準備したモデルだけを使用し、ファイルを読み込まない</param>
	/// <returns>true:成功 false:設定・モデル・クリップ・上限が無効。以前の状態は保持する</returns>
	bool Configure(const Scene &scene, bool preserve, std::string &error,
	               SceneObjectId *invalidObject = nullptr,
	               const LoadModel &load = {},
	               std::span<const std::shared_ptr<const Scene>> prefabs = {},
	               bool allowLoad = true);

	/// <summary>
	/// 有効で再生中の時計だけを進め、候補の骨・ノード姿勢を評価する
	/// </summary>
	/// <param name="scene">同じ再生シーン</param>
	/// <param name="seconds">有限な0以上の秒数。1回の上限は0.1秒</param>
	/// <param name="error">失敗理由</param>
	/// <returns>true:成功 false:時間・対象・姿勢が無効。呼び出し側は候補を破棄する</returns>
	bool Advance(const Scene &scene, float seconds, std::string &error);

	/// <summary>
	/// 準備した候補だけで、クリップ・再生・停止・シーク・速度・ループを操作する
	/// </summary>
	/// <param name="scene">同じ候補の物体と有効状態</param>
	/// <param name="id">Animatorを持つ物体</param>
	/// <param name="operation">操作。STOPはモデルのバインド姿勢へ戻す</param>
	/// <param name="clip">PLAYまたはCROSS_FADEで選ぶ名前。空は現在のクリップを保持する</param>
	/// <param name="number">CROSS_FADEの補間秒数、SEEKの秒数、またはSPEEDの倍率</param>
	/// <param name="flag">PLAYの先頭から再生、またはLOOPの指定</param>
	/// <param name="error">失敗理由</param>
	/// <returns>true:成功 false:対象・引数・評価が無効。失敗時は状態を保持する</returns>
	bool Control(const Scene &scene, SceneObjectId id, Operation operation,
	             std::string_view clip, double number, bool flag,
	             std::string &error);

	/// <summary>
	/// 現在のクリップ・時刻と、実行時に指定した速度・ループ・再生状態を取得する
	/// </summary>
	/// <param name="id">再生シーンの物体</param>
	/// <returns>状態。対象なしはnullopt</returns>
	std::optional<AnimatorPlaybackState> GetState(SceneObjectId id) const;

	/// <summary>
	/// 有効で適用中の物体だけ、変更しないモデル・姿勢の共有参照を描画へ渡す
	/// </summary>
	/// <param name="scene">同じ再生シーン</param>
	/// <param name="poses">成功時だけ置き換える一覧</param>
	/// <param name="error">失敗理由</param>
	/// <returns>true:成功 false:階層・対象が無効</returns>
	bool Snapshot(const Scene &scene, std::vector<AnimatorDrawPose> &poses,
	              std::string &error) const;

  private:
	struct PathLess {
		bool operator()(const std::filesystem::path &a,
		                const std::filesystem::path &b) const;
	};

	struct Resource {
		std::shared_ptr<const ModelAnimationData> data;
		ModelAnimationSampler sampler;
		std::size_t vertices = 0, keys = 0;
	};

	struct State {
		AnimatorComponent settings;
		std::filesystem::path path;
		std::shared_ptr<const Resource> resource;
		std::shared_ptr<const ModelAnimationPose> pose;
		// 切替直前に表示していた姿勢を固定し、切替の途中からでも連続させる。
		std::shared_ptr<const ModelAnimationPose> fadeSource;
		double fadeDuration = 0, fadeElapsed = 0;
		std::size_t clip = 0;
		double cursor = 0;
		float speed = 1;
		bool loop = true, playing = false, applied = false,
		     pendingStart = false;
	};

	/// <summary>
	/// 現在のクリップと時刻を評価し、全成功後だけ姿勢の共有参照を置き換える
	/// </summary>
	/// <param name="state">評価する候補</param>
	/// <param name="error">失敗理由</param>
	/// <returns>true:成功 false:補間が無効</returns>
	static bool Apply(State &state, std::string &error);
	std::map<SceneObjectId, State> states_;
	std::map<std::filesystem::path, std::shared_ptr<const Resource>, PathLess>
	    models_;
};
