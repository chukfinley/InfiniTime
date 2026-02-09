#include "displayapp/screens/HeartRateLog.h"
#include "components/heartrate/HeartRateController.h"
#include "components/heartrate/HeartRateLogger.h"
#include "systemtask/SystemTask.h"
#include "displayapp/InfiniTimeTheme.h"

using namespace Pinetime::Applications::Screens;

namespace {
  void StartStopEventHandler(lv_obj_t* obj, lv_event_t event) {
    auto* screen = static_cast<HeartRateLog*>(obj->user_data);
    screen->OnStartStopEvent(event);
  }
}

HeartRateLog::HeartRateLog(Controllers::HeartRateController& heartRateController,
                           Controllers::HeartRateLogger& heartRateLogger,
                           System::SystemTask& systemTask)
  : heartRateController {heartRateController}, heartRateLogger {heartRateLogger}, systemTask {systemTask} {

  // Title
  labelTitle = lv_label_create(lv_scr_act(), nullptr);
  lv_label_set_text_static(labelTitle, "\xEF\x88\x9E HR Log");
  lv_obj_align(labelTitle, nullptr, LV_ALIGN_IN_TOP_LEFT, 10, 4);

  // Current HR value
  labelCurrentHr = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_text_color(labelCurrentHr, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, Colors::highlight);
  lv_label_set_text_static(labelCurrentHr, "-- bpm");
  lv_obj_align(labelCurrentHr, nullptr, LV_ALIGN_IN_TOP_RIGHT, -10, 4);

  // Chart
  chart = lv_chart_create(lv_scr_act(), nullptr);
  lv_obj_set_size(chart, 220, 120);
  lv_obj_align(chart, nullptr, LV_ALIGN_IN_TOP_MID, 0, 35);
  lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
  lv_chart_set_range(chart, 40, 140);
  lv_chart_set_update_mode(chart, LV_CHART_UPDATE_MODE_SHIFT);
  lv_chart_set_point_count(chart, 120); // Show up to 120 data points (2 hours at 1/min)
  lv_obj_set_style_local_bg_color(chart, LV_CHART_PART_BG, LV_STATE_DEFAULT, LV_COLOR_BLACK);
  lv_obj_set_style_local_border_color(chart, LV_CHART_PART_BG, LV_STATE_DEFAULT, Colors::gray);
  lv_obj_set_style_local_line_width(chart, LV_CHART_PART_SERIES, LV_STATE_DEFAULT, 2);
  lv_obj_set_style_local_size(chart, LV_CHART_PART_SERIES, LV_STATE_DEFAULT, 0); // No point dots

  serHr = lv_chart_add_series(chart, LV_COLOR_RED);
  lv_chart_init_points(chart, serHr, LV_CHART_POINT_DEF);

  // Stats line
  labelStats = lv_label_create(lv_scr_act(), nullptr);
  lv_label_set_text_static(labelStats, "No data recorded");
  lv_obj_set_style_local_text_color(labelStats, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, Colors::lightGray);
  lv_obj_align(labelStats, nullptr, LV_ALIGN_IN_BOTTOM_MID, 0, -50);

  // Start/Stop button
  btnStartStop = lv_btn_create(lv_scr_act(), nullptr);
  btnStartStop->user_data = this;
  lv_obj_set_event_cb(btnStartStop, StartStopEventHandler);
  lv_obj_set_size(btnStartStop, 140, 40);
  lv_obj_align(btnStartStop, nullptr, LV_ALIGN_IN_BOTTOM_MID, 0, -4);

  txtStartStop = lv_label_create(btnStartStop, nullptr);

  isRunning = heartRateController.State() != Controllers::HeartRateController::States::Stopped;
  UpdateStartStopButton();
  UpdateChart();

  taskRefresh = lv_task_create(RefreshTaskCallback, 1000, LV_TASK_PRIO_MID, this);
}

HeartRateLog::~HeartRateLog() {
  lv_task_del(taskRefresh);
  lv_obj_clean(lv_scr_act());
}

void HeartRateLog::Refresh() {
  auto state = heartRateController.State();
  bool nowRunning = state != Controllers::HeartRateController::States::Stopped;

  if (nowRunning != isRunning) {
    isRunning = nowRunning;
    UpdateStartStopButton();
  }

  // Update current HR display
  if (state == Controllers::HeartRateController::States::Running && heartRateController.HeartRate() > 0) {
    lv_label_set_text_fmt(labelCurrentHr, "%d bpm", heartRateController.HeartRate());
  } else {
    lv_label_set_text_static(labelCurrentHr, "-- bpm");
  }
  lv_obj_align(labelCurrentHr, nullptr, LV_ALIGN_IN_TOP_RIGHT, -10, 4);

  UpdateChart();
}

void HeartRateLog::UpdateChart() {
  static constexpr uint16_t chartPoints = 120;
  Controllers::HeartRateLogger::Entry entries[chartPoints];
  uint16_t count = heartRateLogger.GetRecentEntries(entries, chartPoints);

  if (count == 0) {
    lv_label_set_text_static(labelStats, "No data recorded");
    lv_obj_align(labelStats, nullptr, LV_ALIGN_IN_BOTTOM_MID, 0, -50);
    return;
  }

  // Clear and refill chart
  lv_chart_init_points(chart, serHr, LV_CHART_POINT_DEF);

  uint8_t minHr = 255;
  uint8_t maxHr = 0;
  uint32_t sumHr = 0;

  for (uint16_t i = 0; i < count; i++) {
    lv_chart_set_next(chart, serHr, entries[i].bpm);
    if (entries[i].bpm < minHr) {
      minHr = entries[i].bpm;
    }
    if (entries[i].bpm > maxHr) {
      maxHr = entries[i].bpm;
    }
    sumHr += entries[i].bpm;
  }

  uint8_t avgHr = static_cast<uint8_t>(sumHr / count);

  lv_label_set_text_fmt(labelStats, "Min:%d  Avg:%d  Max:%d  (%d)", minHr, avgHr, maxHr, count);
  lv_obj_align(labelStats, nullptr, LV_ALIGN_IN_BOTTOM_MID, 0, -50);

  lv_chart_refresh(chart);
}

void HeartRateLog::OnStartStopEvent(lv_event_t event) {
  if (event != LV_EVENT_CLICKED) {
    return;
  }

  if (isRunning) {
    heartRateController.Disable();
    isRunning = false;
  } else {
    heartRateController.Enable();
    isRunning = true;
  }
  UpdateStartStopButton();
}

void HeartRateLog::UpdateStartStopButton() {
  if (isRunning) {
    lv_label_set_text_static(txtStartStop, "Stop Logging");
    lv_obj_set_style_local_bg_color(btnStartStop, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_RED);
  } else {
    lv_label_set_text_static(txtStartStop, "Start Logging");
    lv_obj_set_style_local_bg_color(btnStartStop, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, Colors::highlight);
  }
}
