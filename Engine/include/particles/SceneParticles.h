#pragma once
#include "particles/ParticleDrawBatch.h"
#include "particles/ParticleEmitterComponent.h"
#include "particles/ParticlePlaybackState.h"
#include "scene/SceneObjectId.h"
#include <map>
#include <optional>
#include <string>
class Scene;

/// <summary>
/// 発生元ごとの乱数・時計・粒子を、編集シーンから分離して更新する
/// </summary>
class SceneParticles {
  public:
	static constexpr std::size_t kMaximumTotalParticles = 32768;
	enum class Operation { PLAY, STOP, CLEAR, EMIT };

	/// <summary>
	/// 発生元を準備し、新しい有効な自動再生だけ初期の粒子を作る
	/// </summary>
	/// <param name="scene">最終姿勢を反映した候補シーン</param>
	/// <param name="preserve">true:既存の粒子と乱数を保持 false:すべて初期化する</param>
	/// <param name="error">失敗理由</param>
	/// <param name="invalidObject">指定時は問題の発生元</param>
	/// <returns>true:成功 false:設定・階層が無効。失敗時は以前の状態を保持する</returns>
	bool Configure(const Scene &scene, bool preserve, std::string &error,
	               SceneObjectId *invalidObject = nullptr);

	/// <summary>
	/// 有効な枝の粒子を短い区間で更新し、発生・重力・寿命を反映する
	/// </summary>
	/// <param name="scene">移動・物理・追従を反映した候補シーン</param>
	/// <param name="seconds">有限な0以上の秒数。0秒は保持し、1回の上限は0.1秒</param>
	/// <param name="error">失敗理由</param>
	/// <returns>true:成功 false:時間・階層・粒子が無効。呼び出し側は候補を破棄する</returns>
	bool Advance(const Scene &scene, float seconds, std::string &error);

	/// <summary>
	/// 候補の発生・停止・消去・任意個数の発生を操作する
	/// </summary>
	/// <param name="scene">同じ候補の物体と姿勢</param>
	/// <param name="id">Particle Emitterを持つ物体</param>
	/// <param name="operation">発生元への操作</param>
	/// <param name="count">EMITで作る数。0以上4096以下</param>
	/// <param name="restart">PLAYで時計・乱数・粒子を初期化するか</param>
	/// <param name="error">失敗理由</param>
	/// <returns>true:成功 false:対象・引数・姿勢が無効。失敗時は状態を保持する</returns>
	bool Control(const Scene &scene, SceneObjectId id, Operation operation,
	             int count, bool restart, std::string &error);

	/// <summary>
	/// 発生元の時計と生存数を取得する。無効な枝の時計と粒子は凍結する
	/// </summary>
	/// <param name="id">再生物体ID</param>
	/// <returns>状態。対象なしはnullopt</returns>
	std::optional<ParticlePlaybackState> GetState(SceneObjectId id) const;

	/// <summary>
	/// 有効な発生元の粒子をワールド座標へ変換し、描画用に複製する
	/// </summary>
	/// <param name="scene">同じ再生シーン</param>
	/// <param name="batches">成功時だけ置き換える描画データ</param>
	/// <param name="error">失敗理由</param>
	/// <returns>true:成功 false:階層・変換結果が無効</returns>
	bool Snapshot(const Scene &scene, std::vector<ParticleDrawBatch> &batches,
	              std::string &error) const;

  private:
	struct Particle {
		DirectX::XMFLOAT3 position{}, velocity{};
		float age = 0, lifetime = 1, scale = 1, rotation = 0;
	};

	struct State {
		ParticleEmitterComponent settings;
		std::vector<Particle> particles;
		double elapsed = 0, fraction = 0;
		std::uint32_t random = 1;
		bool emitting = false, pendingStart = false;
		DirectX::XMFLOAT3 lastOrigin{};
	};

	/// <summary>
	/// 再現可能な乱数を0以上1未満で取得する
	/// </summary>
	/// <param name="state">発生元の乱数状態</param>
	/// <returns>一様乱数</returns>
	static float Random(State &state);
	/// <summary>
	/// 上限内の粒子を作り、指定した残り時間だけ積分する
	/// </summary>
	/// <param name="state">発生先</param>
	/// <param name="matrix">現在の発生元のワールド行列</param>
	/// <param name="origin">移動を補間した発生元のワールド位置</param>
	/// <param name="count">発生数。容量を超える分は作らない</param>
	/// <param name="age">発生から区間末尾までの秒数</param>
	/// <param name="total">全発生元の生存数。作成した分だけ増やす</param>
	static void Emit(State &state, const DirectX::XMFLOAT4X4 &matrix,
	                 const DirectX::XMFLOAT3 &origin, int count, float age,
	                 std::size_t &total);
	/// <summary>
	/// 重力を反映し、粒子の位置・速度・寿命を進める
	/// </summary>
	/// <param name="particle">更新する粒子</param>
	/// <param name="settings">同じ発生元の設定</param>
	/// <param name="matrix">Local時に重力を変換するワールド行列</param>
	/// <param name="seconds">積分時間</param>
	static void Integrate(Particle &particle,
	                      const ParticleEmitterComponent &settings,
	                      const DirectX::XMFLOAT4X4 &matrix, float seconds);
	std::map<SceneObjectId, State> states_;
};
