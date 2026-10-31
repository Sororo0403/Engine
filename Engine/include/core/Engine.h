#pragma once

#include "scene/IScene.h"

#include <memory>

struct EngineContext;

/// <summary>
/// 共通機能の初期化・メインループ・終了処理を実行する
/// </summary>
class Engine {
  public:
	/// <summary>
	/// 最初のシーンを生成する関数
	/// </summary>
	/// <param name="context">初期化済みの共通機能</param>
	/// <returns>未初期化のシーン。所有権をエンジンへ渡す</returns>
	using SceneFactory =
	    std::unique_ptr<IScene> (*)(const EngineContext &context);

	/// <summary>
	/// エンジンを起動し、実行終了まで処理する
	/// </summary>
	/// <param name="sceneFactory">最初のシーンを生成する関数</param>
	/// <returns>0:正常終了 -1:異常終了</returns>
	static int Run(SceneFactory sceneFactory);

  private:
	struct Impl;
};
