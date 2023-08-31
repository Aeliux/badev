// Released under the MIT License. See LICENSE for details.

#include "ballistica/base/input/input.h"

#include "ballistica/base/app_mode/app_mode.h"
#include "ballistica/base/audio/audio.h"
#include "ballistica/base/graphics/support/camera.h"
#include "ballistica/base/input/device/joystick_input.h"
#include "ballistica/base/input/device/keyboard_input.h"
#include "ballistica/base/input/device/test_input.h"
#include "ballistica/base/input/device/touch_input.h"
#include "ballistica/base/logic/logic.h"
#include "ballistica/base/python/base_python.h"
#include "ballistica/base/support/app_config.h"
#include "ballistica/base/ui/console.h"
#include "ballistica/base/ui/ui.h"
#include "ballistica/shared/buildconfig/buildconfig_common.h"
#include "ballistica/shared/foundation/event_loop.h"
#include "ballistica/shared/generic/utils.h"

namespace ballistica::base {

Input::Input() = default;

template <typename F>
void SafePushLogicCall(const char* desc, const F& lambda) {
  // Note: originally this call was created to silently ignore early events
  // coming in before app stuff was up and running, but that was a bad idea,
  // as it caused us to ignore device-create messages sometimes which lead
  // to other issues later. So now I'm trying to fix those problems at the
  // source, but am leaving this intact for now as a clean way to catch
  // anything that needs fixing.
  if (!g_base) {
    FatalError(std::string(desc) + " called with null g_base.");
    return;
  }
  if (auto* loop = g_base->logic->event_loop()) {
    loop->PushCall(lambda);
  } else {
    FatalError(std::string(desc) + " called before logic event loop created.");
  }
}

void Input::PushCreateKeyboardInputDevices() {
  SafePushLogicCall(__func__, [this] { CreateKeyboardInputDevices(); });
}

void Input::CreateKeyboardInputDevices() {
  assert(g_base->InLogicThread());
  if (keyboard_input_ != nullptr || keyboard_input_2_ != nullptr) {
    Log(LogLevel::kError,
        "CreateKeyboardInputDevices called with existing kbs.");
    return;
  }
  keyboard_input_ = Object::NewDeferred<KeyboardInput>(nullptr);
  AddInputDevice(keyboard_input_, false);
  keyboard_input_2_ = Object::NewDeferred<KeyboardInput>(keyboard_input_);
  AddInputDevice(keyboard_input_2_, false);
}

void Input::PushDestroyKeyboardInputDevices() {
  SafePushLogicCall(__func__, [this] { DestroyKeyboardInputDevices(); });
}

void Input::DestroyKeyboardInputDevices() {
  assert(g_base->InLogicThread());
  if (keyboard_input_ == nullptr || keyboard_input_2_ == nullptr) {
    Log(LogLevel::kError,
        "DestroyKeyboardInputDevices called with null kb(s).");
    return;
  }
  RemoveInputDevice(keyboard_input_, false);
  keyboard_input_ = nullptr;
  RemoveInputDevice(keyboard_input_2_, false);
  keyboard_input_2_ = nullptr;
}

auto Input::GetInputDevice(int id) -> InputDevice* {
  if (id < 0 || id >= static_cast<int>(input_devices_.size())) {
    return nullptr;
  }
  return input_devices_[id].Get();
}

auto Input::GetInputDevice(const std::string& name,
                           const std::string& unique_id) -> InputDevice* {
  assert(g_base->InLogicThread());
  for (auto&& i : input_devices_) {
    if (i.Exists() && (i->GetDeviceName() == name)
        && i->GetPersistentIdentifier() == unique_id) {
      return i.Get();
    }
  }
  return nullptr;
}

auto Input::GetNewNumberedIdentifier(const std::string& name,
                                     const std::string& identifier) -> int {
  assert(g_base->InLogicThread());

  // Stuff like reserved_identifiers["JoyStickType"]["0x812312314"] = 2;

  // First off, if we came with an identifier, see if we've got a reserved
  // number already.
  if (!identifier.empty()) {
    auto i = reserved_identifiers_.find(name);
    if (i != reserved_identifiers_.end()) {
      auto j = i->second.find(identifier);
      if (j != i->second.end()) {
        return j->second;
      }
    }
  }

  int num = 1;
  int full_id;
  while (true) {
    bool in_use = false;

    // Scan other devices with the same device-name and find the first number
    // suffix that's not taken.
    for (auto&& i : input_devices_) {
      if (i.Exists()) {
        if ((i->GetRawDeviceName() == name) && i->number() == num) {
          in_use = true;
          break;
        }
      }
    }
    if (!in_use) {
      // Ok so far its unused.. however input devices that provide non-empty
      // identifiers (serial number, usb-id, etc) reserve their number for the
      // duration of the game, so we need to check against all reserved numbers
      // so we don't steal someones... (so that if they disconnect and reconnect
      // they'll get the same number and thus the same name, etc)
      if (!identifier.empty()) {
        auto i = reserved_identifiers_.find(name);
        if (i != reserved_identifiers_.end()) {
          for (auto&& j : i->second) {
            if (j.second == num) {
              in_use = true;
              break;
            }
          }
        }
      }

      // If its *still* clear lets nab it.
      if (!in_use) {
        full_id = num;

        // If we have an identifier, reserve it.
        if (!identifier.empty()) {
          reserved_identifiers_[name][identifier] = num;
        }
        break;
      }
    }
    num++;
  }
  return full_id;
}

void Input::CreateTouchInput() {
  assert(g_core->InMainThread());
  assert(touch_input_ == nullptr);
  touch_input_ = Object::NewDeferred<TouchInput>();
  PushAddInputDeviceCall(touch_input_, false);
}

void Input::AnnounceConnects() {
  static bool first_print = true;

  // For the first announcement just say "X controllers detected" and don't have
  // a sound.
  if (first_print && g_core->GetAppTimeMillisecs() < 10000) {
    first_print = false;

    // Disabling this completely for now; being more lenient with devices
    // allowed on android means this will often come back with large numbers.
    bool do_print{false};

    // If there's been several connected, just give a number.
    if (explicit_bool(do_print)) {
      if (newly_connected_controllers_.size() > 1) {
        std::string s =
            g_base->assets->GetResourceString("controllersDetectedText");
        Utils::StringReplaceOne(
            &s, "${COUNT}",
            std::to_string(newly_connected_controllers_.size()));
        ScreenMessage(s);
      } else {
        ScreenMessage(
            g_base->assets->GetResourceString("controllerDetectedText"));
      }
    }
  } else {
    // If there's been several connected, just give a number.
    if (newly_connected_controllers_.size() > 1) {
      std::string s =
          g_base->assets->GetResourceString("controllersConnectedText");
      Utils::StringReplaceOne(
          &s, "${COUNT}", std::to_string(newly_connected_controllers_.size()));
      ScreenMessage(s);
    } else {
      // If its just one, name it.
      std::string s =
          g_base->assets->GetResourceString("controllerConnectedText");
      Utils::StringReplaceOne(&s, "${CONTROLLER}",
                              newly_connected_controllers_.front());
      ScreenMessage(s);
    }
    if (g_base->assets->sys_assets_loaded()) {
      g_base->audio->PlaySound(g_base->assets->SysSound(SysSoundID::kGunCock));
    }
  }

  newly_connected_controllers_.clear();
}

void Input::AnnounceDisconnects() {
  // If there's been several connected, just give a number.
  if (newly_disconnected_controllers_.size() > 1) {
    std::string s =
        g_base->assets->GetResourceString("controllersDisconnectedText");
    Utils::StringReplaceOne(
        &s, "${COUNT}", std::to_string(newly_disconnected_controllers_.size()));
    ScreenMessage(s);
  } else {
    // If its just one, name it.
    std::string s =
        g_base->assets->GetResourceString("controllerDisconnectedText");
    Utils::StringReplaceOne(&s, "${CONTROLLER}",
                            newly_disconnected_controllers_.front());
    ScreenMessage(s);
  }
  if (g_base->assets->sys_assets_loaded()) {
    g_base->audio->PlaySound(g_base->assets->SysSound(SysSoundID::kCorkPop));
  }

  newly_disconnected_controllers_.clear();
}

void Input::ShowStandardInputDeviceConnectedMessage(InputDevice* j) {
  assert(g_base->InLogicThread());
  std::string suffix;
  suffix += j->GetPersistentIdentifier();
  suffix += j->GetDeviceExtraDescription();
  if (!suffix.empty()) {
    suffix = " " + suffix;
  }
  newly_connected_controllers_.push_back(j->GetDeviceName() + suffix);

  // Set a timer to go off and announce the accumulated additions.
  if (connect_print_timer_id_ != 0) {
    g_base->logic->DeleteAppTimer(connect_print_timer_id_);
  }
  connect_print_timer_id_ = g_base->logic->NewAppTimer(
      250, false, NewLambdaRunnable([this] { AnnounceConnects(); }));
}

void Input::ShowStandardInputDeviceDisconnectedMessage(InputDevice* j) {
  assert(g_base->InLogicThread());

  newly_disconnected_controllers_.push_back(j->GetDeviceName() + " "
                                            + j->GetPersistentIdentifier()
                                            + j->GetDeviceExtraDescription());

  // Set a timer to go off and announce the accumulated additions.
  if (disconnect_print_timer_id_ != 0) {
    g_base->logic->DeleteAppTimer(disconnect_print_timer_id_);
  }
  disconnect_print_timer_id_ = g_base->logic->NewAppTimer(
      250, false, NewLambdaRunnable([this] { AnnounceDisconnects(); }));
}

void Input::PushAddInputDeviceCall(InputDevice* input_device,
                                   bool standard_message) {
  SafePushLogicCall(__func__, [this, input_device, standard_message] {
    AddInputDevice(input_device, standard_message);
  });
}

void Input::RebuildInputDeviceDelegates() {
  assert(g_base->InLogicThread());
  for (auto&& device_ref : input_devices_) {
    if (auto* device = device_ref.Get()) {
      auto delegate = Object::CompleteDeferred(
          g_base->app_mode()->CreateInputDeviceDelegate(device));
      device->set_delegate(delegate);
      delegate->set_input_device(device);
    }
  }
}

void Input::AddInputDevice(InputDevice* device, bool standard_message) {
  assert(g_base->InLogicThread());

  // Let the current app-mode assign it a delegate.
  auto delegate = Object::CompleteDeferred(
      g_base->app_mode()->CreateInputDeviceDelegate(device));
  device->set_delegate(delegate);
  delegate->set_input_device(device);

  // Find the first unused input-device id and use that (might as well keep
  // our list small if we can).
  int index = 0;
  bool found_slot = false;
  for (auto& input_device : input_devices_) {
    if (!input_device.Exists()) {
      input_device = Object::CompleteDeferred(device);
      found_slot = true;
      device->set_index(index);
      break;
    }
    index++;
  }
  if (!found_slot) {
    input_devices_.push_back(Object::CompleteDeferred(device));
    device->set_index(static_cast<int>(input_devices_.size() - 1));
  }

  // We also want to give this input-device as unique an identifier as
  // possible. We ask it for its own string which hopefully includes a serial
  // or something, but if it doesn't and thus matches an already-existing one,
  // we tack an index on to it. that way we can at least uniquely address them
  // based off how many are connected.
  device->set_number(GetNewNumberedIdentifier(device->GetRawDeviceName(),
                                              device->GetDeviceIdentifier()));

  // Let the device know it's been added (for custom announcements, etc.)
  device->OnAdded();

  // Immediately apply controls if initial app-config has already been
  // applied; otherwise it'll happen as part of that.
  if (g_base->logic->applied_app_config()) {
    // Update controls for just this guy.
    device->UpdateMapping();

    // Need to do this after updating controls, as some control settings can
    // affect things we count (such as whether start activates default button).
    UpdateInputDeviceCounts();
  }

  if (g_buildconfig.ostype_macos()) {
    // Special case: on mac, the first time a iOS/Mac controller is connected,
    // let the user know they may want to enable them if they're currently set
    // as ignored. (the default at the moment is to only use classic device
    // support).
    static bool printed_ios_mac_controller_warning = false;
    if (!printed_ios_mac_controller_warning && ignore_mfi_controllers_
        && device->IsMFiController()) {
      ScreenMessage(R"({"r":"macControllerSubsystemMFiNoteText"})", {1, 1, 0});
      printed_ios_mac_controller_warning = true;
    }
  }

  if (standard_message && !device->ShouldBeHiddenFromUser()) {
    ShowStandardInputDeviceConnectedMessage(device);
  }
}

void Input::PushRemoveInputDeviceCall(InputDevice* input_device,
                                      bool standard_message) {
  SafePushLogicCall(__func__, [this, input_device, standard_message] {
    RemoveInputDevice(input_device, standard_message);
  });
}

void Input::RemoveInputDevice(InputDevice* input, bool standard_message) {
  assert(g_base->InLogicThread());

  if (standard_message && !input->ShouldBeHiddenFromUser()) {
    ShowStandardInputDeviceDisconnectedMessage(input);
  }

  // Just look for it in our list.. if we find it, simply clear the ref
  // (we need to keep the ref around so our list indices don't change).
  for (auto& input_device : input_devices_) {
    if (input_device.Exists() && (input_device.Get() == input)) {
      // Pull it off the list before killing it (in case it tries to trigger
      // another kill itself).
      Object::Ref<InputDevice> device = input_device;

      // Ok we cleared its slot in our vector; now we just have
      // the local variable 'device' keeping it alive.
      input_device.Clear();

      // Tell it to detach from anything it is controlling.
      device->DetachFromPlayer();

      // This should kill the device.
      // FIXME: since many devices get allocated in the main thread,
      //  should we not kill it there too?...
      device.Clear();
      UpdateInputDeviceCounts();
      return;
    }
  }
  throw Exception("Input::RemoveInputDevice: invalid device provided");
}

void Input::UpdateInputDeviceCounts() {
  assert(g_base->InLogicThread());

  auto current_time_millisecs =
      static_cast<millisecs_t>(g_base->logic->display_time() * 1000.0);
  have_button_using_inputs_ = false;
  have_start_activated_default_button_inputs_ = false;
  have_non_touch_inputs_ = false;
  int total = 0;
  int controller_count = 0;
  for (auto& input_device : input_devices_) {
    // Ok, we now limit non-keyboard non-touchscreen devices to ones that have
    // been active recently.. (we're starting to get lots of virtual devices and
    // other cruft on android; don't wanna show controller UIs just due to
    // those)
    if (input_device.Exists()
        && ((*input_device).IsTouchScreen() || (*input_device).IsKeyboard()
            || ((*input_device).last_input_time_millisecs() != 0
                && current_time_millisecs
                           - (*input_device).last_input_time_millisecs()
                       < 60000))) {
      total++;
      if (!(*input_device).IsTouchScreen()) {
        have_non_touch_inputs_ = true;
      }
      if ((*input_device).start_button_activates_default_widget()) {
        have_start_activated_default_button_inputs_ = true;
      }
      if ((*input_device).IsController()) {
        have_button_using_inputs_ = true;
        if (!(*input_device).IsUIOnly() && !(*input_device).IsTestInput()) {
          controller_count++;
        }
      }
    }
  }
  if (controller_count > max_controller_count_so_far_) {
    max_controller_count_so_far_ = controller_count;
    if (max_controller_count_so_far_ == 1) {
      g_base->python->objs().PushCall(
          BasePython::ObjID::kAwardInControlAchievementCall);
    } else if (max_controller_count_so_far_ == 2) {
      g_base->python->objs().PushCall(
          BasePython::ObjID::kAwardDualWieldingAchievementCall);
    }
  }
}

auto Input::GetLocalActiveInputDeviceCount() -> int {
  assert(g_base->InLogicThread());

  // This can get called alot so lets cache the value.
  auto current_time_millisecs =
      static_cast<millisecs_t>(g_base->logic->display_time() * 1000.0);
  if (current_time_millisecs
      != last_get_local_active_input_device_count_check_time_) {
    last_get_local_active_input_device_count_check_time_ =
        current_time_millisecs;

    int count = 0;
    for (auto& input_device : input_devices_) {
      // Tally up local non-keyboard, non-touchscreen devices that have been
      // used in the last minute.
      if (input_device.Exists() && !input_device->IsKeyboard()
          && !input_device->IsTouchScreen() && !input_device->IsUIOnly()
          && input_device->IsLocal()
          && (input_device->last_input_time_millisecs() != 0
              && current_time_millisecs
                         - input_device->last_input_time_millisecs()
                     < 60000)) {
        count++;
      }
    }
    local_active_input_device_count_ = count;
  }
  return local_active_input_device_count_;
}

auto Input::HaveControllerWithPlayer() -> bool {
  assert(g_base->InLogicThread());
  // NOLINTNEXTLINE(readability-use-anyofallof)
  for (auto& input_device : input_devices_) {
    if (input_device.Exists() && (*input_device).IsController()
        && (*input_device).AttachedToPlayer()) {
      return true;
    }
  }
  return false;
}

auto Input::HaveRemoteAppController() -> bool {
  assert(g_base->InLogicThread());
  // NOLINTNEXTLINE(readability-use-anyofallof)
  for (auto& input_device : input_devices_) {
    if (input_device.Exists() && (*input_device).IsRemoteApp()) {
      return true;
    }
  }
  return false;
}

auto Input::GetInputDevicesWithName(const std::string& name)
    -> std::vector<InputDevice*> {
  std::vector<InputDevice*> vals;
  if (!g_core->HeadlessMode()) {
    for (auto& input_device : input_devices_) {
      if (input_device.Exists()) {
        auto* js = dynamic_cast<JoystickInput*>(input_device.Get());
        if (js && js->GetDeviceName() == name) {
          vals.push_back(js);
        }
      }
    }
  }
  return vals;
}

auto Input::GetConfigurableGamePads() -> std::vector<InputDevice*> {
  assert(g_base->InLogicThread());
  std::vector<InputDevice*> vals;
  if (!g_core->HeadlessMode()) {
    for (auto& input_device : input_devices_) {
      if (input_device.Exists()) {
        auto* js = dynamic_cast<JoystickInput*>(input_device.Get());
        if (js && js->GetAllowsConfiguring() && !js->ShouldBeHiddenFromUser()) {
          vals.push_back(js);
        }
      }
    }
  }
  return vals;
}

auto Input::ShouldCompletelyIgnoreInputDevice(InputDevice* input_device)
    -> bool {
  if (g_buildconfig.ostype_macos()) {
    if (ignore_mfi_controllers_ && input_device->IsMFiController()) {
      return true;
    }
  }
  return ignore_sdl_controllers_ && input_device->IsSDLController();
}

void Input::UpdateEnabledControllerSubsystems() {
  assert(g_base);

  // First off, on mac, let's update whether we want to completely ignore either
  // the classic or the iOS/Mac controller subsystems.
  if (g_buildconfig.ostype_macos()) {
    std::string sys = g_base->app_config->Resolve(
        AppConfig::StringID::kMacControllerSubsystem);
    if (sys == "Classic") {
      ignore_mfi_controllers_ = true;
      ignore_sdl_controllers_ = false;
    } else if (sys == "MFi") {
      ignore_mfi_controllers_ = false;
      ignore_sdl_controllers_ = true;
    } else if (sys == "Both") {
      ignore_mfi_controllers_ = false;
      ignore_sdl_controllers_ = false;
    } else {
      BA_LOG_ONCE(LogLevel::kError,
                  "Invalid mac-controller-subsystem value: '" + sys + "'");
    }
  }
}

void Input::OnAppStart() { assert(g_base->InLogicThread()); }

void Input::OnAppPause() { assert(g_base->InLogicThread()); }

void Input::OnAppResume() { assert(g_base->InLogicThread()); }

void Input::OnAppShutdown() { assert(g_base->InLogicThread()); }

// Tells all inputs to update their controls based on the app config.
void Input::DoApplyAppConfig() {
  assert(g_base->InLogicThread());

  UpdateEnabledControllerSubsystems();

  // It's technically possible that updating these controls will add or remove
  // devices, thus changing the input_devices_ list, so lets work with a copy of
  // it.
  std::vector<Object::Ref<InputDevice> > input_devices = input_devices_;
  for (auto& input_device : input_devices) {
    if (input_device.Exists()) {
      input_device->UpdateMapping();
    }
  }

  // Some config settings can affect this.
  UpdateInputDeviceCounts();
}

void Input::OnScreenSizeChange() { assert(g_base->InLogicThread()); }

void Input::StepDisplayTime() {
  assert(g_base->InLogicThread());

  millisecs_t real_time = g_core->GetAppTimeMillisecs();

  // If input has been locked an excessively long amount of time, unlock it.
  if (input_lock_count_temp_) {
    if (real_time - last_input_temp_lock_time_ > 10000) {
      Log(LogLevel::kError,
          "Input has been temp-locked for 10 seconds; unlocking.");
      input_lock_count_temp_ = 0;
      PrintLockLabels();
      input_lock_temp_labels_.clear();
      input_unlock_temp_labels_.clear();
    }
  }

  // We now need to update our input-device numbers dynamically since they're
  // based on recently-active devices.
  // ..we do this much more often for the first few seconds to keep
  // controller-usage from being as annoying.
  // millisecs_t incr = (real_time > 10000) ? 468 : 98;
  // Update: don't remember why that was annoying; trying a single value for
  // now.
  millisecs_t incr = 249;
  if (real_time - last_input_device_count_update_time_ > incr) {
    UpdateInputDeviceCounts();
    last_input_device_count_update_time_ = real_time;

    // Keep our idle-time up to date.
    if (input_active_) {
      input_idle_time_ = 0;
    } else {
      input_idle_time_ += incr;
    }
    input_active_ = false;
  }

  for (auto& input_device : input_devices_) {
    if (input_device.Exists()) {
      (*input_device).Update();
    }
  }
}

void Input::Reset() {
  assert(g_base->InLogicThread());

  // Detach all inputs from players.
  for (auto& input_device : input_devices_) {
    if (input_device.Exists()) {
      input_device->DetachFromPlayer();
    }
  }
}

void Input::ResetHoldStates() {
  assert(g_base->InLogicThread());
  ResetKeyboardHeldKeys();
  ResetJoyStickHeldButtons();
}

void Input::LockAllInput(bool permanent, const std::string& label) {
  assert(g_base->InLogicThread());
  if (permanent) {
    input_lock_count_permanent_++;
    input_lock_permanent_labels_.push_back(label);
  } else {
    input_lock_count_temp_++;
    if (input_lock_count_temp_ == 1) {
      last_input_temp_lock_time_ = g_core->GetAppTimeMillisecs();
    }
    input_lock_temp_labels_.push_back(label);

    recent_input_locks_unlocks_.push_back(
        "temp lock: " + label + " time "
        + std::to_string(g_core->GetAppTimeMillisecs()));
    while (recent_input_locks_unlocks_.size() > 10) {
      recent_input_locks_unlocks_.pop_front();
    }
  }
}

void Input::UnlockAllInput(bool permanent, const std::string& label) {
  assert(g_base->InLogicThread());

  recent_input_locks_unlocks_.push_back(
      permanent ? "permanent unlock: "
                : "temp unlock: " + label + " time "
                      + std::to_string(g_core->GetAppTimeMillisecs()));
  while (recent_input_locks_unlocks_.size() > 10)
    recent_input_locks_unlocks_.pop_front();

  if (permanent) {
    input_lock_count_permanent_--;
    input_unlock_permanent_labels_.push_back(label);
    if (input_lock_count_permanent_ < 0) {
      BA_LOG_PYTHON_TRACE_ONCE("lock-count-permanent < 0");
      PrintLockLabels();
      input_lock_count_permanent_ = 0;
    }

    // When lock counts get back down to zero, clear our labels since all is
    // well.
    if (input_lock_count_permanent_ == 0) {
      input_lock_permanent_labels_.clear();
      input_unlock_permanent_labels_.clear();
    }
  } else {
    input_lock_count_temp_--;
    input_unlock_temp_labels_.push_back(label);
    if (input_lock_count_temp_ < 0) {
      Log(LogLevel::kWarning,
          "temp input unlock at time "
              + std::to_string(g_core->GetAppTimeMillisecs())
              + " with no active lock: '" + label + "'");
      // This is to be expected since we can reset this to 0.
      input_lock_count_temp_ = 0;
    }

    // When lock counts get back down to zero, clear our labels since all is
    // well.
    if (input_lock_count_temp_ == 0) {
      input_lock_temp_labels_.clear();
      input_unlock_temp_labels_.clear();
    }
  }
}

void Input::PrintLockLabels() {
  std::string s = "INPUT LOCK REPORT (time="
                  + std::to_string(g_core->GetAppTimeMillisecs()) + "):";
  int num;

  s += "\n " + std::to_string(input_lock_temp_labels_.size()) + " TEMP LOCKS:";
  num = 1;
  for (auto& input_lock_temp_label : input_lock_temp_labels_) {
    s += "\n   " + std::to_string(num++) + ": " + input_lock_temp_label;
  }

  s += "\n " + std::to_string(input_unlock_temp_labels_.size())
       + " TEMP UNLOCKS:";
  num = 1;
  for (auto& input_unlock_temp_label : input_unlock_temp_labels_) {
    s += "\n   " + std::to_string(num++) + ": " + input_unlock_temp_label;
  }

  s += "\n " + std::to_string(input_lock_permanent_labels_.size())
       + " PERMANENT LOCKS:";
  num = 1;
  for (auto& input_lock_permanent_label : input_lock_permanent_labels_) {
    s += "\n   " + std::to_string(num++) + ": " + input_lock_permanent_label;
  }

  s += "\n " + std::to_string(input_unlock_permanent_labels_.size())
       + " PERMANENT UNLOCKS:";
  num = 1;
  for (auto& input_unlock_permanent_label : input_unlock_permanent_labels_) {
    s += "\n   " + std::to_string(num++) + ": " + input_unlock_permanent_label;
  }
  s += "\n " + std::to_string(recent_input_locks_unlocks_.size())
       + " MOST RECENT LOCKS:";
  num = 1;
  for (auto& recent_input_locks_unlock : recent_input_locks_unlocks_) {
    s += "\n   " + std::to_string(num++) + ": " + recent_input_locks_unlock;
  }

  Log(LogLevel::kError, s);
}

void Input::ProcessStressTesting(int player_count) {
  assert(g_core->InMainThread());
  assert(player_count >= 0);

  millisecs_t time = g_core->GetAppTimeMillisecs();

  // FIXME: If we don't check for stress_test_last_leave_time_ we totally
  //  confuse the game.. need to be able to survive that.

  // Kill some off if we have too many.
  while (static_cast<int>(test_inputs_.size()) > player_count) {
    delete test_inputs_.front();
    test_inputs_.pop_front();
  }

  // If we have less than full test-inputs, add one randomly.
  if (static_cast<int>(test_inputs_.size()) < player_count
      && ((rand() % 1000 < 10))) {  // NOLINT
    test_inputs_.push_back(new TestInput());
  }

  // Every so often lets kill the oldest one off.
  if (explicit_bool(true)) {
    if (test_inputs_.size() > 0 && (rand() % 2000 < 3)) {  // NOLINT
      stress_test_last_leave_time_ = time;

      // Usually do oldest; sometimes newest.
      if (rand() % 5 == 0) {  // NOLINT
        delete test_inputs_.back();
        test_inputs_.pop_back();
      } else {
        delete test_inputs_.front();
        test_inputs_.pop_front();
      }
    }
  }

  if (time - stress_test_time_ > 1000) {
    stress_test_time_ = time;  // reset..
    for (auto& test_input : test_inputs_) {
      (*test_input).Reset();
    }
  }
  while (stress_test_time_ < time) {
    stress_test_time_++;
    for (auto& test_input : test_inputs_) {
      (*test_input).Process(stress_test_time_);
    }
  }
}

void Input::PushTextInputEvent(const std::string& text) {
  SafePushLogicCall(__func__, [this, text] {
    MarkInputActive();

    // Ignore  if input is locked.
    if (IsInputLocked()) {
      return;
    }
    if (g_base && g_base->console() != nullptr
        && g_base->console()->HandleTextEditing(text)) {
      return;
    }
    g_base->ui->SendWidgetMessage(WidgetMessage(
        WidgetMessage::Type::kTextInput, nullptr, 0, 0, 0, 0, text.c_str()));
  });
}

void Input::PushJoystickEvent(const SDL_Event& event,
                              InputDevice* input_device) {
  SafePushLogicCall(__func__, [this, event, input_device] {
    HandleJoystickEvent(event, input_device);
  });
}

void Input::HandleJoystickEvent(const SDL_Event& event,
                                InputDevice* input_device) {
  assert(g_base->InLogicThread());
  assert(input_device);

  if (ShouldCompletelyIgnoreInputDevice(input_device)) {
    return;
  }
  if (IsInputLocked()) {
    return;
  }

  // Make note that we're not idle.
  MarkInputActive();

  // And that this particular device isn't idle either.
  input_device->UpdateLastInputTime();

  // If someone is capturing these events, give them a crack at it.
  if (joystick_input_capture_) {
    if (joystick_input_capture_(event, input_device)) {
      return;
    }
  }

  input_device->HandleSDLEvent(&event);
}

void Input::PushKeyPressEvent(const SDL_Keysym& keysym) {
  SafePushLogicCall(__func__, [this, keysym] { HandleKeyPress(&keysym); });
}

void Input::PushKeyReleaseEvent(const SDL_Keysym& keysym) {
  SafePushLogicCall(__func__, [this, keysym] { HandleKeyRelease(&keysym); });
}

void Input::CaptureKeyboardInput(HandleKeyPressCall* press_call,
                                 HandleKeyReleaseCall* release_call) {
  assert(g_base->InLogicThread());
  if (keyboard_input_capture_press_ || keyboard_input_capture_release_) {
    Log(LogLevel::kError, "Setting key capture redundantly.");
  }
  keyboard_input_capture_press_ = press_call;
  keyboard_input_capture_release_ = release_call;
}

void Input::ReleaseKeyboardInput() {
  assert(g_base->InLogicThread());
  keyboard_input_capture_press_ = nullptr;
  keyboard_input_capture_release_ = nullptr;
}

void Input::CaptureJoystickInput(HandleJoystickEventCall* call) {
  assert(g_base->InLogicThread());
  if (joystick_input_capture_) {
    Log(LogLevel::kError, "Setting joystick capture redundantly.");
  }
  joystick_input_capture_ = call;
}

void Input::ReleaseJoystickInput() {
  assert(g_base->InLogicThread());
  joystick_input_capture_ = nullptr;
}

void Input::HandleKeyPress(const SDL_Keysym* keysym) {
  assert(g_base->InLogicThread());

  MarkInputActive();

  // Ignore all key presses if input is locked.
  if (IsInputLocked()) {
    return;
  }

  // If someone is capturing these events, give them a crack at it.
  if (keyboard_input_capture_press_) {
    if (keyboard_input_capture_press_(*keysym)) {
      return;
    }
  }

  // Regardless of what else we do, keep track of mod key states.
  // (for things like manual camera moves. For individual key presses
  // ideally we should use the modifiers bundled with the key presses)
  UpdateModKeyStates(keysym, true);

  bool repeat_press;
  if (keys_held_.count(keysym->sym) != 0) {
    repeat_press = true;
  } else {
    repeat_press = false;
    keys_held_.insert(keysym->sym);
  }

  // Mobile-specific stuff.
  if (g_buildconfig.ostype_ios_tvos() || g_buildconfig.ostype_android()) {
    switch (keysym->sym) {
      // FIXME: See if this stuff is still necessary. Was this perhaps
      //  specifically to support the console?
      case SDLK_DELETE:
      case SDLK_RETURN:
      case SDLK_KP_ENTER:
      case SDLK_BACKSPACE: {
        // FIXME: I don't remember what this was put here for, but now that
        // we
        //  have hardware keyboards it crashes text fields by sending them a
        //  TEXT_INPUT message with no string.. I made them resistant to
        //  that case but wondering if we can take this out?...
        g_base->ui->SendWidgetMessage(
            WidgetMessage(WidgetMessage::Type::kTextInput, keysym));
        break;
      }
      default:
        break;
    }
  }

  // A few things that apply only to non-mobile.
  if (!g_buildconfig.ostype_ios_tvos() && !g_buildconfig.ostype_android()) {
    // Command-F or Control-F toggles full-screen.
    if (!repeat_press && keysym->sym == SDLK_f
        && ((keysym->mod & KMOD_CTRL) || (keysym->mod & KMOD_GUI))) {
      g_base->python->objs()
          .Get(BasePython::ObjID::kToggleFullscreenCall)
          .Call();
      return;
    }

    // Control-Q quits. On mac, the usual cmd-q gets handled by SDL/etc.
    // implicitly.
    if (!repeat_press && keysym->sym == SDLK_q && (keysym->mod & KMOD_CTRL)) {
      g_base->ui->ConfirmQuit();
      return;
    }
  }

  // Let the console intercept stuff if it wants at this point.
  if (g_base && g_base->console() != nullptr
      && g_base->console()->HandleKeyPress(keysym)) {
    return;
  }

  // Ctrl-V or Cmd-V sends paste commands to any interested text fields.
  // Command-Q or Control-Q quits.
  if (!repeat_press && keysym->sym == SDLK_v
      && ((keysym->mod & KMOD_CTRL) || (keysym->mod & KMOD_GUI))) {
    g_base->ui->SendWidgetMessage(WidgetMessage(WidgetMessage::Type::kPaste));
    return;
  }

  bool handled = false;

  // None of the following stuff accepts key repeats.
  if (!repeat_press) {
    switch (keysym->sym) {
      // Menu button on android/etc. pops up the menu.
      case SDLK_MENU: {
        if (!g_base->ui->MainMenuVisible()) {
          g_base->ui->PushMainMenuPressCall(touch_input_);
        }
        handled = true;
        break;
      }

      case SDLK_EQUALS:
      case SDLK_PLUS:
        g_base->app_mode()->ChangeGameSpeed(1);
        handled = true;
        break;

      case SDLK_MINUS:
        g_base->app_mode()->ChangeGameSpeed(-1);
        handled = true;
        break;

      case SDLK_F5: {
        if (g_base->ui->PartyIconVisible()) {
          g_base->ui->ActivatePartyIcon();
        }
        handled = true;
        break;
      }

      case SDLK_F7:
        SafePushLogicCall(__func__,
                          [] { g_base->graphics->ToggleManualCamera(); });
        handled = true;
        break;

      case SDLK_F8:
        SafePushLogicCall(
            __func__, [] { g_base->graphics->ToggleNetworkDebugDisplay(); });
        handled = true;
        break;

      case SDLK_F9:
        g_base->python->objs().PushCall(
            BasePython::ObjID::kLanguageTestToggleCall);
        handled = true;
        break;

      case SDLK_F10:
        SafePushLogicCall(__func__,
                          [] { g_base->graphics->ToggleDebugDraw(); });
        handled = true;
        break;

      case SDLK_ESCAPE:

        if (!g_base->ui->MainMenuVisible()) {
          // There's no main menu up. Ask for one.

          // Note: keyboard_input_ may be nullptr but escape key should
          // still function for menus; it just won't claim ownership.
          g_base->ui->PushMainMenuPressCall(keyboard_input_);
        } else {
          // Ok there *is* a main menu up. Send it a cancel message.
          g_base->ui->SendWidgetMessage(
              WidgetMessage(WidgetMessage::Type::kCancel));
        }
        handled = true;
        break;

      default:
        break;
    }
  }

  // If we haven't claimed it, pass it along as potential player/widget input.
  if (!handled) {
    if (keyboard_input_) {
      keyboard_input_->HandleKey(keysym, repeat_press, true);
    }
  }
}

void Input::HandleKeyRelease(const SDL_Keysym* keysym) {
  assert(g_base->InLogicThread());

  // Note: we want to let these through even if input is locked.

  MarkInputActive();

  // If someone is capturing these events, give them a crack at it.
  if (keyboard_input_capture_release_) {
    if (keyboard_input_capture_release_(*keysym)) {
      return;
    }
  }

  // Regardless of what else we do, keep track of mod key states.
  // (for things like manual camera moves. For individual key presses
  // ideally we should use the modifiers bundled with the key presses)
  UpdateModKeyStates(keysym, false);

  // In some cases we may receive duplicate key-release events
  // (if a keyboard reset was run it deals out key releases but then the
  // keyboard driver issues them as well)
  if (keys_held_.count(keysym->sym) == 0) {
    return;
  }

  keys_held_.erase(keysym->sym);

  if (IsInputLocked()) {
    return;
  }

  bool handled = false;

  if (g_base && g_base->console() != nullptr
      && g_base->console()->HandleKeyRelease(keysym)) {
    handled = true;
  }

  // If we haven't claimed it, pass it along as potential player input.
  if (!handled) {
    if (keyboard_input_) {
      keyboard_input_->HandleKey(keysym, false, false);
    }
  }
}

void Input::UpdateModKeyStates(const SDL_Keysym* keysym, bool press) {
  switch (keysym->sym) {
    case SDLK_LCTRL:
    case SDLK_RCTRL: {
      if (Camera* c = g_base->graphics->camera()) {
        c->set_ctrl_down(press);
      }
      break;
    }
    case SDLK_LALT:
    case SDLK_RALT: {
      if (Camera* c = g_base->graphics->camera()) {
        c->set_alt_down(press);
      }
      break;
    }
    case SDLK_LGUI:
    case SDLK_RGUI: {
      if (Camera* c = g_base->graphics->camera()) {
        c->set_cmd_down(press);
      }
      break;
    }
    default:
      break;
  }
}

void Input::PushMouseScrollEvent(const Vector2f& amount) {
  SafePushLogicCall(__func__, [this, amount] { HandleMouseScroll(amount); });
}

void Input::HandleMouseScroll(const Vector2f& amount) {
  assert(g_base->InLogicThread());
  if (IsInputLocked()) {
    return;
  }
  MarkInputActive();

  if (std::abs(amount.y) > 0.0001f) {
    g_base->ui->SendWidgetMessage(
        WidgetMessage(WidgetMessage::Type::kMouseWheel, nullptr, cursor_pos_x_,
                      cursor_pos_y_, amount.y));
  }
  if (std::abs(amount.x) > 0.0001f) {
    g_base->ui->SendWidgetMessage(
        WidgetMessage(WidgetMessage::Type::kMouseWheelH, nullptr, cursor_pos_x_,
                      cursor_pos_y_, amount.x));
  }
  mouse_move_count_++;

  Camera* camera = g_base->graphics->camera();
  if (camera) {
    if (camera->manual()) {
      camera->ManualHandleMouseWheel(0.005f * amount.y);
    }
  }
}

void Input::PushSmoothMouseScrollEvent(const Vector2f& velocity,
                                       bool momentum) {
  SafePushLogicCall(__func__, [this, velocity, momentum] {
    HandleSmoothMouseScroll(velocity, momentum);
  });
}

void Input::HandleSmoothMouseScroll(const Vector2f& velocity, bool momentum) {
  assert(g_base->InLogicThread());
  if (IsInputLocked()) {
    return;
  }
  MarkInputActive();

  bool handled = false;
  handled = g_base->ui->SendWidgetMessage(
      WidgetMessage(WidgetMessage::Type::kMouseWheelVelocity, nullptr,
                    cursor_pos_x_, cursor_pos_y_, velocity.y, momentum));
  g_base->ui->SendWidgetMessage(
      WidgetMessage(WidgetMessage::Type::kMouseWheelVelocityH, nullptr,
                    cursor_pos_x_, cursor_pos_y_, velocity.x, momentum));

  last_mouse_move_time_ = g_core->GetAppTimeMillisecs();
  mouse_move_count_++;

  Camera* camera = g_base->graphics->camera();
  if (!handled && camera) {
    if (camera->manual()) {
      camera->ManualHandleMouseWheel(-0.25f * velocity.y);
    }
  }
}

void Input::PushMouseMotionEvent(const Vector2f& position) {
  SafePushLogicCall(__func__,
                    [this, position] { HandleMouseMotion(position); });
}

void Input::HandleMouseMotion(const Vector2f& position) {
  assert(g_base->graphics);
  assert(g_base->InLogicThread());
  MarkInputActive();

  float old_cursor_pos_x = cursor_pos_x_;
  float old_cursor_pos_y = cursor_pos_y_;

  // Convert normalized view coords to our virtual ones.
  cursor_pos_x_ = g_base->graphics->PixelToVirtualX(
      position.x * g_base->graphics->screen_pixel_width());
  cursor_pos_y_ = g_base->graphics->PixelToVirtualY(
      position.y * g_base->graphics->screen_pixel_height());

  last_mouse_move_time_ = g_core->GetAppTimeMillisecs();
  mouse_move_count_++;

  bool handled{};

  // If we have a touch-input in editing mode, pass along events to it.
  // (it usually handles its own events but here we want it to play nice
  // with stuff under it by blocking touches, etc)
  if (touch_input_ && touch_input_->editing()) {
    touch_input_->HandleTouchMoved(reinterpret_cast<void*>(1), cursor_pos_x_,
                                   cursor_pos_y_);
  }

  // UI interaction.
  if (!IsInputLocked()) {
    handled = g_base->ui->SendWidgetMessage(
        WidgetMessage(WidgetMessage::Type::kMouseMove, nullptr, cursor_pos_x_,
                      cursor_pos_y_));
  }

  // Manual camera motion.
  Camera* camera = g_base->graphics->camera();
  if (!handled && camera && camera->manual()) {
    float move_h = (cursor_pos_x_ - old_cursor_pos_x)
                   / g_base->graphics->screen_virtual_width();
    float move_v = (cursor_pos_y_ - old_cursor_pos_y)
                   / g_base->graphics->screen_virtual_width();
    camera->ManualHandleMouseMove(move_h, move_v);
  }

  // Old screen edge UI.
  g_base->ui->HandleLegacyRootUIMouseMotion(cursor_pos_x_, cursor_pos_y_);
}

void Input::PushMouseDownEvent(int button, const Vector2f& position) {
  SafePushLogicCall(__func__, [this, button, position] {
    HandleMouseDown(button, position);
  });
}

void Input::HandleMouseDown(int button, const Vector2f& position) {
  assert(g_base);
  assert(g_base->graphics);
  assert(g_base->InLogicThread());

  if (IsInputLocked()) {
    return;
  }

  //  if (!g_base->ui->MainMenuVisible()) {
  //    return;
  //  }

  MarkInputActive();

  last_mouse_move_time_ = g_core->GetAppTimeMillisecs();
  mouse_move_count_++;

  // Convert normalized view coords to our virtual ones.
  cursor_pos_x_ = g_base->graphics->PixelToVirtualX(
      position.x * g_base->graphics->screen_pixel_width());
  cursor_pos_y_ = g_base->graphics->PixelToVirtualY(
      position.y * g_base->graphics->screen_pixel_height());

  millisecs_t click_time = g_core->GetAppTimeMillisecs();
  bool double_click = (click_time - last_click_time_ <= double_click_time_);
  last_click_time_ = click_time;

  bool handled{};
  // auto* root_widget = g_base->ui->root_widget();

  // If we have a touch-input in editing mode, pass along events to it.
  // (it usually handles its own events but here we want it to play nice
  // with stuff under it by blocking touches, etc)
  if (touch_input_ && touch_input_->editing()) {
    handled = touch_input_->HandleTouchDown(reinterpret_cast<void*>(1),
                                            cursor_pos_x_, cursor_pos_y_);
  }

  if (!handled) {
    if (g_base->ui->HandleLegacyRootUIMouseDown(cursor_pos_x_, cursor_pos_y_)) {
      handled = true;
    }
  }

  if (!handled) {
    handled = g_base->ui->SendWidgetMessage(
        WidgetMessage(WidgetMessage::Type::kMouseDown, nullptr, cursor_pos_x_,
                      cursor_pos_y_, double_click ? 2 : 1));
  }

  // Manual camera input.
  Camera* camera = g_base->graphics->camera();
  if (!handled && camera) {
    switch (button) {
      case SDL_BUTTON_LEFT:
        camera->set_mouse_left_down(true);
        break;
      case SDL_BUTTON_RIGHT:
        camera->set_mouse_right_down(true);
        break;
      case SDL_BUTTON_MIDDLE:
        camera->set_mouse_middle_down(true);
        break;
      default:
        break;
    }
    camera->UpdateManualMode();
  }
}

void Input::PushMouseUpEvent(int button, const Vector2f& position) {
  SafePushLogicCall(
      __func__, [this, button, position] { HandleMouseUp(button, position); });
}

void Input::HandleMouseUp(int button, const Vector2f& position) {
  assert(g_base->InLogicThread());
  MarkInputActive();

  // Convert normalized view coords to our virtual ones.
  cursor_pos_x_ = g_base->graphics->PixelToVirtualX(
      position.x * g_base->graphics->screen_pixel_width());
  cursor_pos_y_ = g_base->graphics->PixelToVirtualY(
      position.y * g_base->graphics->screen_pixel_height());

  bool handled{};

  // If we have a touch-input in editing mode, pass along events to it.
  // (it usually handles its own events but here we want it to play nice
  // with stuff under it by blocking touches, etc)
  if (touch_input_ && touch_input_->editing()) {
    touch_input_->HandleTouchUp(reinterpret_cast<void*>(1), cursor_pos_x_,
                                cursor_pos_y_);
  }

  // ui_v1::Widget* root_widget = g_base->ui->root_widget();
  // if (root_widget) {
  handled = g_base->ui->SendWidgetMessage(WidgetMessage(
      WidgetMessage::Type::kMouseUp, nullptr, cursor_pos_x_, cursor_pos_y_));
  // }

  Camera* camera = g_base->graphics->camera();
  if (!handled && camera) {
    switch (button) {
      case SDL_BUTTON_LEFT:
        camera->set_mouse_left_down(false);
        break;
      case SDL_BUTTON_RIGHT:
        camera->set_mouse_right_down(false);
        break;
      case SDL_BUTTON_MIDDLE:
        camera->set_mouse_middle_down(false);
        break;
      default:
        break;
    }
    camera->UpdateManualMode();
  }

  g_base->ui->HandleLegacyRootUIMouseUp(cursor_pos_x_, cursor_pos_y_);
}

void Input::PushTouchEvent(const TouchEvent& e) {
  SafePushLogicCall(__func__, [e, this] { HandleTouchEvent(e); });
}

void Input::HandleTouchEvent(const TouchEvent& e) {
  assert(g_base->InLogicThread());
  assert(g_base->graphics);

  if (IsInputLocked()) {
    return;
  }

  MarkInputActive();

  // float x = e.x;
  // float y = e.y;

  if (g_buildconfig.ostype_ios_tvos()) {
    printf("FIXME: update touch handling\n");
  }

  float x = g_base->graphics->PixelToVirtualX(
      e.x * g_base->graphics->screen_pixel_width());
  float y = g_base->graphics->PixelToVirtualY(
      e.y * g_base->graphics->screen_pixel_height());

  if (e.overall) {
    // Sanity test: if the OS tells us that this is the beginning of an,
    // overall multitouch gesture, it should always be winding up as our
    // single_touch_.
    if (e.type == TouchEvent::Type::kDown && single_touch_ != nullptr) {
      BA_LOG_ONCE(LogLevel::kError,
                  "Got touch labeled first but will not be our single.");
    }

    // Also: if the OS tells us that this is the end of an overall
    // multi-touch gesture, it should mean that our single_touch_ has ended
    // or will be.
    if ((e.type == TouchEvent::Type::kUp
         || e.type == TouchEvent::Type::kCanceled)
        && single_touch_ != nullptr && single_touch_ != e.touch) {
      BA_LOG_ONCE(LogLevel::kError,
                  "Last touch coming up is not single touch!");
    }
  }

  // We keep track of one 'single' touch which we pass along as
  // mouse events which covers most UI stuff.
  if (e.type == TouchEvent::Type::kDown && single_touch_ == nullptr) {
    single_touch_ = e.touch;
    HandleMouseDown(SDL_BUTTON_LEFT, Vector2f(e.x, e.y));
  }

  if (e.type == TouchEvent::Type::kMoved && e.touch == single_touch_) {
    HandleMouseMotion(Vector2f(e.x, e.y));
  }

  // Currently just applying touch-cancel the same as touch-up here;
  // perhaps should be smarter in the future.
  if ((e.type == TouchEvent::Type::kUp || e.type == TouchEvent::Type::kCanceled)
      && (e.touch == single_touch_ || e.overall)) {
    single_touch_ = nullptr;
    HandleMouseUp(SDL_BUTTON_LEFT, Vector2f(e.x, e.y));
  }

  // If we've got a touch input device, forward events along to it.
  if (touch_input_) {
    touch_input_->HandleTouchEvent(e.type, e.touch, x, y);
  }
}

void Input::ResetJoyStickHeldButtons() {
  for (auto&& i : input_devices_) {
    if (i.Exists()) {
      i->ResetHeldStates();
    }
  }
}

// Send key-ups for any currently-held keys.
void Input::ResetKeyboardHeldKeys() {
  assert(g_base->InLogicThread());
  if (!g_core->HeadlessMode()) {
    // Synthesize key-ups for all our held keys.
    while (!keys_held_.empty()) {
      SDL_Keysym k;
      memset(&k, 0, sizeof(k));
      k.sym = (SDL_Keycode)(*keys_held_.begin());
      HandleKeyRelease(&k);
    }
  }
}

void Input::Draw(FrameDef* frame_def) {
  // Draw touch input visual guides.
  if (touch_input_) {
    touch_input_->Draw(frame_def);
  }
}

auto Input::IsCursorVisible() const -> bool {
  assert(g_base->InLogicThread());
  if (!g_base->ui) {
    return false;
  }

  // Keeps mouse hidden to start with..
  if (mouse_move_count_ < 2) {
    return false;
  }
  bool val;

  // Show our cursor if any dialogs/windows are up or else if its been
  // moved very recently.
  if (g_base->ui->MainMenuVisible()) {
    val = (g_core->GetAppTimeMillisecs() - last_mouse_move_time_ < 5000);
  } else {
    val = (g_core->GetAppTimeMillisecs() - last_mouse_move_time_ < 1000);
  }
  return val;
}

void Input::LsInputDevices() {
  BA_PRECONDITION(g_base->InLogicThread());

  std::string out;

  std::string ind{"  "};
  int index{0};
  for (auto& device : input_devices_) {
    if (index != 0) {
      out += "\n";
    }
    out += std::to_string(index + 1) + ":\n";
    out += ind + "name: " + device->GetDeviceName() + "\n";
    out += ind + "index: " + std::to_string(device->index()) + "\n";
    out += (ind + "is-controller: " + std::to_string(device->IsController())
            + "\n");
    out += (ind + "is-sdl-controller: "
            + std::to_string(device->IsSDLController()) + "\n");
    out += (ind + "is-touch-screen: " + std::to_string(device->IsTouchScreen())
            + "\n");
    out += (ind + "is-remote-control: "
            + std::to_string(device->IsRemoteControl()) + "\n");
    out += (ind + "is-test-input: " + std::to_string(device->IsTestInput())
            + "\n");
    out +=
        (ind + "is-keyboard: " + std::to_string(device->IsKeyboard()) + "\n");
    out += (ind + "is-mfi-controller: "
            + std::to_string(device->IsMFiController()) + "\n");
    out += (ind + "is-local: " + std::to_string(device->IsLocal()) + "\n");
    out += (ind + "is-ui-only: " + std::to_string(device->IsUIOnly()) + "\n");
    out += (ind + "is-remote-app: " + std::to_string(device->IsRemoteApp())
            + "\n");

    out += ind + "attached-to: " + device->delegate().DescribeAttachedTo();

    ++index;
  }

  Log(LogLevel::kInfo, out);
}

}  // namespace ballistica::base
