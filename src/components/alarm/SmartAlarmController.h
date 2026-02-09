#pragma once

#include <cstdint>
#include <FreeRTOS.h>
#include <timers.h>

namespace Pinetime {
  namespace System {
    class SystemTask;
  }

  namespace Controllers {
    class FS;
    class DateTime;
    class HeartRateLogger;
    class Settings;

    class SmartAlarmController {
    public:
      enum class SleepPhase : uint8_t { Unknown, Light, Deep, REM };

      SmartAlarmController(Controllers::DateTime& dateTime,
                           Controllers::FS& fs,
                           Controllers::HeartRateLogger& hrLogger,
                           Controllers::Settings& settings);

      void Init(System::SystemTask* systemTask);

      void SetAlarmTime(uint8_t hours, uint8_t minutes);
      void SetEnabled(bool enabled);
      void ScheduleAlarm();
      void DisableAlarm();
      void StopAlerting();

      uint8_t Hours() const;
      uint8_t Minutes() const;
      bool IsEnabled() const;
      bool IsAlerting() const;
      bool IsInWindow() const;
      SleepPhase CurrentPhase() const;

      void CheckSleepPhase();
      void OnWindowStart();
      void OnAlarmDeadline();

      void SaveSettings();

    private:
      struct AlarmSettings {
        uint8_t version = 1;
        uint8_t hours = 7;
        uint8_t minutes = 0;
        uint8_t padding = 0; // reserved for file format compatibility
        bool enabled = false;
      };

      static constexpr const char* filePath = "/.system/smartalarm.dat";
      static constexpr uint8_t windowMinutes = 30;
      static constexpr uint8_t requiredLightSleepChecks = 2;

      AlarmSettings alarmSettings;
      bool alerting = false;
      bool inWindow = false;
      SleepPhase currentPhase = SleepPhase::Unknown;
      uint8_t consecutiveLightChecks = 0;
      SleepPhase previousPhase = SleepPhase::Unknown;
      uint16_t savedBackgroundInterval = 0;
      bool settingsChanged = false;

      Controllers::DateTime& dateTime;
      Controllers::FS& fs;
      Controllers::HeartRateLogger& hrLogger;
      Controllers::Settings& settings;
      System::SystemTask* systemTask = nullptr;

      TimerHandle_t windowStartTimer = nullptr;
      TimerHandle_t alarmDeadlineTimer = nullptr;
      TimerHandle_t phaseCheckTimer = nullptr;

      SleepPhase AnalyzeSleepPhase();
      void TriggerWake();
      void StopTimers();
      void EnableBackgroundHR();
      void RestoreBackgroundHR();

      void LoadSettingsFromFile();
      void SaveSettingsToFile();

      static void WindowStartCallback(TimerHandle_t timer);
      static void AlarmDeadlineCallback(TimerHandle_t timer);
      static void PhaseCheckCallback(TimerHandle_t timer);
    };
  }
}
