#include "displayapp/screens/SmartAlarm.h"
#include "displayapp/screens/Symbols.h"
#include "displayapp/InfiniTimeTheme.h"
#include "components/alarm/SmartAlarmController.h"
#include "components/motor/MotorController.h"
#include "systemtask/SystemTask.h"

extern lv_font_t jetbrains_mono_bold_20;

using namespace Pinetime::Applications::Screens;

namespace {
  void ValueChangedHandler(void* userData) {
    auto* screen = static_cast<SmartAlarm*>(userData);
    screen->OnValueChanged();
  }
}

static void btnEventHandler(lv_obj_t* obj, lv_event_t event) {
  auto* screen = static_cast<SmartAlarm*>(obj->user_data);
  screen->OnButtonEvent(obj, event);
}

static void StopAlarmTaskCallback(lv_task_t* task) {
  auto* screen = static_cast<SmartAlarm*>(task->user_data);
  screen->StopAlerting();
}

SmartAlarm::SmartAlarm(Controllers::SmartAlarmController& smartAlarmController,
                       Controllers::Settings::ClockType clockType,
                       System::SystemTask& systemTask,
                       Controllers::MotorController& motorController)
  : smartAlarmController {smartAlarmController}, wakeLock(systemTask), motorController {motorController} {

  // Hour counter (top left, matching Alarm layout)
  hourCounter.Create();
  lv_obj_align(hourCounter.GetObject(), nullptr, LV_ALIGN_IN_TOP_LEFT, 0, 0);
  if (clockType == Controllers::Settings::ClockType::H12) {
    hourCounter.EnableTwelveHourMode();
    lblampm = lv_label_create(lv_scr_act(), nullptr);
    lv_obj_set_style_local_text_font(lblampm, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &jetbrains_mono_bold_20);
    lv_label_set_text_static(lblampm, "AM");
    lv_label_set_align(lblampm, LV_LABEL_ALIGN_CENTER);
    lv_obj_align(lblampm, lv_scr_act(), LV_ALIGN_CENTER, 0, 30);
  }
  hourCounter.SetValue(smartAlarmController.Hours());
  hourCounter.SetValueChangedEventCallback(this, ValueChangedHandler);

  // Minute counter (top right)
  minuteCounter.Create();
  lv_obj_align(minuteCounter.GetObject(), nullptr, LV_ALIGN_IN_TOP_RIGHT, 0, 0);
  minuteCounter.SetValue(smartAlarmController.Minutes());
  minuteCounter.SetValueChangedEventCallback(this, ValueChangedHandler);

  // Colon (centered, matching Alarm layout)
  lv_obj_t* colonLabel = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_text_font(colonLabel, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &jetbrains_mono_76);
  lv_label_set_text_static(colonLabel, ":");
  lv_obj_align(colonLabel, lv_scr_act(), LV_ALIGN_CENTER, 0, -29);

  // Stop button (hidden until alerting, full-width at bottom)
  btnStop = lv_btn_create(lv_scr_act(), nullptr);
  btnStop->user_data = this;
  lv_obj_set_event_cb(btnStop, btnEventHandler);
  lv_obj_set_size(btnStop, 240, 70);
  lv_obj_align(btnStop, lv_scr_act(), LV_ALIGN_IN_BOTTOM_LEFT, 0, 0);
  lv_obj_set_style_local_bg_color(btnStop, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_RED);
  txtStop = lv_label_create(btnStop, nullptr);
  lv_label_set_text_static(txtStop, Symbols::stop);
  lv_obj_set_hidden(btnStop, true);

  static constexpr lv_color_t bgColor = Colors::bgAlt;

  // Enable switch (centered at bottom)
  enableSwitch = lv_switch_create(lv_scr_act(), nullptr);
  enableSwitch->user_data = this;
  lv_obj_set_event_cb(enableSwitch, btnEventHandler);
  lv_obj_set_size(enableSwitch, 100, 50);
  lv_obj_align(enableSwitch, lv_scr_act(), LV_ALIGN_IN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_local_bg_color(enableSwitch, LV_SWITCH_PART_BG, LV_STATE_DEFAULT, bgColor);

  // Sleep phase label (above bottom buttons)
  lblPhase = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_text_color(lblPhase, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, Colors::lightGray);
  lv_label_set_text_static(lblPhase, "");
  lv_obj_align(lblPhase, lv_scr_act(), LV_ALIGN_IN_BOTTOM_MID, 0, -55);

  UpdateAlarmTime();

  if (smartAlarmController.IsAlerting()) {
    SetAlerting();
  } else {
    SetSwitchState(LV_ANIM_OFF);
  }

  UpdatePhaseLabel();

  taskRefresh = lv_task_create(RefreshTaskCallback, 1000, LV_TASK_PRIO_MID, this);
}

SmartAlarm::~SmartAlarm() {
  if (smartAlarmController.IsAlerting()) {
    StopAlerting();
  }
  if (taskRefresh != nullptr) {
    lv_task_del(taskRefresh);
  }
  lv_obj_clean(lv_scr_act());
  smartAlarmController.SaveSettings();
}

void SmartAlarm::Refresh() {
  UpdatePhaseLabel();
}

void SmartAlarm::UpdatePhaseLabel() {
  if (smartAlarmController.IsInWindow()) {
    using Phase = Controllers::SmartAlarmController::SleepPhase;
    switch (smartAlarmController.CurrentPhase()) {
      case Phase::Light:
        lv_label_set_text_static(lblPhase, "Light Sleep");
        lv_obj_set_style_local_text_color(lblPhase, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, Colors::highlight);
        break;
      case Phase::Deep:
        lv_label_set_text_static(lblPhase, "Deep Sleep");
        lv_obj_set_style_local_text_color(lblPhase, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, Colors::blue);
        break;
      case Phase::REM:
        lv_label_set_text_static(lblPhase, "REM Sleep");
        lv_obj_set_style_local_text_color(lblPhase, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, Colors::orange);
        break;
      default:
        lv_label_set_text_static(lblPhase, "Monitoring...");
        lv_obj_set_style_local_text_color(lblPhase, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, Colors::lightGray);
        break;
    }
  } else if (smartAlarmController.IsEnabled()) {
    lv_label_set_text_static(lblPhase, "Armed - HR active");
    lv_obj_set_style_local_text_color(lblPhase, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, Colors::highlight);
  } else {
    lv_label_set_text_static(lblPhase, "");
  }
  lv_obj_align(lblPhase, lv_scr_act(), LV_ALIGN_IN_BOTTOM_MID, 0, -55);
}

void SmartAlarm::OnButtonEvent(lv_obj_t* obj, lv_event_t event) {
  if (event != LV_EVENT_CLICKED) {
    return;
  }

  if (obj == btnStop) {
    StopAlerting();
    return;
  }

  if (obj == enableSwitch) {
    if (lv_switch_get_state(enableSwitch)) {
      smartAlarmController.ScheduleAlarm();
    } else {
      smartAlarmController.DisableAlarm();
    }
    return;
  }
}

bool SmartAlarm::OnButtonPushed() {
  if (smartAlarmController.IsAlerting()) {
    StopAlerting();
    return true;
  }
  return false;
}

bool SmartAlarm::OnTouchEvent(TouchEvents event) {
  return smartAlarmController.IsAlerting() && event == TouchEvents::SwipeDown;
}

void SmartAlarm::OnValueChanged() {
  DisableAlarm();
  UpdateAlarmTime();
}

void SmartAlarm::UpdateAlarmTime() {
  if (lblampm != nullptr) {
    if (hourCounter.GetValue() >= 12) {
      lv_label_set_text_static(lblampm, "PM");
    } else {
      lv_label_set_text_static(lblampm, "AM");
    }
  }
  smartAlarmController.SetAlarmTime(hourCounter.GetValue(), minuteCounter.GetValue());
}

void SmartAlarm::SetAlerting() {
  lv_obj_set_hidden(enableSwitch, true);
  lv_obj_set_hidden(lblPhase, true);
  hourCounter.HideControls();
  minuteCounter.HideControls();
  lv_obj_set_hidden(btnStop, false);
  taskStopAlarm = lv_task_create(StopAlarmTaskCallback, pdMS_TO_TICKS(60 * 1000), LV_TASK_PRIO_MID, this);
  motorController.StartRinging();
  wakeLock.Lock();
}

void SmartAlarm::StopAlerting() {
  smartAlarmController.StopAlerting();
  motorController.StopRinging();
  SetSwitchState(LV_ANIM_OFF);
  if (taskStopAlarm != nullptr) {
    lv_task_del(taskStopAlarm);
    taskStopAlarm = nullptr;
  }
  wakeLock.Release();
  lv_obj_set_hidden(btnStop, true);
  hourCounter.ShowControls();
  minuteCounter.ShowControls();
  lv_obj_set_hidden(enableSwitch, false);
  lv_obj_set_hidden(lblPhase, false);
}

void SmartAlarm::SetSwitchState(lv_anim_enable_t anim) {
  if (smartAlarmController.IsEnabled()) {
    lv_switch_on(enableSwitch, anim);
  } else {
    lv_switch_off(enableSwitch, anim);
  }
}

void SmartAlarm::DisableAlarm() {
  if (smartAlarmController.IsEnabled()) {
    smartAlarmController.DisableAlarm();
    lv_switch_off(enableSwitch, LV_ANIM_ON);
  }
}

