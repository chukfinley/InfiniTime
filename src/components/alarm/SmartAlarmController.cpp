#include "components/alarm/SmartAlarmController.h"
#include "components/heartrate/HeartRateLogger.h"
#include "components/datetime/DateTimeController.h"
#include "components/settings/Settings.h"
#include "components/fs/FS.h"
#include "systemtask/SystemTask.h"

#include <chrono>
#include <cmath>
#include <cstring>
#include <libraries/log/nrf_log.h>

using namespace Pinetime::Controllers;

SmartAlarmController::SmartAlarmController(Controllers::DateTime& dateTime,
                                           Controllers::FS& fs,
                                           Controllers::HeartRateLogger& hrLogger,
                                           Controllers::Settings& settings)
  : dateTime {dateTime}, fs {fs}, hrLogger {hrLogger}, settings {settings} {
}

void SmartAlarmController::WindowStartCallback(TimerHandle_t timer) {
  auto* controller = static_cast<SmartAlarmController*>(pvTimerGetTimerID(timer));
  controller->OnWindowStart();
}

void SmartAlarmController::AlarmDeadlineCallback(TimerHandle_t timer) {
  auto* controller = static_cast<SmartAlarmController*>(pvTimerGetTimerID(timer));
  controller->OnAlarmDeadline();
}

void SmartAlarmController::PhaseCheckCallback(TimerHandle_t timer) {
  auto* controller = static_cast<SmartAlarmController*>(pvTimerGetTimerID(timer));
  controller->CheckSleepPhase();
}

void SmartAlarmController::Init(System::SystemTask* systemTask) {
  this->systemTask = systemTask;
  windowStartTimer = xTimerCreate("SmartWin", 1, pdFALSE, this, WindowStartCallback);
  alarmDeadlineTimer = xTimerCreate("SmartDead", 1, pdFALSE, this, AlarmDeadlineCallback);
  phaseCheckTimer = xTimerCreate("SmartChk", pdMS_TO_TICKS(60 * 1000), pdTRUE, this, PhaseCheckCallback);

  LoadSettingsFromFile();
  if (alarmSettings.enabled) {
    NRF_LOG_INFO("[SmartAlarm] Loaded enabled alarm, scheduling");
    ScheduleAlarm();
  }
}

void SmartAlarmController::SetAlarmTime(uint8_t hours, uint8_t minutes) {
  if (alarmSettings.hours != hours || alarmSettings.minutes != minutes) {
    alarmSettings.hours = hours;
    alarmSettings.minutes = minutes;
    settingsChanged = true;
  }
}

void SmartAlarmController::SetEnabled(bool enabled) {
  if (alarmSettings.enabled != enabled) {
    alarmSettings.enabled = enabled;
    settingsChanged = true;
  }
}

void SmartAlarmController::ScheduleAlarm() {
  StopTimers();

  auto now = dateTime.CurrentDateTime();
  time_t ttNow = std::chrono::system_clock::to_time_t(std::chrono::time_point_cast<std::chrono::system_clock::duration>(now));
  tm* tmAlarm = std::localtime(&ttNow);

  // Set alarm time
  tmAlarm->tm_hour = alarmSettings.hours;
  tmAlarm->tm_min = alarmSettings.minutes;
  tmAlarm->tm_sec = 0;
  tmAlarm->tm_isdst = -1;

  time_t alarmEpoch = std::mktime(tmAlarm);

  // If alarm time has already passed today, schedule for tomorrow
  if (alarmEpoch <= ttNow) {
    alarmEpoch += 24 * 60 * 60;
  }

  // Calculate seconds to alarm and to window start
  auto secondsToAlarm = static_cast<int32_t>(alarmEpoch - ttNow);
  auto secondsToWindow = secondsToAlarm - (windowMinutes * 60);

  if (secondsToWindow < 0) {
    secondsToWindow = 0;
  }

  // Schedule the hard deadline timer (fires at exact alarm time)
  if (secondsToAlarm > 0) {
    xTimerChangePeriod(alarmDeadlineTimer, static_cast<TickType_t>(secondsToAlarm) * configTICK_RATE_HZ, 0);
    xTimerStart(alarmDeadlineTimer, 0);
  }

  // Schedule the window start timer
  if (secondsToWindow > 0) {
    xTimerChangePeriod(windowStartTimer, static_cast<TickType_t>(secondsToWindow) * configTICK_RATE_HZ, 0);
    xTimerStart(windowStartTimer, 0);
  } else {
    // Window has already started (alarm is within windowMinutes from now)
    OnWindowStart();
  }

  if (!alarmSettings.enabled) {
    alarmSettings.enabled = true;
    settingsChanged = true;
  }

  // Start background HR immediately at 60s interval for sleep tracking
  EnableBackgroundHR();

  NRF_LOG_INFO("[SmartAlarm] Scheduled: alarm in %ds, window in %ds", secondsToAlarm, secondsToWindow);
}

void SmartAlarmController::DisableAlarm() {
  StopTimers();
  RestoreBackgroundHR();
  inWindow = false;
  currentPhase = SleepPhase::Unknown;
  consecutiveLightChecks = 0;
  previousPhase = SleepPhase::Unknown;

  if (alarmSettings.enabled) {
    alarmSettings.enabled = false;
    settingsChanged = true;
  }
}

void SmartAlarmController::OnWindowStart() {
  NRF_LOG_INFO("[SmartAlarm] Wake window started");
  inWindow = true;
  consecutiveLightChecks = 0;
  previousPhase = SleepPhase::Unknown;
  currentPhase = SleepPhase::Unknown;

  // HR already running at 60s since alarm was armed
  // Start periodic phase checking (every 60s)
  xTimerStart(phaseCheckTimer, 0);
}

void SmartAlarmController::OnAlarmDeadline() {
  NRF_LOG_INFO("[SmartAlarm] Hard alarm deadline reached");
  xTimerStop(phaseCheckTimer, 0);
  inWindow = false;

  if (!alerting) {
    TriggerWake();
  }
}

void SmartAlarmController::CheckSleepPhase() {
  if (alerting || !inWindow) {
    return;
  }

  previousPhase = currentPhase;
  currentPhase = AnalyzeSleepPhase();

  NRF_LOG_INFO("[SmartAlarm] Phase check: %d (prev: %d), light count: %d",
               static_cast<int>(currentPhase),
               static_cast<int>(previousPhase),
               consecutiveLightChecks);

  if (currentPhase == SleepPhase::Light) {
    consecutiveLightChecks++;

    // Wake if transitioning into light sleep from deep/REM (end of cycle)
    // or if we've been in light sleep for enough consecutive checks
    bool transitionToLight = (previousPhase == SleepPhase::Deep || previousPhase == SleepPhase::REM);
    bool sustainedLight = (consecutiveLightChecks >= requiredLightSleepChecks);

    if (transitionToLight || sustainedLight) {
      NRF_LOG_INFO("[SmartAlarm] Light sleep detected, waking user");
      xTimerStop(phaseCheckTimer, 0);
      xTimerStop(alarmDeadlineTimer, 0);
      inWindow = false;
      TriggerWake();
    }
  } else {
    consecutiveLightChecks = 0;
  }
}

SmartAlarmController::SleepPhase SmartAlarmController::AnalyzeSleepPhase() {
  // Need at least 5 minutes of data for meaningful analysis
  static constexpr uint16_t analysisWindow = 10;
  static constexpr uint16_t minEntries = 5;

  HeartRateLogger::Entry entries[analysisWindow];
  uint16_t count = hrLogger.GetRecentEntries(entries, analysisWindow);

  if (count < minEntries) {
    return SleepPhase::Unknown;
  }

  // Calculate mean HR
  uint32_t sum = 0;
  for (uint16_t i = 0; i < count; i++) {
    sum += entries[i].bpm;
  }
  float mean = static_cast<float>(sum) / static_cast<float>(count);

  // Calculate HR variability (standard deviation)
  float varianceSum = 0;
  for (uint16_t i = 0; i < count; i++) {
    float diff = static_cast<float>(entries[i].bpm) - mean;
    varianceSum += diff * diff;
  }
  float stddev = std::sqrt(varianceSum / static_cast<float>(count));

  // Calculate trend: compare first half vs second half
  uint16_t half = count / 2;
  uint32_t firstHalfSum = 0;
  uint32_t secondHalfSum = 0;
  for (uint16_t i = 0; i < half; i++) {
    firstHalfSum += entries[i].bpm;
  }
  for (uint16_t i = half; i < count; i++) {
    secondHalfSum += entries[i].bpm;
  }
  float firstHalfMean = static_cast<float>(firstHalfSum) / static_cast<float>(half);
  float secondHalfMean = static_cast<float>(secondHalfSum) / static_cast<float>(count - half);
  float trend = secondHalfMean - firstHalfMean;

  // Calculate baseline from all available data (up to full buffer)
  // For simplicity, use the overall mean as an approximation
  // In a full session, the baseline stabilizes to the sleeper's average
  float baseline = mean;

  // Use all logged data for a better baseline if we have more than the analysis window
  if (hrLogger.GetEntryCount() > analysisWindow) {
    static constexpr uint16_t baselineWindow = 60; // up to 1 hour
    HeartRateLogger::Entry baselineEntries[baselineWindow];
    uint16_t baselineCount = hrLogger.GetRecentEntries(baselineEntries, baselineWindow);
    if (baselineCount > analysisWindow) {
      uint32_t baselineSum = 0;
      for (uint16_t i = 0; i < baselineCount; i++) {
        baselineSum += baselineEntries[i].bpm;
      }
      baseline = static_cast<float>(baselineSum) / static_cast<float>(baselineCount);
    }
  }

  // Classification
  // Deep sleep: HR well below baseline, very steady
  if (mean < (baseline - 6.0f) && stddev < 3.0f) {
    return SleepPhase::Deep;
  }

  // REM sleep: high variability, HR may be elevated
  if (stddev > 7.0f) {
    return SleepPhase::REM;
  }

  // Light sleep: HR near baseline or rising, moderate variability
  if (stddev >= 3.0f && stddev <= 7.0f) {
    return SleepPhase::Light;
  }

  // If HR is rising (transitioning out of deep sleep)
  if (trend > 2.0f && mean > (baseline - 6.0f)) {
    return SleepPhase::Light;
  }

  // Default: if low variability but not that far below baseline
  if (stddev < 3.0f && mean >= (baseline - 6.0f)) {
    return SleepPhase::Light;
  }

  return SleepPhase::Deep;
}

void SmartAlarmController::TriggerWake() {
  alerting = true;
  RestoreBackgroundHR();
  systemTask->PushMessage(System::Messages::SetOffSmartAlarm);
}

void SmartAlarmController::StopAlerting() {
  alerting = false;
  inWindow = false;
  currentPhase = SleepPhase::Unknown;
  consecutiveLightChecks = 0;
  previousPhase = SleepPhase::Unknown;

  // Disable (one-shot alarm)
  alarmSettings.enabled = false;
  settingsChanged = true;
}

void SmartAlarmController::StopTimers() {
  if (windowStartTimer != nullptr) {
    xTimerStop(windowStartTimer, 0);
  }
  if (alarmDeadlineTimer != nullptr) {
    xTimerStop(alarmDeadlineTimer, 0);
  }
  if (phaseCheckTimer != nullptr) {
    xTimerStop(phaseCheckTimer, 0);
  }
}

void SmartAlarmController::EnableBackgroundHR() {
  // Set to 60s for sleep tracking
  if (savedBackgroundInterval == 0) {
    auto currentInterval = settings.GetHeartRateBackgroundMeasurementInterval();
    savedBackgroundInterval = currentInterval.has_value() ? currentInterval.value() : std::numeric_limits<uint16_t>::max();
  }
  settings.SetHeartRateBackgroundMeasurementInterval(60);
}

void SmartAlarmController::RestoreBackgroundHR() {
  if (savedBackgroundInterval == std::numeric_limits<uint16_t>::max()) {
    settings.SetHeartRateBackgroundMeasurementInterval(std::nullopt);
  } else if (savedBackgroundInterval != 0) {
    settings.SetHeartRateBackgroundMeasurementInterval(savedBackgroundInterval);
  }
  savedBackgroundInterval = 0;
}

uint8_t SmartAlarmController::Hours() const {
  return alarmSettings.hours;
}

uint8_t SmartAlarmController::Minutes() const {
  return alarmSettings.minutes;
}

bool SmartAlarmController::IsEnabled() const {
  return alarmSettings.enabled;
}

bool SmartAlarmController::IsAlerting() const {
  return alerting;
}

bool SmartAlarmController::IsInWindow() const {
  return inWindow;
}

SmartAlarmController::SleepPhase SmartAlarmController::CurrentPhase() const {
  return currentPhase;
}

void SmartAlarmController::SaveSettings() {
  if (settingsChanged) {
    SaveSettingsToFile();
    settingsChanged = false;
  }
}

void SmartAlarmController::LoadSettingsFromFile() {
  lfs_file_t file;
  if (fs.FileOpen(&file, filePath, LFS_O_RDONLY) != LFS_ERR_OK) {
    return;
  }

  AlarmSettings buffer;
  if (fs.FileRead(&file, reinterpret_cast<uint8_t*>(&buffer), sizeof(buffer)) == sizeof(buffer)) {
    if (buffer.version == 1) {
      alarmSettings = buffer;
    }
  }
  fs.FileClose(&file);
}

void SmartAlarmController::SaveSettingsToFile() {
  lfs_dir dir;
  if (fs.DirOpen("/.system", &dir) != LFS_ERR_OK) {
    fs.DirCreate("/.system");
  }
  fs.DirClose(&dir);

  lfs_file_t file;
  if (fs.FileOpen(&file, filePath, LFS_O_WRONLY | LFS_O_CREAT) != LFS_ERR_OK) {
    NRF_LOG_WARNING("[SmartAlarm] Failed to save settings");
    return;
  }

  fs.FileWrite(&file, reinterpret_cast<const uint8_t*>(&alarmSettings), sizeof(alarmSettings));
  fs.FileClose(&file);
}
