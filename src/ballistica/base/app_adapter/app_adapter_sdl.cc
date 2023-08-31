// Released under the MIT License. See LICENSE for details.

#if BA_SDL_BUILD

#include "ballistica/base/app_adapter/app_adapter_sdl.h"

#include "ballistica/base/dynamics/bg/bg_dynamics.h"
#include "ballistica/base/graphics/gl/gl_sys.h"
#include "ballistica/base/graphics/graphics_server.h"
#include "ballistica/base/input/device/joystick_input.h"
#include "ballistica/base/input/input.h"
#include "ballistica/base/logic/logic.h"
#include "ballistica/base/python/base_python.h"
#include "ballistica/base/support/stress_test.h"
#include "ballistica/base/ui/ui.h"
#include "ballistica/core/platform/core_platform.h"
#include "ballistica/shared/foundation/event_loop.h"
#include "ballistica/shared/python/python.h"

namespace ballistica::base {

void AppAdapterSDL::HandleSDLEvent(const SDL_Event& event) {
  assert(g_core->InMainThread());

  switch (event.type) {
    case SDL_JOYAXISMOTION:
    case SDL_JOYBUTTONDOWN:
    case SDL_JOYBUTTONUP:
    case SDL_JOYBALLMOTION:
    case SDL_JOYHATMOTION: {
      // It seems that joystick connection/disconnection callbacks can fire
      // while there are still events for that joystick in the queue. So
      // take care to ignore events for no-longer-existing joysticks.
      assert(event.jaxis.which == event.jbutton.which
             && event.jaxis.which == event.jhat.which);
      if (static_cast<size_t>(event.jbutton.which) >= sdl_joysticks_.size()
          || sdl_joysticks_[event.jbutton.which] == nullptr) {
        return;
      }
      JoystickInput* js = GetSDLJoystickInput_(&event);
      if (js) {
        if (g_base) {
          g_base->input->PushJoystickEvent(event, js);
        }
      } else {
        Log(LogLevel::kError, "Unable to get SDL Joystick for event type "
                                  + std::to_string(event.type));
      }
      break;
    }

    case SDL_MOUSEBUTTONDOWN: {
      const SDL_MouseButtonEvent* e = &event.button;

      // Convert sdl's coords to normalized view coords.
      float x = static_cast<float>(e->x) / screen_dimensions_.x;
      float y = 1.0f - static_cast<float>(e->y) / screen_dimensions_.y;
      if (g_base) {
        g_base->input->PushMouseDownEvent(e->button, Vector2f(x, y));
      }
      break;
    }

    case SDL_MOUSEBUTTONUP: {
      const SDL_MouseButtonEvent* e = &event.button;

      // Convert sdl's coords to normalized view coords.
      float x = static_cast<float>(e->x) / screen_dimensions_.x;
      float y = 1.0f - static_cast<float>(e->y) / screen_dimensions_.y;
      if (g_base) {
        g_base->input->PushMouseUpEvent(e->button, Vector2f(x, y));
      }
      break;
    }

    case SDL_MOUSEMOTION: {
      const SDL_MouseMotionEvent* e = &event.motion;

      // Convert sdl's coords to normalized view coords.
      float x = static_cast<float>(e->x) / screen_dimensions_.x;
      float y = 1.0f - static_cast<float>(e->y) / screen_dimensions_.y;
      if (g_base) {
        g_base->input->PushMouseMotionEvent(Vector2f(x, y));
      }
      break;
    }

    case SDL_KEYDOWN: {
      if (g_base) {
        g_base->input->PushKeyPressEvent(event.key.keysym);
      }
      break;
    }

    case SDL_KEYUP: {
      if (g_base) {
        g_base->input->PushKeyReleaseEvent(event.key.keysym);
      }
      break;
    }

#if BA_SDL2_BUILD || BA_MINSDL_BUILD
    case SDL_MOUSEWHEEL: {
      const SDL_MouseWheelEvent* e = &event.wheel;

      // Seems in general scrolling is a lot faster on mac SDL compared to
      // windows/linux. (maybe its just for trackpads/etc..).. so lets
      // compensate.
      int scroll_speed;
      if (g_buildconfig.ostype_android()) {
        scroll_speed = 1;
      } else if (g_buildconfig.ostype_macos()) {
        scroll_speed = 500;
      } else {
        scroll_speed = 500;
      }
      if (g_base) {
        g_base->input->PushMouseScrollEvent(
            Vector2f(static_cast<float>(e->x * scroll_speed),
                     static_cast<float>(e->y * scroll_speed)));
      }
      break;
    }
#endif  // BA_SDL2_BUILD

#if BA_OSTYPE_MACOS && BA_XCODE_BUILD
    case SDL_SMOOTHSCROLLEVENT: {
      const SDL_SmoothScrollEvent* e = &event.scroll;
      if (g_base) {
        g_base->input->PushSmoothMouseScrollEvent(
            Vector2f(0.2f * e->deltaX, -0.2f * e->deltaY), e->momentum);
      }
      break;
    }
#endif

    // Currently used in our some of our heavily customized builds. Should
    // replace this with some sort of PushDrawEvent() thing.
#if BA_XCODE_BUILD
    case SDL_RESIZEDRAWEVENT:
    case SDL_DRAWEVENT: {
      DrawFrame(event.type == SDL_RESIZEDRAWEVENT);
      break;
    }
#endif  // BA_OSTYPE_MACOS || BA_OSTYPE_ANDROID

    // Is there a reason we need to ignore these on ios?
    // do they even happen there?
    //
    // UPDATE: I think the even types are just not defined on our old iOS
    // SDL.
#if BA_SDL2_BUILD && !BA_OSTYPE_IOS_TVOS && BA_ENABLE_SDL_JOYSTICKS
    case SDL_JOYDEVICEREMOVED:
      // In this case we're passed the instance-id of the joystick.
      SDLJoystickDisconnected_(event.jdevice.which);
      break;
    case SDL_JOYDEVICEADDED:
      SDLJoystickConnected_(event.jdevice.which);
      break;
#endif

    case SDL_QUIT:
      // g_base->logic->event_loop()->PushCall([] { g_base->logic->Shutdown();
      // });
      g_base->logic->event_loop()->PushCall([] { g_base->ui->ConfirmQuit(); });
      break;

#if BA_OSTYPE_MACOS && BA_XCODE_BUILD && !BA_HEADLESS_BUILD
    case SDL_FULLSCREENSWITCH:
      // Our custom hacked-up SDL informs *us* when our window enters or
      // exits fullscreen. Let's commit this to our config so that we're in
      // sync..
      g_base->python->objs().PushCall(
          event.user.code ? BasePython::ObjID::kSetConfigFullscreenOnCall
                          : BasePython::ObjID::kSetConfigFullscreenOffCall);
      g_base->graphics_server->set_fullscreen_enabled(event.user.code);
      break;
#endif

#if BA_SDL2_BUILD

    case SDL_TEXTINPUT: {
      if (g_base) {
        g_base->input->PushTextInputEvent(event.text.text);
      }
      break;
    }

    case SDL_WINDOWEVENT: {
      switch (event.window.event) {
        case SDL_WINDOWEVENT_MINIMIZED:  // NOLINT(bugprone-branch-clone)

          // Hmm do we want to pause the app on desktop when minimized?
          // Gonna say no for now.
#if BA_OSTYPE_IOS_TVOS
          PauseApp();
#endif
          break;

        case SDL_WINDOWEVENT_RESTORED:
#if BA_OSTYPE_IOS_TVOS
          ResumeApp();
#endif
          break;

        case SDL_WINDOWEVENT_RESIZED:
        case SDL_WINDOWEVENT_SIZE_CHANGED: {
#if BA_OSTYPE_IOS_TVOS
          // Do nothing here currently.
#else   // Generic SDL:
          int pixels_x, pixels_y;
          SDL_GL_GetDrawableSize(
              g_base->graphics_server->gl_context()->sdl_window(), &pixels_x,
              &pixels_y);

          // Pixel density is number of pixels divided by window dimension.
          screen_dimensions_ = Vector2f(event.window.data1, event.window.data2);
          g_base->graphics_server->SetScreenResolution(
              static_cast<float>(pixels_x), static_cast<float>(pixels_y));
#endif  // BA_OSTYPE_IOS_TVOS

          break;
        }
        default:
          break;
      }
      break;
      default:
        break;
    }
#else   // BA_SDL2_BUILD
    case SDL_VIDEORESIZE: {
      screen_dimensions_ = Vector2f(event.resize.w, event.resize.h);
      g_base->graphics_server->SetScreenResolution(event.resize.w,
                                                   event.resize.h);
      break;
    }
#endif  // BA_SDL2_BUILD
  }
}

auto FilterSDLEvent(const SDL_Event* event) -> int {
  try {
    // If this event is coming from this thread, handle it immediately.
    if (std::this_thread::get_id() == g_core->main_thread_id) {
      auto app = static_cast_check_type<AppAdapterSDL*>(g_base->app_adapter);
      assert(app);
      if (app) {
        app->HandleSDLEvent(*event);
      }
      return false;  // We handled it; sdl doesn't need to keep it.
    } else {
      // Otherwise just let SDL post it to the normal queue.. we process
      // this every now and then to pick these up.
      return true;  // sdl should keep this.
    }
  } catch (const std::exception& e) {
    BA_LOG_ONCE(LogLevel::kError,
                std::string("Error in inline SDL-Event handling: ") + e.what());
    throw;
  }
}

#if BA_SDL2_BUILD
inline auto FilterSDL2Event(void* user_data, SDL_Event* event) -> int {
  return FilterSDLEvent(event);
}
#endif

// Note: can move this to AppAdapterSDL::AppAdapterSDL() once it is no longer
// needed by the legacy mac build.
void AppAdapterSDL::InitSDL() {
  assert(g_core);

  if (g_buildconfig.ostype_macos()) {
    // We don't want sdl to translate command/option clicks to different
    // mouse buttons dernit.
    g_core->platform->SetEnv("SDL_HAS3BUTTONMOUSE", "1");
  }

  // Let's turn on extra GL debugging on linux debug builds.
  if (g_buildconfig.ostype_linux() && g_buildconfig.debug_build()) {
    g_core->platform->SetEnv("MESA_DEBUG", "true");
  }

  uint32_t sdl_flags{};

  // We can skip joysticks and video for headless.
  if (!g_buildconfig.headless_build()) {
    sdl_flags |= SDL_INIT_VIDEO;
    if (explicit_bool(true)) {
      sdl_flags |= SDL_INIT_JOYSTICK;

      // KILL THIS ONCE MAC SDL1.2 BUILD IS DEAD.
      // Register our hotplug callbacks in our funky custom mac build.
#if BA_OSTYPE_MACOS && BA_XCODE_BUILD && !BA_HEADLESS_BUILD
      SDL_JoystickSetHotPlugCallbacks(AppAdapterSDL::SDLJoystickConnected_,
                                      AppAdapterSDL::SDLJoystickDisconnected_);
#endif
    }
  }

  // Whatever fancy-pants stuff SDL is trying to do with catching
  // signals/etc, we don't want it.
  sdl_flags |= SDL_INIT_NOPARACHUTE;

  // We may or may not want xinput on windows.
  if (g_buildconfig.ostype_windows()) {
    if (!g_core->platform->GetLowLevelConfigValue("enablexinput", 1)) {
      SDL_SetHint(SDL_HINT_XINPUT_ENABLED, "0");
    }
  }

  int result = SDL_Init(sdl_flags);
  if (result < 0) {
    throw Exception(std::string("SDL_Init failed: ") + SDL_GetError());
  }

  // KILL THIS ONCE SDL IS NO LONGER USED ON IOS BUILD
  if (g_buildconfig.ostype_ios_tvos() || g_buildconfig.ostype_android()) {
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
  }

  // KILL THIS ONCE MAC SDL 1.2 BUILD IS DEAD
#if !BA_SDL2_BUILD
  SDL_EnableUNICODE(true);
  SDL_EnableKeyRepeat(200, 50);
#endif
}

AppAdapterSDL::AppAdapterSDL() {
  InitSDL();

  // If we're not running our own even loop, we set up a filter to intercept
  // SDL events the moment they're generated and we process them
  // immediately. This way we don't have to poll for events and can be
  // purely callback-based, which fits in nicely with most modern event
  // models.
  if (!ManagesEventLoop()) {
#if BA_SDL2_BUILD
    SDL_SetEventFilter(FilterSDL2Event, nullptr);
#else
    SDL_SetEventFilter(FilterSDLEvent);
#endif  // BA_SDL2_BUILD
  } else {
    // Otherwise we do the standard old SDL polling stuff.

    // Set up a timer to chew through events every now and then. Polling
    // isn't super elegant, but is necessary in SDL's case. (SDLWaitEvent()
    // itself is pretty much a loop with SDL_PollEvents() followed by
    // SDL_Delay(10) until something is returned; In spirit, we're pretty
    // much doing that same thing, except that we're free to handle other
    // matters concurrently instead of being locked in a delay call.
    g_core->main_event_loop()->NewTimer(10, true, NewLambdaRunnable([this] {
                                          assert(g_base->app_adapter);
                                          g_base->app_adapter->RunEvents();
                                        }));
  }
}

void AppAdapterSDL::OnMainThreadStartApp() {
  AppAdapter::OnMainThreadStartApp();

  if (!g_core->HeadlessMode() && g_buildconfig.enable_sdl_joysticks()) {
    // Add initial sdl joysticks. any added/removed after this will be
    // handled via events. (it seems (on mac at least) even the initial ones
    // are handled via events, so lets make sure we handle redundant
    // joystick connections gracefully.
    if (explicit_bool(true)) {
      int joystick_count = SDL_NumJoysticks();
      for (int i = 0; i < joystick_count; i++) {
        AppAdapterSDL::SDLJoystickConnected_(i);
      }

      // We want events from joysticks.
      SDL_JoystickEventState(SDL_ENABLE);
    }
  }
}

void AppAdapterSDL::RunEvents() {
  AppAdapter::RunEvents();

  // Now run all pending SDL events until we run out or we're told to quit.
  SDL_Event event;
  while (SDL_PollEvent(&event) && (!g_core->main_event_loop()->done())) {
    HandleSDLEvent(event);
  }
}

void AppAdapterSDL::DidFinishRenderingFrame(FrameDef* frame) {
  AppAdapter::DidFinishRenderingFrame(frame);
  SwapBuffers_();
}

void AppAdapterSDL::DoSwap_() {
  assert(g_base->InGraphicsThread());

  if (g_buildconfig.debug_build()) {
    millisecs_t diff = g_core->GetAppTimeMillisecs() - swap_start_time_;
    if (diff > 5) {
      Log(LogLevel::kWarning, "Swap handling delay of " + std::to_string(diff));
    }
  }

#if BA_ENABLE_OPENGL
#if BA_SDL2_BUILD
  SDL_GL_SwapWindow(g_base->graphics_server->gl_context()->sdl_window());
#else
  SDL_GL_SwapBuffers();
#endif  // BA_SDL2_BUILD
#endif  // BA_ENABLE_OPENGL

  millisecs_t cur_time = g_core->GetAppTimeMillisecs();

  // Do some post-render analysis/updates.
  if (last_swap_time_ != 0) {
    millisecs_t diff2 = cur_time - last_swap_time_;
    if (auto_vsync_) {
      UpdateAutoVSync_(static_cast<int>(diff2));
    }

    // If we drop to a super-crappy FPS lets take some countermeasures such
    // as telling BG-dynamics to kill off some stuff.
    if (diff2 >= 1000 / 20) {
      too_slow_frame_count_++;
    } else {
      too_slow_frame_count_ = 0;
    }

    // Several slow frames in a row and we take action.
    if (too_slow_frame_count_ > 10) {
      too_slow_frame_count_ = 0;

      // A common cause of slowness is excessive smoke and bg stuff; lets
      // tell the bg dynamics thread to tone it down.
      g_base->bg_dynamics->TooSlow();
    }
  }
  last_swap_time_ = cur_time;
}

void AppAdapterSDL::SwapBuffers_() {
  swap_start_time_ = g_core->GetAppTimeMillisecs();
  assert(g_core->main_event_loop()->ThreadIsCurrent());
  DoSwap_();

  // FIXME: Move this somewhere reasonable. Not here. On mac/ios we wanna
  // delay our game-center login until we've drawn a few frames, so lets do
  // that here. ...hmm; why is that? I don't remember. Should revisit.
  if (g_buildconfig.use_game_center()) {
    static int f_count = 0;
    f_count++;
    if (f_count == 5) {
      g_core->platform->GameCenterLogin();
    }
  }
}

void AppAdapterSDL::UpdateAutoVSync_(int diff) {
  assert(auto_vsync_);

  // If we're currently vsyncing, watch for slow frames.
  if (vsync_enabled_) {
    bool should_disable{};

    // Note (March 2023): Currently mac opengl vsync seems broken on recent
    // OSs. See discussions: https://github.com/libsdl-org/SDL/issues/4918
    // Since Mac compositor generally avoids tearing anyway, just gonna have
    // 'auto' mode disable vsync for now. Explicit enable is still available
    // for odd cases where it still may be beneficial.
    if (g_buildconfig.ostype_macos()) {
      should_disable = true;
    } else {
      // Keep a smoothed average of the FPS we get with VSync on.
      {
        float this_fps = 1000.0f / static_cast<float>(diff);
        float smoothing = 0.95f;
        average_vsync_fps_ =
            smoothing * average_vsync_fps_ + (1.0f - smoothing) * this_fps;
      }

      // FIXME: should not be assuming a 60fps framerate these days. If
      // framerate drops significantly below 60, flip vsync off to get a
      // better framerate (but *only* if we're pretty sure we can hit 60
      // with it on; otherwise if we're on a 30hz monitor we'll get into a
      // cycle of flipping it off and on repeatedly since we slow down a lot
      // with it on and then speed up a lot with it off).
      if (diff >= 1000 / 40 && average_vsync_fps_ > 55.0f) {
        vsync_bad_frame_count_++;
      } else {
        vsync_bad_frame_count_ = 0;
      }
      should_disable = vsync_bad_frame_count_ >= 10;
    }
    if (should_disable) {
      vsync_enabled_ = false;
#if BA_ENABLE_OPENGL
      g_base->graphics_server->gl_context()->SetVSync(vsync_enabled_);
#endif
      vsync_good_frame_count_ = 0;
    }
  } else {
    bool should_enable{};
    if (g_buildconfig.ostype_macos()) {
      should_enable = false;
    } else {
      // Vsync is currently off; watch for framerate staying consistently
      // high and then turn it on again.
      if (diff <= 1000 / 50) {
        vsync_good_frame_count_++;
      } else {
        vsync_good_frame_count_ = 0;
      }
      // FIXME - should not be assuming a 60fps framerate these days.
      should_enable = vsync_good_frame_count_ >= 60;
    }
    if (should_enable) {
      vsync_enabled_ = true;
#if BA_ENABLE_OPENGL
      g_base->graphics_server->gl_context()->SetVSync(vsync_enabled_);
#endif
      vsync_bad_frame_count_ = 0;
    }
  }
}

void AppAdapterSDL::SetAutoVSync(bool enable) {
  auto_vsync_ = enable;
  // If we're doing auto, start with vsync on.
  if (enable) {
    vsync_enabled_ = true;
#if BA_ENABLE_OPENGL
    g_base->graphics_server->gl_context()->SetVSync(vsync_enabled_);
#endif
  }
}

void AppAdapterSDL::SDLJoystickConnected_(int device_index) {
  assert(g_core && g_core->InMainThread());

  // We add all existing inputs when bootstrapping is complete; we should
  // never be getting these before that happens.
  if (!g_base) {
    Log(LogLevel::kError,
        "Unexpected SDLJoystickConnected_ early in boot sequence.");
    return;
  }

  // Create the joystick here in the main thread and then pass it over to the
  // logic thread to be added to the game.
  if (g_buildconfig.ostype_ios_tvos()) {
    BA_LOG_ONCE(LogLevel::kError, "WTF GOT SDL-JOY-CONNECTED ON IOS");
  } else {
    auto* j = Object::NewDeferred<JoystickInput>(device_index);
    if (g_buildconfig.sdl2_build() && g_buildconfig.enable_sdl_joysticks()) {
      int instance_id = SDL_JoystickInstanceID(j->sdl_joystick());
      Get()->AddSDLInputDevice_(j, instance_id);
    } else {
      Get()->AddSDLInputDevice_(j, device_index);
    }
  }
}

void AppAdapterSDL::SDLJoystickDisconnected_(int index) {
  assert(g_core->InMainThread());
  assert(index >= 0);
  Get()->RemoveSDLInputDevice_(index);
}

void AppAdapterSDL::SetInitialScreenDimensions(const Vector2f& dimensions) {
  screen_dimensions_ = dimensions;
}

void AppAdapterSDL::AddSDLInputDevice_(JoystickInput* input, int index) {
  assert(g_base && g_base->input != nullptr);
  assert(input != nullptr);
  assert(g_core->InMainThread());
  assert(index >= 0);

  // Keep a mapping of SDL input-device indices to Joysticks.
  if (static_cast_check_fit<int>(sdl_joysticks_.size()) <= index) {
    sdl_joysticks_.resize(static_cast<size_t>(index) + 1, nullptr);
  }
  sdl_joysticks_[index] = input;

  g_base->input->PushAddInputDeviceCall(input, true);
}

void AppAdapterSDL::RemoveSDLInputDevice_(int index) {
  assert(g_core->InMainThread());
  assert(index >= 0);
  JoystickInput* j = GetSDLJoystickInput_(index);
  assert(j);
  if (static_cast_check_fit<int>(sdl_joysticks_.size()) > index) {
    sdl_joysticks_[index] = nullptr;
  } else {
    Log(LogLevel::kError, "Invalid index on RemoveSDLInputDevice: size is "
                              + std::to_string(sdl_joysticks_.size())
                              + "; index is " + std::to_string(index));
  }
  g_base->input->PushRemoveInputDeviceCall(j, true);
}

auto AppAdapterSDL::GetSDLJoystickInput_(const SDL_Event* e) const
    -> JoystickInput* {
  assert(g_core->InMainThread());
  int joy_id;

  // Attempt to pull the joystick id from the event.
  switch (e->type) {
    case SDL_JOYAXISMOTION:
      joy_id = e->jaxis.which;
      break;
    case SDL_JOYBUTTONDOWN:
    case SDL_JOYBUTTONUP:
      joy_id = e->jbutton.which;
      break;
    case SDL_JOYBALLMOTION:
      joy_id = e->jball.which;
      break;
    case SDL_JOYHATMOTION:
      joy_id = e->jhat.which;
      break;
    default:
      return nullptr;
  }
  return GetSDLJoystickInput_(joy_id);
}

auto AppAdapterSDL::GetSDLJoystickInput_(int sdl_joystick_id) const
    -> JoystickInput* {
  assert(g_core->InMainThread());
  for (auto sdl_joystick : sdl_joysticks_) {
    if ((sdl_joystick != nullptr) && (*sdl_joystick).sdl_joystick_id() >= 0
        && (*sdl_joystick).sdl_joystick_id() == sdl_joystick_id)
      return sdl_joystick;
  }
  return nullptr;  // Epic fail.
}

}  // namespace ballistica::base

#endif  // BA_SDL_BUILD
