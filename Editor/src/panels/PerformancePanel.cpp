#include "panels/PerformancePanel.h"

#include "graphics/ui/ImGuiSystem.h"

#include <algorithm>
#include <format>
#include <imgui.h>

void PerformancePanel::Show() {
	open_ = true;
	focusRequested_ = true;
}

void PerformancePanel::Reset() {
	timing_.Clear();
	open_ = false;
	focusRequested_ = false;
	paused_ = false;
	firstUpdate_ = true;
}

void PerformancePanel::Record(float deltaTime) {
	if (firstUpdate_) {
		firstUpdate_ = false;
		return;
	}

	if (!paused_) {
		timing_.AddSample(deltaTime);
	}
}

void PerformancePanel::Draw(const ImGuiSystem &ui, int width, int height) {
	if (!open_) {
		return;
	}

	ImGui::SetNextWindowSize({560.0f, 460.0f}, ImGuiCond_FirstUseEver);

	if (focusRequested_) {
		ImGui::SetNextWindowCollapsed(false);
		ImGui::SetNextWindowFocus();
		focusRequested_ = false;
	}

	if (!ImGui::Begin("Performance", &open_)) {
		ImGui::End();
		return;
	}

	ui.Checkbox("Pause timing", paused_);

	const auto summary = timing_.GetSummary();
	ui.Text(std::format("Samples: {} / {} ({:.2f} s)", summary.sampleCount,
	                    FrameTimingHistory::kCapacity,
	                    summary.durationSeconds));

	if (summary.sampleCount > 0) {
		ui.Text(std::format("FPS: {:.1f}    Mean: {:.2f} ms",
		                    summary.framesPerSecond, summary.meanMilliseconds));
		ui.Text(std::format(
		    "Min: {:.2f} ms    P95: {:.2f} ms    Max: {:.2f} ms",
		    summary.minimumMilliseconds, summary.percentile95Milliseconds,
		    summary.maximumMilliseconds));
		const auto maximum =
		    std::clamp(summary.maximumMilliseconds * 1.1,
		               kMinimumGraphMilliseconds, kMaximumGraphMilliseconds);
		ImGui::PlotLines(
		    "##FrameIntervals",
		    [](void *data, int index) {
			    const auto &timing = *static_cast<FrameTimingHistory *>(data);
			    return static_cast<float>(
			        (std::min)(kMaximumGraphMilliseconds,
			                   static_cast<double>(timing.GetSampleSeconds(
			                       static_cast<std::size_t>(index))) *
			                       1000.0));
		    },
		    &timing_, static_cast<int>(summary.sampleCount), 0, nullptr, 0.0f,
		    static_cast<float>(maximum), {0.0f, kGraphHeight});
		ui.Text(std::format("Intervals >= 1 s: {} (included in statistics)",
		                    summary.longIntervalCount));
	} else {
		ui.Text(paused_ ? "Timing is paused. Resume to record frames."
		                : "Waiting for frame samples...");
	}

	ImGui::TextWrapped("Application frame interval includes UI, waits and "
	                   "presentation. Long intervals can include loading, "
	                   "pauses or stalls. Graph display is capped at 250 ms.");
	ImGui::Separator();

	if (width > 0 && height > 0) {
		ui.Text(std::format("Scene target: {} x {}", width, height));
	} else {
		ui.Text("Scene target: not displayed");
	}

	ImGui::End();
}
