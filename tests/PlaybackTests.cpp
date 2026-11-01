#include "gameplay/PlaybackClock.h"
#include "gameplay/SampleStage.h"
#include "gameplay/SceneSimulation.h"
#include "math/TransformUtils.h"
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void Require(bool value, const char *message) {
	if (!value) {
		throw std::runtime_error(message);
	}
}
} // namespace
void RunPlaybackTests() {
	PlaybackClock clock;
	Require(!clock.IsPaused() && clock.Advance(0.025f) == 0.025f,
	        "normal playback clock");
	clock.RequestStep();
	Require(clock.Advance(0.01f) == 0.01f, "step while running changed time");
	clock.TogglePause();
	Require(clock.IsPaused() && !clock.Advance(1), "pause advanced game time");
	clock.RequestStep();
	clock.RequestStep();
	Require(!clock.Advance(std::numeric_limits<float>::quiet_NaN()),
	        "invalid time accepted");
	Require(clock.Advance(10) == PlaybackClock::kStepSeconds &&
	            clock.IsPaused() && !clock.Advance(10),
	        "step did not consume exactly once or unpaused clock");
	clock.RequestStep();
	clock.TogglePause();
	Require(clock.Advance(.02f) == .02f, "resume retained pending step");
	clock.TogglePause();
	Require(!clock.Advance(0), "resume left pending step");
	clock.RequestStep();
	clock.Reset();
	Require(!clock.IsPaused() && clock.Advance(.03f) == .03f,
	        "restart retained pause/step");

	Scene source;
	source.ReplaceObjects(SampleStage::Create());
	SceneSimulation play;
	std::string error;
	Require(play.Start(source, error), "playback integration startup");
	clock.TogglePause();
	const auto before = play.GetScene().FindObject(5)->transform;
	for (int i = 0; i < 100; ++i) {
		const auto time = clock.Advance(.1f);
		if (time) {
			Require(play.Update(*time, {1, 0, false}), "paused update");
		}
	}
	Require(TransformUtils::AreEqual(before,
	                                 play.GetScene().FindObject(5)->transform),
	        "paused scene moved");
	clock.RequestStep();
	const auto step = clock.Advance(5);
	Require(step && play.Update(*step, {1, 0, false}),
	        "one-frame update failed");
	const auto after = play.GetScene().FindObject(5)->transform;
	Require(after.translation.x > before.translation.x &&
	            after.translation.x < before.translation.x + .1f &&
	            after.translation.y < before.translation.y && !clock.Advance(5),
	        "step advanced more than one game frame");
	Require(TransformUtils::AreEqual(source.FindObject(5)->transform, before),
	        "stepping changed editing scene");
	clock.TogglePause();
	const auto resumed = clock.Advance(.016f);
	Require(resumed && play.Update(*resumed, {1, 0, false}) &&
	            play.GetScene().FindObject(5)->transform.translation.x >
	                after.translation.x,
	        "resume did not continue");
	std::cout << "PASS: pause/resume, single-frame time, coalesced/invalid "
	             "requests, restart reset and scene-editing isolation\n";
}
