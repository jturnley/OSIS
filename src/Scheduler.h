#pragma once

// A 20 Hz heartbeat on the game's main thread (queued as SKSE tasks from a background
// thread, like Softbody Arousal's updater) plus simple delayed tasks for the UI tests.
namespace Scheduler
{
	void Start();
	void After(float a_seconds, std::function<void()> a_fn);  // main thread, real time
}
