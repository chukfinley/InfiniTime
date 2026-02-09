#pragma once

#include <cstdint>

namespace Pinetime {
  namespace Controllers {
    class FS;
    class DateTime;
  }

  namespace Controllers {
    class HeartRateLogger {
    public:
      struct Entry {
        uint32_t timestamp;
        uint8_t bpm;
      };

      HeartRateLogger(Controllers::FS& fs, Controllers::DateTime& dateTime);

      void Init();
      void AddMeasurement(uint8_t bpm);
      uint16_t GetRecentEntries(Entry* buffer, uint16_t maxCount) const;
      uint16_t GetEntryCount() const;
      void Clear();

      static constexpr uint16_t maxEntries = 480;

    private:
      static constexpr const char* dirPath = "/.system";
      static constexpr const char* filePath = "/.system/hrlog.dat";

      struct FileHeader {
        uint8_t version = 1;
        uint16_t writeIndex = 0;
        uint16_t count = 0;
      };

      Controllers::FS& fs;
      Controllers::DateTime& dateTime;
      FileHeader header;
      uint32_t lastLogTimestamp = 0;

      void LoadHeader();
      void SaveHeader();
      void WriteEntry(const Entry& entry);
    };
  }
}
