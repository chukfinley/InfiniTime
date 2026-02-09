#pragma once

#include "displayapp/screens/Screen.h"
#include "displayapp/apps/Apps.h"
#include "displayapp/Controllers.h"
#include "displayapp/widgets/Counter.h"
#include "components/settings/Settings.h"
#include "systemtask/WakeLock.h"
#include "Symbols.h"
#include <lvgl/lvgl.h>

extern lv_font_t jetbrains_mono_76;

namespace Pinetime {
  namespace Controllers {
    class SmartAlarmController;
    class MotorController;
  }

  namespace System {
    class SystemTask;
  }

  namespace Applications {
    namespace Screens {
      class SmartAlarm : public Screen {
      public:
        SmartAlarm(Controllers::SmartAlarmController& smartAlarmController,
                   Controllers::Settings::ClockType clockType,
                   System::SystemTask& systemTask,
                   Controllers::MotorController& motorController);
        ~SmartAlarm() override;

        void SetAlerting();
        void StopAlerting();
        void OnButtonEvent(lv_obj_t* obj, lv_event_t event);
        bool OnButtonPushed() override;
        bool OnTouchEvent(TouchEvents event) override;
        void OnValueChanged();
        void Refresh() override;

      private:
        Controllers::SmartAlarmController& smartAlarmController;
        System::WakeLock wakeLock;
        Controllers::MotorController& motorController;

        Widgets::Counter hourCounter = Widgets::Counter(0, 23, jetbrains_mono_76);
        Widgets::Counter minuteCounter = Widgets::Counter(0, 59, jetbrains_mono_76);

        lv_obj_t* btnStop = nullptr;
        lv_obj_t* txtStop = nullptr;
        lv_obj_t* enableSwitch = nullptr;
        lv_obj_t* lblPhase = nullptr;
        lv_obj_t* lblampm = nullptr;
        lv_task_t* taskRefresh = nullptr;
        lv_task_t* taskStopAlarm = nullptr;

        void UpdateAlarmTime();
        void SetSwitchState(lv_anim_enable_t anim);
        void DisableAlarm();
        void UpdatePhaseLabel();
      };
    }

    template <>
    struct AppTraits<Apps::SmartAlarm> {
      static constexpr Apps app = Apps::SmartAlarm;
      static constexpr const char* icon = Screens::Symbols::moon;

      static Screens::Screen* Create(AppControllers& controllers) {
        return new Screens::SmartAlarm(controllers.smartAlarmController,
                                       controllers.settingsController.GetClockType(),
                                       *controllers.systemTask,
                                       controllers.motorController);
      };

      static bool IsAvailable(Pinetime::Controllers::FS& /*filesystem*/) {
        return true;
      };
    };
  }
}
