#include "panels/SceneViewPanel.h"

#include "graphics/debug/DebugUI.h"
#include "graphics/render/RenderSystem.h"
#include "graphics/render/RenderTexture.h"

#include <algorithm>
#include <cmath>

SceneViewPanel::SceneViewPanel(RenderSystem &render) : render_(render) {}

SceneViewPanel::~SceneViewPanel() = default;

bool SceneViewPanel::DrawUI(const DebugUI &ui) {
	visible_ = false;
	input_ = {};

	ui.SetNextWindowSize(800.0f, 500.0f);

	if (!ui.BeginWindow("Scene")) {
		ui.EndWindow();
		return true;
	}

	const auto size = ui.GetContentSize();

	if (!std::isfinite(size.x) || !std::isfinite(size.y) || size.x < 1.0f ||
	    size.y < 1.0f) {
		ui.EndWindow();
		return true;
	}

	// 極端なパネルサイズで巨大な描画先を作らないよう制限する
	const int width = static_cast<int>(std::clamp(size.x, 1.0f, 8192.0f));
	const int height = static_cast<int>(std::clamp(size.y, 1.0f, 8192.0f));

	if (!texture_) {
		texture_ = render_.CreateRenderTexture(width, height);

		if (!texture_) {
			ui.EndWindow();
			return false;
		}
	} else if (!texture_->Resize(width, height)) {
		ui.EndWindow();
		return false;
	}

	// リサイズを済ませてから、UIの画像として登録する
	ui.Image(*texture_, static_cast<float>(width), static_cast<float>(height));

	// 最後に表示した項目が画像である間に取得する
	input_ = ui.GetViewportInput();

	visible_ = true;

	ui.EndWindow();
	return true;
}

RenderTexture *SceneViewPanel::GetRenderTexture() const {
	return visible_ ? texture_.get() : nullptr;
}

const ViewportInput &SceneViewPanel::GetInput() const { return input_; }

void SceneViewPanel::Finalize() {
	texture_.reset();
	input_ = {};
	visible_ = false;
}
