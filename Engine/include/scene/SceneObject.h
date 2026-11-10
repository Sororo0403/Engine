#pragma once
#include "scene/ButtonComponent.h"
#include "scene/CanvasComponent.h"
#include "scene/ImageComponent.h"
#include "scene/RectTransformComponent.h"
#include "scene/TextComponent.h"

#include "animation/AnimatorComponent.h"
#include "animation/TransformAnimationComponent.h"
#include "gameplay/CharacterSettings.h"
#include "gameplay/FollowCameraSettings.h"
#include "gameplay/MotionPathComponent.h"
#include "gameplay/RespawnSettings.h"
#include "gameplay/ScriptComponent.h"
#include "gameplay/TriggerActionsComponent.h"
#include "graphics/material/ColorTint.h"
#include "math/Transform.h"
#include "particles/ParticleEmitterComponent.h"
#include "physics/Collider.h"
#include "physics/RigidBodySettings.h"
#include "scene/AudioListenerComponent.h"
#include "scene/AudioSourceComponent.h"
#include "scene/CameraComponent.h"
#include "scene/MeshComponent.h"
#include "scene/MeshRendererComponent.h"
#include "scene/PointLightComponent.h"
#include "scene/PrefabLinkComponent.h"
#include "scene/SceneObjectId.h"
#include "scene/DirectionalLightComponent.h"
#include "scene/SpotLightComponent.h"
#include "animation/AnimationClipComponent.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

/// <summary>
/// シーン上の物体の名前・変形情報・描画と衝突の設定を保持する
/// </summary>
struct SceneObject {
	std::string name = "Object";
	std::string tag;
	// 自身の設定。実際の有効状態はSceneActivityで祖先と合わせて判定する
	bool active = true;
	// 親の変更にはSceneHierarchy::SetParentを使う。0はルートを表す
	SceneObjectId parentId = 0;
	// 親に対するローカル変形。ルートの場合はワールド変形と一致する
	Transform transform{};

	std::optional<MeshComponent> mesh;
	std::optional<MeshRendererComponent> meshRenderer;

	// 指定時は、モデルの全材質のRGBへこの色を掛ける
	std::optional<ColorTint> colorTint;

	// 指定した物体は衝突判定に使用する。未指定の場合は衝突形状を持たない
	std::optional<Collider> collider;
	// 重力や衝突で動く剛体。Box Colliderと組み合わせて使用する
	std::optional<RigidBodySettings> rigidBody;

	// 再生時に入力・重力で動かす物体。衝突形状も必要
	std::optional<CharacterSettings> character;
	// Transformを描画カメラとして使う設定
	std::optional<CameraComponent> camera;
	// カメラの追従動作。対象IDは同じシーンの物体を指定する
	std::optional<FollowCameraSettings> followCamera;
	// 落下時の復帰。Character Controllerを持つ物体に使用する
	std::optional<RespawnSettings> respawn;
	// 同じ物体のTriggerイベントで実行する対象物体の操作
	std::optional<TriggerActionsComponent> triggerActions;
	// 物体のワールド位置を光源として使用する
	std::optional<PointLightComponent> pointLight;
	// ワールド位置と親子合成した回転から円錐状に照らす
	std::optional<SpotLightComponent> spotLight;
	std::optional<AnimationClipComponent> animationClip;
	std::optional<DirectionalLightComponent> directionalLight;
	// 親子合成後の位置で再生する音源と、受信姿勢の指定
	std::optional<AudioSourceComponent> audioSource;
	std::optional<AudioListenerComponent> audioListener;
	// 保存・編集用のPrefab由来情報。ゲーム出力では通常の物体へ展開する
	std::optional<PrefabLinkComponent> prefabLink;
	// 開始位置を基準にしたローカル経路。Colliderがあれば移動床として使用する
	std::optional<MotionPathComponent> motionPath;
	// 開始時の姿勢を基準にした変形のキー。実行中の時計と開始姿勢はシミュレーションが保持する
	std::optional<TransformAnimationComponent> transformAnimation;
	// モデル内の骨・ノードのクリップを指定し、時計と描画姿勢は再生側に保持する
	std::optional<AnimatorComponent> animator;
	// 発生設定だけを保存し、生存する粒子・時計・乱数は再生シーンが保持する
	std::optional<ParticleEmitterComponent> particleEmitter;
	// Luaのコールバック・開始状態・物体参照。VMと実行中のstateはシミュレーション側で保持する
	std::optional<ScriptComponent> script;
	// 画面上のUI。3DのTransformとは独立した座標系でレイアウトする
	std::optional<CanvasComponent> canvas;
	std::optional<RectTransformComponent> rectTransform;
	std::optional<TextComponent> text;
	std::optional<ImageComponent> image;
	std::optional<ButtonComponent> button;

	/// <summary>
	/// 物体の分類タグを検証する。空は未設定として扱う
	/// </summary>
	/// <param name="tag">最大64バイトのUTF-8文字列</param>
	/// <returns>true:有効 false:長すぎる、NULを含む、またはUTF-8ではない</returns>
	static bool IsValidTag(std::string_view tag);

	/// <summary>
	/// Meshに形状の参照が割り当てられているか確認する
	/// </summary>
	/// <returns>true:参照あり false:コンポーネントなし、または未割り当て</returns>
	bool HasMesh() const {
		return mesh && !mesh->modelPath.empty();
	}

	/// <summary>
	/// Mesh Rendererで表示を無効にしているか確認する
	/// </summary>
	/// <returns>true:非表示 false:表示が有効、またはMesh Rendererなし</returns>
	bool IsHidden() const {
		return meshRenderer && !meshRenderer->visible;
	}

	/// <summary>
	/// MeshとMesh Rendererがそろい、形状を描画できる設定か確認する
	/// </summary>
	/// <returns>true:描画対象 false:コンポーネント・形状の未設定、または非表示</returns>
	bool IsRenderable() const {
		return HasMesh() && meshRenderer && meshRenderer->visible;
	}

	/// <summary>
	/// 変形と全コンポーネントの値が一致するか確認する
	/// </summary>
	/// <param name="other">比較対象</param>
	/// <returns>true:一致 false:不一致または無効な変形</returns>
	bool operator==(const SceneObject &other) const;

	/// <summary>
	/// Meshの描画設定と色を除き、シミュレーションに必要な設定が一致するか確認する
	/// </summary>
	bool HasSameSimulationSettings(const SceneObject &other) const;

	/// <summary>
	/// 既存の姿勢制御と競合せず、指定トラックの値を適用できるか確認する
	/// </summary>
	bool CanAnimate(const AnimationClipTrack &track) const;
};
