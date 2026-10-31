#pragma once

class ModelManager;
class RenderSystem;
class InputSystem;
class DebugUI;

/// <summary>
/// アプリ側が使用できるエンジンの共通機能への参照
/// </summary>
struct EngineContext {
	ModelManager &models;
	RenderSystem &render;
	const InputSystem &input;
	DebugUI &debugUI;
};
