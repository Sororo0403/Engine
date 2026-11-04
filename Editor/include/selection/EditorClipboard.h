#pragma once

#include "scene/SceneObject.h"
#include "scene/SceneObjectId.h"

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <vector>

class Scene;

/// <summary>
/// コピーした物体・Transform・Box Collider・Cameraのレンズ設定・Character設定・追従設定・復帰設定・UVを、元のシーンと独立して保持する
/// </summary>
class EditorClipboard {
  public:
	/// <summary>
	/// 指定物体と全子孫を親から先にコピーする。範囲外の親はワールドSRTを保って解除する
	/// </summary>
	/// <param name="scene">コピー元のシーン</param>
	/// <param name="objectIds">重複のないコピー対象ID一覧</param>
	/// <returns>true:全件コピー成功 false:対象・関係・データが無効、または範囲外の親の解除にせん断が必要。失敗時は以前の内容を保持する</returns>
	bool Copy(const Scene &scene, std::span<const SceneObjectId> objectIds);

	/// <summary>
	/// コピーした物体データを取得する
	/// </summary>
	/// <returns>次のCopy成功またはClearまで有効なデータ一覧</returns>
	std::span<const SceneObject> GetObjects() const;

	/// <summary>
	/// コピーしたデータと同じ順序の複製元IDを取得する
	/// </summary>
	/// <returns>コピー範囲内の参照を付け替えるためのID一覧。次のCopy成功またはClearまで有効</returns>
	std::span<const SceneObjectId> GetSourceIds() const;

	/// <summary>
	/// 元のシーンの置き換えに合わせ、コピー範囲外の物体への参照を解除する。
	/// コピー範囲内の参照とTransform・Box Collider・レンズ設定・Character設定・追従設定・復帰設定のコピーは保持する
	/// </summary>
	void DetachExternalReferences() noexcept;

	/// <summary>
	/// 貼り付け可能な物体を保持しているか確認する
	/// </summary>
	/// <returns>true:データあり false:空</returns>
	bool HasObjects() const;

	/// <summary>
	/// Transformをコピーする。物体のコピー内容は変更しない
	/// </summary>
	/// <param name="transform">コピーする有効な変形情報</param>
	/// <returns>true:成功 false:値が無効。以前の内容は保持する</returns>
	bool CopyTransform(const Transform &transform);

	/// <summary>
	/// コピーしたTransformを取得する
	/// </summary>
	/// <returns>次のCopyTransform成功またはClearまで有効な値。未設定はnullptr</returns>
	const Transform *GetTransform() const;

	/// <summary>
	/// Box Colliderのローカル形状をコピーする。物体とTransformのコピーは変更しない
	/// </summary>
	/// <param name="collider">中心と半寸法が有効な衝突形状。元のTransformは保持しない</param>
	/// <returns>true:コピー成功 false:無効な形状。以前のコピー内容は保持する</returns>
	bool CopyCollider(const BoxCollider &collider);

	/// <summary>
	/// コピーしたBox Colliderのローカル形状を取得する
	/// </summary>
	/// <returns>次のCopyCollider成功またはClearまで有効な値。未設定はnullptr</returns>
	const BoxCollider *GetCollider() const;

	/// <summary>
	/// Cameraのレンズ設定をコピーする。Primary・姿勢・追従設定と、他のコピー内容は変更しない
	/// </summary>
	/// <param name="settings">有効な縦視野角とNear／Far Clip</param>
	/// <returns>true:コピー成功 false:設定が無効。以前の内容は保持する</returns>
	bool CopyCameraLens(const CameraSettings &settings);

	/// <summary>
	/// コピーしたCameraのレンズ設定を取得する
	/// </summary>
	/// <returns>次のCopyCameraLens成功またはClearまで有効な値。未設定はnullptr</returns>
	const CameraSettings *GetCameraLens() const;

	/// <summary>
	/// Character Controllerの設定をコピーする。他のコピー内容は変更しない
	/// </summary>
	/// <param name="settings">移動速度・ジャンプ速度・重力・終端速度・ジャンプ猶予の有効な設定</param>
	/// <returns>true:コピー成功 false:無効な設定。以前の内容は保持する</returns>
	bool CopyCharacterSettings(const CharacterSettings &settings);

	/// <summary>
	/// コピーしたCharacter Controllerの設定を取得する
	/// </summary>
	/// <returns>次のCopyCharacterSettings成功またはClearまで有効な値。未設定はnullptr</returns>
	const CharacterSettings *GetCharacterSettings() const;

	/// <summary>
	/// Follow Cameraのオフセットと平滑化設定をコピーする。追従対象は保存せず、他のコピー内容は変更しない
	/// </summary>
	/// <param name="settings">有効なカメラ位置・注視位置のオフセットと追従速度</param>
	/// <returns>true:コピー成功 false:無効な設定。以前の内容は保持する</returns>
	bool CopyFollowSettings(const FollowCameraSettings &settings);

	/// <summary>
	/// コピーしたFollow Cameraの設定を取得する。追従対象は常に未指定
	/// </summary>
	/// <returns>次のCopyFollowSettings成功またはClearまで有効な値。未設定はnullptr</returns>
	const FollowCameraSettings *GetFollowSettings() const;

	/// <summary>
	/// Respawnの設定をコピーする。Transformや他のコピー内容は変更しない
	/// </summary>
	/// <param name="settings">有効な落下高度・開始位置の使用設定・明示的な復帰位置</param>
	/// <returns>true:コピー成功 false:無効な設定。以前の内容は保持する</returns>
	bool CopyRespawnSettings(const RespawnSettings &settings);

	/// <summary>
	/// コピーしたRespawnの設定を取得する
	/// </summary>
	/// <returns>次のCopyRespawnSettings成功またはClearまで有効な値。未設定はnullptr</returns>
	const RespawnSettings *GetRespawnSettings() const;

	/// <summary>
	/// 画像の参照を含まない、物体ごとのUV調整
	/// </summary>
	struct TextureUVs {
		std::array<float, 2> scale{1.0f, 1.0f};
		std::array<float, 2> offset{0.0f, 0.0f};
	};

	/// <summary>
	/// UVの繰り返しと位置だけをコピーする。他のコピー内容は変更しない
	/// </summary>
	/// <param name="scale">有限な繰り返し倍率。0と負数も使用可能</param>
	/// <param name="offset">有限なUV位置</param>
	/// <returns>true:成功 false:値が無効。以前の内容は保持する</returns>
	bool CopyTextureUVs(const std::array<float, 2> &scale,
	                    const std::array<float, 2> &offset);

	/// <summary>
	/// コピーしたUVを取得する。シーンを切り替えても保持する
	/// </summary>
	/// <returns>次のCopyTextureUVs成功またはClearまで有効な値。未設定はnullptr</returns>
	const TextureUVs *GetTextureUVs() const;

	/// <summary>
	/// コピーしたデータを破棄する
	/// </summary>
	void Clear();

  private:
	std::vector<SceneObject> objects_;
	std::vector<SceneObjectId> sourceIds_;
	// シーンを置き換えた後に確保を行わず、外部参照だけを解除するための添字
	std::vector<std::size_t> externalReferences_;
	std::optional<Transform> transform_;
	std::optional<BoxCollider> collider_;
	std::optional<CameraSettings> cameraLens_;
	std::optional<CharacterSettings> characterSettings_;
	std::optional<FollowCameraSettings> followSettings_;
	std::optional<RespawnSettings> respawnSettings_;
	std::optional<TextureUVs> textureUVs_;
};
