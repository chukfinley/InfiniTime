#pragma once

#include "displayapp/screens/Screen.h"
#include "displayapp/apps/Apps.h"
#include "displayapp/Controllers.h"
#include "Symbols.h"
#include <lvgl/lvgl.h>

namespace Pinetime {
  namespace Controllers {
    class HeartRateController;
    class HeartRateLogger;
  }

  namespace System {
    class SystemTask;
  }

  namespace Applications {
    namespace Screens {
      class HeartRateLog : public Screen {
      public:
        HeartRateLog(Controllers::HeartRateController& heartRateController,
                     Controllers::HeartRateLogger& heartRateLogger,
                     System::SystemTask& systemTask);
        ~HeartRateLog() override;

        void Refresh() override;
        void OnStartStopEvent(lv_event_t event);

      private:
        Controllers::HeartRateController& heartRateController;
        Controllers::HeartRateLogger& heartRateLogger;
        System::SystemTask& systemTask;

        lv_obj_t* chart;
        lv_chart_series_t* serHr;
        lv_obj_t* labelTitle;
        lv_obj_t* labelCurrentHr;
        lv_obj_t* labelStats;
        lv_obj_t* btnStartStop;
        lv_obj_t* txtStartStop;
        lv_task_t* taskRefresh;

        bool isRunning = false;

        void UpdateChart();
        void UpdateStartStopButton();
      };
    }

    template <>
    struct AppTraits<Apps::HeartRateLog> {
      static constexpr Apps app = Apps::HeartRateLog;
      static constexpr const char* icon = Screens::Symbols::heartBeat;

      static Screens::Screen* Create(AppControllers& controllers) {
        return new Screens::HeartRateLog(controllers.heartRateController, controllers.heartRateLogger, *controllers.systemTask);
      };

      static bool IsAvailable(Pinetime::Controllers::FS& /*filesystem*/) {
        return true;
      };
    };
  }
}
