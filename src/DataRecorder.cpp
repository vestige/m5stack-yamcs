#include "DataRecorder.h"

#include <M5Unified.h>
#include <SD.h>
#include <SPI.h>

namespace {

// path の親のフォルダを順に作る (/rec、/rec/gnss、/rec/gnss/20261030)
void makeParents(const char* path) {
  char dir[RecordFormat::kMaxPathLength];
  for (size_t i = 1; path[i] != '\0' && i < sizeof(dir); i++) {
    if (path[i] != '/') continue;
    memcpy(dir, path, i);
    dir[i] = '\0';
    if (!SD.exists(dir)) SD.mkdir(dir);
  }
}

}  // namespace

void DataRecorder::begin() {
  if (mount()) {
    updateFreeSpace();
    Serial.printf("[REC] microSD: %lluMB, free %uMB\n", SD.totalBytes() / (1024 * 1024), freeMB_);
    printContents();
  } else {
    Serial.println("[REC] microSD not found");
  }
}

bool DataRecorder::mount() {
  // M5Stack Core の microSD は、液晶と同じ SPI につながっている
  int cs = M5.getPin(m5::pin_name_t::sd_spi_cs);
  if (cs < 0) cs = GPIO_NUM_4;
  SPI.begin(M5.getPin(m5::pin_name_t::sd_spi_sclk), M5.getPin(m5::pin_name_t::sd_spi_miso),
            M5.getPin(m5::pin_name_t::sd_spi_mosi), cs);
  mounted_ = SD.begin(cs, SPI, 25000000) && SD.cardType() != CARD_NONE;
  state_ = mounted_ ? State::WaitingTime : State::NoCard;
  return mounted_;
}

void DataRecorder::record(const uint8_t* packet, size_t size) {
  if (!mounted_) return;
  RecordFormat::Source source;
  if (!RecordFormat::sourceOf(packet, size, &source)) return;
  uint64_t timeMs = RecordFormat::packetTimeMs(packet, size);
  if (timeMs == 0) {
    // 時刻のないデータは、あとで並べられないので記録しない
    if (state_ == State::Recording) state_ = State::WaitingTime;
    return;
  }
  if (state_ == State::WaitingTime) state_ = State::Recording;

  Buffer& buffer = buffers_[static_cast<size_t>(source)];
  char path[RecordFormat::kMaxPathLength];
  RecordFormat::filePath(source, static_cast<uint32_t>(timeMs / 1000), path, sizeof(path));
  // 1時間ごとにファイルを分ける。時が変わったり、貯める場所が足りなくなったりしたら、先に書く
  if (strcmp(buffer.path, path) != 0 || buffer.used + RecordFormat::kLengthSize + size > kBufferSize) {
    flush(buffer);
    strncpy(buffer.path, path, sizeof(buffer.path));
  }
  if (RecordFormat::kLengthSize + size > kBufferSize) return;
  RecordFormat::writeLength(size, buffer.data + buffer.used);
  memcpy(buffer.data + buffer.used + RecordFormat::kLengthSize, packet, size);
  buffer.used += RecordFormat::kLengthSize + size;
  records_++;
}

bool DataRecorder::flush(Buffer& buffer) {
  if (buffer.used == 0 || buffer.path[0] == '\0') return true;
  makeParents(buffer.path);
  File file = SD.open(buffer.path, FILE_APPEND);
  bool ok = file && file.write(buffer.data, buffer.used) == buffer.used;
  if (file) file.close();
  buffer.used = 0;
  if (!ok) {
    Serial.printf("[REC] write failed: %s\n", buffer.path);
    state_ = State::Error;
    mounted_ = false;
  }
  return ok;
}

void DataRecorder::flushAll() {
  for (Buffer& buffer : buffers_) {
    if (!flush(buffer)) return;
  }
}

void DataRecorder::poll(uint32_t nowMs) {
  if (!mounted_) {
    // microSD を挿し直したら、また記録を始める
    if (nowMs - lastRetryMs_ >= kRetryIntervalMs) {
      lastRetryMs_ = nowMs;
      SD.end();
      if (mount()) {
        updateFreeSpace();
        Serial.println("[REC] microSD mounted");
      }
    }
    return;
  }
  if (nowMs - lastFlushMs_ >= kFlushIntervalMs) {
    lastFlushMs_ = nowMs;
    flushAll();
  }
  if (mounted_ && nowMs - lastSpaceCheckMs_ >= kSpaceCheckIntervalMs) {
    lastSpaceCheckMs_ = nowMs;
    updateFreeSpace();
    // 空きが少なければ、古い記録から消す
    for (size_t i = 0; i < RecordFormat::kSourceCount && freeMB_ < kMinFreeMB; i++) {
      while (freeMB_ < kMinFreeMB && removeOldest(static_cast<RecordFormat::Source>(i))) updateFreeSpace();
    }
  }
}

void DataRecorder::updateFreeSpace() {
  uint64_t freeBytes = SD.totalBytes() - SD.usedBytes();
  uint64_t mb = freeBytes / (1024 * 1024);
  freeMB_ = mb > 0xFFFF ? 0xFFFF : static_cast<uint16_t>(mb);
}

// その機器の、いちばん古い日のフォルダの、いちばん古いファイルを消す。消せたら true
bool DataRecorder::removeOldest(RecordFormat::Source source) {
  char dirPath[RecordFormat::kMaxPathLength];
  RecordFormat::sourceDir(source, dirPath, sizeof(dirPath));
  File dir = SD.open(dirPath);
  if (!dir) return false;
  String oldestDay;
  for (File entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    String name = entry.name();
    if (entry.isDirectory() && (oldestDay.isEmpty() || name < oldestDay)) oldestDay = name;
  }
  dir.close();
  if (oldestDay.isEmpty()) return false;
  String dayPath = String(dirPath) + "/" + oldestDay;
  File day = SD.open(dayPath);
  String oldestFile;
  for (File entry = day.openNextFile(); entry; entry = day.openNextFile()) {
    String name = entry.name();
    if (!entry.isDirectory() && (oldestFile.isEmpty() || name < oldestFile)) oldestFile = name;
  }
  day.close();
  if (oldestFile.isEmpty()) return SD.rmdir(dayPath);
  Serial.printf("[REC] remove %s/%s\n", dayPath.c_str(), oldestFile.c_str());
  return SD.remove(dayPath + "/" + oldestFile);
}

// 起動のときに、すでにある記録の量を出す (再起動の前の記録が残っているかを、カードを抜かずに確かめる)
void DataRecorder::printContents() {
  for (size_t i = 0; i < RecordFormat::kSourceCount; i++) {
    auto source = static_cast<RecordFormat::Source>(i);
    char dirPath[RecordFormat::kMaxPathLength];
    RecordFormat::sourceDir(source, dirPath, sizeof(dirPath));
    uint32_t files = 0;
    uint64_t bytes = 0;
    String latest;
    File dir = SD.open(dirPath);
    if (dir) {
      for (File day = dir.openNextFile(); day; day = dir.openNextFile()) {
        if (!day.isDirectory()) continue;
        String dayName = day.name();
        for (File entry = day.openNextFile(); entry; entry = day.openNextFile()) {
          if (entry.isDirectory()) continue;
          files++;
          bytes += entry.size();
          String name = dayName + "/" + entry.name();
          if (name > latest) latest = name;
        }
      }
      dir.close();
    }
    Serial.printf("[REC] %s: %u files, %lluKB%s%s\n", RecordFormat::sourceName(source), files, bytes / 1024,
                  latest.isEmpty() ? "" : ", latest ", latest.c_str());
  }
}
