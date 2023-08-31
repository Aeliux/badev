// Released under the MIT License. See LICENSE for details.

#include "ballistica/base/app_adapter/app_adapter.h"

#include "ballistica/base/graphics/graphics_server.h"
#include "ballistica/base/graphics/renderer/renderer.h"
#include "ballistica/base/input/input.h"
#include "ballistica/base/networking/network_reader.h"
#include "ballistica/base/networking/networking.h"
#include "ballistica/base/support/stress_test.h"
#include "ballistica/base/ui/ui.h"
#include "ballistica/shared/foundation/event_loop.h"
#include "ballistica/shared/python/python.h"

namespace ballistica::base {

AppAdapter::AppAdapter() = default;

AppAdapter::~AppAdapter() = default;

void AppAdapter::LogicThreadDoApplyAppConfig() {
  assert(g_base->InLogicThread());
}

auto AppAdapter::ManagesEventLoop() const -> bool {
  // We have 2 redundant values for essentially the same thing;
  // should get rid of IsEventPushMode() once we've created
  // App subclasses for our various platforms.
  return !g_core->platform->IsEventPushMode();
}

void AppAdapter::OnMainThreadStartApp() {
  assert(g_base);
  assert(g_core);
  assert(g_core->InMainThread());

  // Add some common input devices where applicable. More specific ones (SDL
  // Joysticks, etc.) get added in subclasses.

  // If we've got a nice themed hardware cursor, show it. Otherwise we'll
  // render it manually, which is laggier but gets the job done.
  g_core->platform->SetHardwareCursorVisible(g_buildconfig.hardware_cursor());

  if (!g_core->HeadlessMode()) {
    // On desktop systems we just assume keyboard input exists and add it
    // immediately.
    if (g_core->platform->IsRunningOnDesktop()) {
      g_base->input->PushCreateKeyboardInputDevices();
    }

    // On non-tv, non-desktop, non-vr systems, create a touchscreen input.
    if (!g_core->platform->IsRunningOnTV() && !g_core->IsVRMode()
        && !g_core->platform->IsRunningOnDesktop()) {
      g_base->input->CreateTouchInput();
    }
  }
}

void AppAdapter::DrawFrame(bool during_resize) {
  assert(g_base->InGraphicsThread());

  // It's possible to be asked to draw before we're ready.
  if (!g_base->graphics_server || !g_base->graphics_server->renderer()) {
    return;
  }

  millisecs_t starttime = g_core->GetAppTimeMillisecs();

  // A resize-draw event means that we're drawing due to a window resize.
  // In this case we ignore regular draw events for a short while
  // afterwards which makes resizing smoother.
  //
  // FIXME: should figure out the *correct* way to handle this; I believe
  //  the underlying cause here is some sort of context contention across
  //  threads.
  if (during_resize) {
    last_resize_draw_event_time_ = starttime;
  } else {
    if (starttime - last_resize_draw_event_time_ < (1000 / 30)) {
      return;
    }
  }
  g_base->graphics_server->TryRender();
  RunRenderUpkeepCycle();
}

void AppAdapter::RunRenderUpkeepCycle() {
  // This should only be firing if the OS is handling the event loop.
  assert(!ManagesEventLoop());

  // Pump the main event loop (when we're being driven by frame-draw
  // callbacks, this is the only place that gets done).
  g_core->main_event_loop()->RunSingleCycle();

  // Now do the general app event cycle for whoever needs to process things.
  // FIXME KILL THIS.
  RunEvents();
}

// FIXME KILL THIS.
void AppAdapter::RunEvents() {
  // There's probably a better place for this.
  g_base->stress_test()->Update();

  // Give platforms a chance to pump/handle their own events.
  //
  // FIXME: now that we have app class overrides, platform should really not
  // be doing event handling. (need to fix Rift build in this regard).
  g_core->platform->RunEvents();
}

void AppAdapter::UpdatePauseResume_() {
  if (app_paused_) {
    // Unpause if no one wants pause.
    if (!app_pause_requested_) {
      OnAppResume_();
      app_paused_ = false;
    }
  } else {
    // OnAppPause if anyone wants.
    if (app_pause_requested_) {
      OnAppPause_();
      app_paused_ = true;
    }
  }
}

void AppAdapter::OnAppPause_() {
  assert(g_core->InMainThread());

  // IMPORTANT: Any pause related stuff that event-loop-threads need to do
  // should be done from their registered pause-callbacks. If we instead
  // push runnables to them from here they may or may not be called before
  // their event-loop is actually paused.

  // Pause all event loops.
  EventLoop::SetEventLoopsPaused(true);

  if (g_base->network_reader) {
    g_base->network_reader->OnAppPause();
  }
  g_base->networking->OnAppPause();
}

void AppAdapter::OnAppResume_() {
  assert(g_core->InMainThread());
  last_app_resume_time_ = g_core->GetAppTimeMillisecs();

  // Spin all event-loops back up.
  EventLoop::SetEventLoopsPaused(false);

  // Run resumes that expect to happen in the main thread.
  g_base->network_reader->OnAppResume();
  g_base->networking->OnAppResume();

  // When resuming from a paused state, we may want to pause whatever game
  // was running when we last were active.
  //
  // TODO(efro): we should make this smarter so it doesn't happen if we're
  // in a network game or something that we can't pause; bringing up the
  // menu doesn't really accomplish anything there.
  if (g_core->should_pause) {
    g_core->should_pause = false;

    // If we've been completely backgrounded, send a menu-press command to
    // the game; this will bring up a pause menu if we're in the game/etc.
    if (!g_base->ui->MainMenuVisible()) {
      g_base->ui->PushMainMenuPressCall(nullptr);
    }
  }
}

void AppAdapter::PauseApp() {
  assert(g_core);
  assert(g_core->InMainThread());
  millisecs_t start_time{core::CorePlatform::GetCurrentMillisecs()};

  // Apple mentioned 5 seconds to run stuff once backgrounded or they bring
  // down the hammer. Let's aim to stay under 2.
  millisecs_t max_duration{2000};

  g_core->platform->DebugLog(
      "PauseApp@" + std::to_string(core::CorePlatform::GetCurrentMillisecs()));
  assert(!app_pause_requested_);
  app_pause_requested_ = true;
  UpdatePauseResume_();

  // We assume that the OS will completely suspend our process the moment we
  // return from this call (though this is not technically true on all
  // platforms). So we want to spin and wait for threads to actually process
  // the pause message.
  size_t running_thread_count{};
  while (std::abs(core::CorePlatform::GetCurrentMillisecs() - start_time)
         < max_duration) {
    // If/when we get to a point with no threads waiting to be paused, we're
    // good to go.
    auto threads{EventLoop::GetStillPausingThreads()};
    running_thread_count = threads.size();
    if (running_thread_count == 0) {
      if (g_buildconfig.debug_build()) {
        Log(LogLevel::kDebug,
            "PauseApp() completed in "
                + std::to_string(core::CorePlatform::GetCurrentMillisecs()
                                 - start_time)
                + "ms.");
      }
      return;
    }
  }

  // If we made it here, we timed out. Complain.
  Log(LogLevel::kError,
      std::string("PauseApp() took too long; ")
          + std::to_string(running_thread_count)
          + " threads not yet paused after "
          + std::to_string(core::CorePlatform::GetCurrentMillisecs()
                           - start_time)
          + " ms.");
}

void AppAdapter::ResumeApp() {
  assert(g_core && g_core->InMainThread());
  millisecs_t start_time{core::CorePlatform::GetCurrentMillisecs()};
  g_core->platform->DebugLog(
      "ResumeApp@" + std::to_string(core::CorePlatform::GetCurrentMillisecs()));
  assert(app_pause_requested_);
  app_pause_requested_ = false;
  UpdatePauseResume_();
  if (g_buildconfig.debug_build()) {
    Log(LogLevel::kDebug,
        "ResumeApp() completed in "
            + std::to_string(core::CorePlatform::GetCurrentMillisecs()
                             - start_time)
            + "ms.");
  }
}

void AppAdapter::DidFinishRenderingFrame(FrameDef* frame) {}

void AppAdapter::PrimeMainThreadEventPump() {
  assert(!ManagesEventLoop());

  // Need to release the GIL while we're doing this so other thread
  // can do their Python-y stuff.
  Python::ScopedInterpreterLockRelease release;

  // Pump events manually until a screen gets created.
  // At that point we use frame-draws to drive our event loop.
  while (!g_base->graphics_server->initial_screen_created()) {
    g_core->main_event_loop()->RunSingleCycle();
    core::CorePlatform::SleepMillisecs(1);
  }
}

}  // namespace ballistica::base
