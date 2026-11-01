#include "graphics/render/RenderSystem.h"
#include "core/Logger.h"
#include "graphics/render/RenderTexture.h"
#include "platform/windows/WinApp.h"

#include <exception>
#include <format>

RenderSystem::RenderSystem(D3D12Graphics &graphics, MeshRenderer &renderer,
                           DebugLineRenderer &debugRenderer, const WinApp &app)
    : graphics_(graphics), renderer_(renderer), debugRenderer_(debugRenderer),
      app_(app) {}

std::unique_ptr<RenderTexture> RenderSystem::CreateRenderTexture(int width,
                                                                 int height) {
	try {
		auto target =
		    std::unique_ptr<RenderTexture>(new RenderTexture(graphics_));

		if (!target->Resize(width, height)) {
			return nullptr;
		}

		return target;
	} catch (const std::exception &error) {
		Logger::Error(std::format("描画先の作成中に例外が発生しました: {}",
		                          error.what()));
		return nullptr;
	}
}

bool RenderSystem::Update(RenderWorld &world, int width, int height) {
	if (!world.Uses(graphics_)) {
		Logger::Error("RenderSystem::Update: 描画基盤が異なります。");
		return false;
	}

	return world.Update(width, height);
}

void RenderSystem::Draw(const RenderWorld &world, RenderTexture &target,
                        const float (&clearColor)[4]) const {
	if (!world.Uses(graphics_) || !target.Uses(graphics_)) {
		Logger::Error("RenderSystem::Draw: 描画基盤が異なります。");
		return;
	}

	if (!target.Begin(clearColor)) {
		return;
	}

	world.Draw(renderer_, debugRenderer_);

	target.End();
}

std::unique_ptr<RenderWorld> RenderSystem::CreateWorld() {
	try {
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
	return Update(world, app_.GetWidth(), app_.GetHeight());
}

void RenderSystem::Draw(const RenderWorld &world) const {
	if (!world.Uses(graphics_)) {
		Logger::Error("RenderSystem::Draw: 描画基盤が異なります。");
		return;
	}

	world.Draw(renderer_, debugRenderer_);
}
