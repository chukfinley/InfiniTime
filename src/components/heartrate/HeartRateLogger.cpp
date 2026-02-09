#include "components/heartrate/HeartRateLogger.h"
#include "components/fs/FS.h"
#include "components/datetime/DateTimeController.h"

#include <cstring>

using namespace Pinetime::Controllers;

HeartRateLogger::HeartRateLogger(Controllers::FS& fs, Controllers::DateTime& dateTime) : fs {fs}, dateTime {dateTime} {
}

void HeartRateLogger::Init() {
  LoadHeader();
}

void HeartRateLogger::LoadHeader() {
  lfs_file_t file;
  if (fs.FileOpen(&file, filePath, LFS_O_RDONLY) == LFS_ERR_OK) {
    FileHeader readHeader;
    if (fs.FileRead(&file, reinterpret_cast<uint8_t*>(&readHeader), sizeof(FileHeader)) == sizeof(FileHeader)) {
      if (readHeader.version == 1 && readHeader.writeIndex < maxEntries && readHeader.count <= maxEntries) {
        header = readHeader;
      }
    }
    fs.FileClose(&file);
  }
}

void HeartRateLogger::SaveHeader() {
  lfs_file_t file;
  fs.DirCreate(dirPath);
  if (fs.FileOpen(&file, filePath, LFS_O_RDWR | LFS_O_CREAT) == LFS_ERR_OK) {
    fs.FileWrite(&file, reinterpret_cast<const uint8_t*>(&header), sizeof(FileHeader));
    fs.FileClose(&file);
  }
}

void HeartRateLogger::AddMeasurement(uint8_t bpm) {
  if (bpm == 0) {
    return;
  }

  auto now = std::chrono::system_clock::to_time_t(dateTime.CurrentDateTime());
  auto nowSeconds = static_cast<uint32_t>(now);

  // Throttle to at most once per 30 seconds
  if (lastLogTimestamp != 0 && (nowSeconds - lastLogTimestamp) < 30) {
    return;
  }
  lastLogTimestamp = nowSeconds;

  Entry entry;
  entry.timestamp = nowSeconds;
  entry.bpm = bpm;

  WriteEntry(entry);

  header.writeIndex = (header.writeIndex + 1) % maxEntries;
  if (header.count < maxEntries) {
    header.count++;
  }
  SaveHeader();
}

void HeartRateLogger::WriteEntry(const Entry& entry) {
  lfs_file_t file;
  fs.DirCreate(dirPath);
  if (fs.FileOpen(&file, filePath, LFS_O_RDWR | LFS_O_CREAT) == LFS_ERR_OK) {
    lfs_off_t offset = sizeof(FileHeader) + (header.writeIndex * sizeof(Entry));
    fs.FileSeek(&file, offset);
    fs.FileWrite(&file, reinterpret_cast<const uint8_t*>(&entry), sizeof(Entry));
    fs.FileClose(&file);
  }
}

uint16_t HeartRateLogger::GetRecentEntries(Entry* buffer, uint16_t maxCount) const {
  if (header.count == 0 || maxCount == 0) {
    return 0;
  }

  uint16_t toRead = (maxCount < header.count) ? maxCount : header.count;

  lfs_file_t file;
  if (fs.FileOpen(const_cast<lfs_file_t*>(&file), filePath, LFS_O_RDONLY) != LFS_ERR_OK) {
    return 0;
  }

  // Read entries in chronological order (oldest first)
  // The oldest entry is at writeIndex (if buffer is full) or at 0 (if not full)
  uint16_t startIndex;
  if (header.count < maxEntries) {
    // Buffer not full yet, oldest entry is at max(0, count - toRead)
    startIndex = header.count - toRead;
  } else {
    // Buffer is full, oldest is at writeIndex
    // We want the most recent toRead entries, so start at (writeIndex - toRead + maxEntries) % maxEntries
    startIndex = (header.writeIndex + maxEntries - toRead) % maxEntries;
  }

  for (uint16_t i = 0; i < toRead; i++) {
    uint16_t idx = (startIndex + i) % maxEntries;
    lfs_off_t offset = sizeof(FileHeader) + (idx * sizeof(Entry));
    const_cast<FS&>(fs).FileSeek(const_cast<lfs_file_t*>(&file), offset);
    const_cast<FS&>(fs).FileRead(const_cast<lfs_file_t*>(&file), reinterpret_cast<uint8_t*>(&buffer[i]), sizeof(Entry));
  }

  const_cast<FS&>(fs).FileClose(const_cast<lfs_file_t*>(&file));
  return toRead;
}

uint16_t HeartRateLogger::GetEntryCount() const {
  return header.count;
}

void HeartRateLogger::Clear() {
  header.writeIndex = 0;
  header.count = 0;
  lastLogTimestamp = 0;
  fs.FileDelete(filePath);
  SaveHeader();
}
