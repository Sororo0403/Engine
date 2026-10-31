#include "graphics/render/RenderSystem.h"
#include "core/Logger.h"

#include "platform/windows/WinApp.h"

#include <exception>
#include <format>

RenderSystem::RenderSystem(D3D12Graphics &graphics, MeshRenderer &renderer,
                           const WinApp &app)
    : graphics_(graphics), renderer_(renderer), app_(app) {}

std::unique_ptr<RenderWorld> RenderSystem::CreateWorld() {
	try {
		// RenderWorldのコンストラクターはprivateなので、
		// フレンドであるRenderSystemから直接生成する
		auto world = std::unique_ptr<RenderWorld>(new RenderWorld(graphics_));

		if (!world->Initialize()) {
			return nullptr;
		}

		return world;
	} catch (const std::exception &error) {
		Logger::Error(
		    std::format("RenderSystemのワールド作成中に例外が発生しました: {}",
		                error.what()));

		return nullptr;
	}
}

bool RenderSystem::Update(RenderWorld &world) {
	if (!world.Uses(graphics_)) {
		Logger::Error("RenderSystem::Update: 描画基盤が異なります。");
		return false;
	}

	return world.Update(app_.GetWidth(), app_.GetHeight());
}

void RenderSystem::Draw(const RenderWorld &world) const {
	if (!world.Uses(graphics_)) {
		Logger::Error("RenderSystem::Draw: 描画基盤が異なります。");
		return;
	}

	world.Draw(renderer_);
}
