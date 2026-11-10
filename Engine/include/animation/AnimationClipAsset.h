#pragma once

#include "animation/AnimationClipComponent.h"
#include <string>

namespace AnimationClipAsset {
/// <summary>
/// 拡張子が .clip.json のアニメーション素材か判定する
/// </summary>
bool IsPath(const std::filesystem::path &path);

/// <summary>
/// シーンから独立したクリップを読み書きする。トラックの対象は1から始まる割り当て枠
/// </summary>
bool Load(const std::filesystem::path &path, AnimationClipComponent &clip,
          std::string &error);
bool Save(const std::filesystem::path &path, const AnimationClipComponent &clip,
          std::string &error, bool overwrite = true);

/// <summary>
/// 素材の割り当て枠をシーンの物体IDへ解決した再生用クリップを作る
/// </summary>
bool Resolve(const AnimationClipComponent &player, AnimationClipComponent &clip,
             std::string &error);
} // namespace AnimationClipAsset
