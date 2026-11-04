#include "DataRecorder.h"

#include <M5Unified.h>
#include <SD.h>
#include <SPI.h>

#include <algorithm>

namespace {

// 記録の1件の上限。これより長い長さが読めたら、ファイルが壊れているとみなす
const size_t kMaxRecordSize = 256;

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

String hourKey(uint32_t utcSec) {
  char key[RecordFormat::kHourKeyLength];
  RecordFormat::hourKey(utcSec, key, sizeof(key));
  return String(key);
}

// 解放の印を残すファイル (/rec/<機器>/released)。中身は Unix 秒の数字
String releaseMarkPath(RecordFormat::Source source) {
  char dir[RecordFormat::kMaxPathLength];
  RecordFormat::sourceDir(source, dir, sizeof(dir));
  return String(dir) + "/released";
}

bool selected(uint8_t source, size_t i) { return source == DataRecorder::kAllSources || source == i; }

}  // namespace

void DataRecorder::begin() {
  if (mount()) {
    updateFreeSpace();
    Serial.printf("[REC] microSD: %lluMB, free %uMB\n", SD.totalBytes() / (1024 * 1024), freeMB_);
    scanContents(true);
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
  if (mounted_) loadReleaseMarks();
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
  if (ok) pendingBytes_[&buffer - buffers_] += buffer.used;
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
        scanContents(false);
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
    // 空きが少なければ、古い記録から消す (まだ降ろしていない分でも消す。記録を止めるより、新しいデータを残す)
    bool removed = false;
    for (size_t i = 0; i < RecordFormat::kSourceCount && freeMB_ < kMinFreeMB; i++) {
      while (freeMB_ < kMinFreeMB && removeOldest(static_cast<RecordFormat::Source>(i))) {
        removed = true;
        updateFreeSpace();
      }
    }
    if (removed) scanContents(false);
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

void DataRecorder::forEachFile(RecordFormat::Source source, const FileVisitor& visit) {
  char dirPath[RecordFormat::kMaxPathLength];
  RecordFormat::sourceDir(source, dirPath, sizeof(dirPath));
  File dir = SD.open(dirPath);
  if (!dir) return;
  for (File day = dir.openNextFile(); day; day = dir.openNextFile()) {
    if (!day.isDirectory()) continue;
    String dayName = day.name();
    for (File entry = day.openNextFile(); entry; entry = day.openNextFile()) {
      String name = entry.name();
      if (entry.isDirectory() || !name.endsWith(".bin")) continue;
      String key = dayName + "/" + name.substring(0, name.length() - 4);
      visit(key, String(dirPath) + "/" + dayName + "/" + name, entry.size());
    }
  }
  dir.close();
}

void DataRecorder::scanContents(bool print) {
  for (size_t i = 0; i < RecordFormat::kSourceCount; i++) {
    auto source = static_cast<RecordFormat::Source>(i);
    String releasedKey = hourKey(releasedSec_[i]);
    uint32_t files = 0;
    uint64_t bytes = 0;
    String latest;
    uint32_t startMs = millis();
    pendingBytes_[i] = 0;
    forEachFile(source, [&](const String& key, const String& path, uint32_t size) {
      files++;
      bytes += size;
      if (key > releasedKey) {
        pendingBytes_[i] += size;
      } else if (key == releasedKey) {
        // 印のある1時間分は、印より後の記録だけを数える
        pendingBytes_[i] += bytesFrom(path, static_cast<uint64_t>(releasedSec_[i]) * 1000, size);
      }
      if (key > latest) latest = key;
    });
    // 書く前に貯めている分も、まだ降ろしていない量に入れる
    pendingBytes_[i] += buffers_[i].used;
    if (!print) continue;
    Serial.printf("[REC] %s: %u files, %lluKB, pending %lluKB%s%s%s (%lu ms)\n", RecordFormat::sourceName(source),
                  files, bytes / 1024, pendingBytes_[i] / 1024, latest.isEmpty() ? "" : ", latest ", latest.c_str(),
                  latest.isEmpty() ? "" : ".bin", static_cast<unsigned long>(millis() - startMs));
  }
}

uint32_t DataRecorder::bytesFrom(const String& path, uint64_t fromMs, uint32_t size) {
  File file = SD.open(path, FILE_READ);
  if (!file) return size;
  uint8_t record[kMaxRecordSize];
  uint32_t offset = 0;
  while (offset < size) {
    uint8_t length[RecordFormat::kLengthSize];
    if (file.read(length, sizeof(length)) != sizeof(length)) break;
    size_t n = (static_cast<size_t>(length[0]) << 8) | length[1];
    // 書きかけで切れていたら、そこから後ろは全部「まだ降ろしていない」に数える
    if (n == 0 || n > sizeof(record) || file.read(record, n) != n) break;
    if (RecordFormat::packetTimeMs(record, n) >= fromMs) break;
    offset += RecordFormat::kLengthSize + n;
  }
  file.close();
  return size - offset;
}

void DataRecorder::loadReleaseMarks() {
  for (size_t i = 0; i < RecordFormat::kSourceCount; i++) {
    releasedSec_[i] = 0;
    String path = releaseMarkPath(static_cast<RecordFormat::Source>(i));
    // まだ一度も消していなければ、印のファイルは無い (無いファイルを開くとエラーのログが出るので、先に確かめる)
    if (!SD.exists(path)) continue;
    File file = SD.open(path, FILE_READ);
    if (!file) continue;
    releasedSec_[i] = static_cast<uint32_t>(file.readString().toInt());
    file.close();
  }
}

void DataRecorder::saveReleaseMark(RecordFormat::Source source) {
  char dir[RecordFormat::kMaxPathLength];
  RecordFormat::sourceDir(source, dir, sizeof(dir));
  if (!SD.exists("/rec")) SD.mkdir("/rec");
  if (!SD.exists(dir)) SD.mkdir(dir);
  File file = SD.open(releaseMarkPath(source), FILE_WRITE);
  if (!file) return;
  file.print(releasedSec_[static_cast<size_t>(source)]);
  file.close();
}

uint32_t DataRecorder::pendingKB(RecordFormat::Source source) const {
  return static_cast<uint32_t>(pendingBytes_[static_cast<size_t>(source)] / 1024);
}

bool DataRecorder::startDump(uint8_t source, uint32_t startSec, uint32_t endSec, uint32_t* totalBytes) {
  if (!mounted_ || dump_.active) return false;
  // 貯めている分も降ろせるよう、先に書いておく
  flushAll();
  if (!mounted_) return false;

  dump_ = Dump();
  dump_.source = source;
  dump_.endMs = endSec == 0 ? UINT64_MAX : static_cast<uint64_t>(endSec) * 1000;
  String endKey = endSec == 0 ? String("99999999/99") : hourKey(endSec - 1);
  for (size_t i = 0; i < RecordFormat::kSourceCount; i++) {
    if (!selected(source, i)) continue;
    uint32_t from = startSec == 0 ? releasedSec_[i] : startSec;
    String startKey = hourKey(from);
    std::vector<DumpFile> files;
    forEachFile(static_cast<RecordFormat::Source>(i), [&](const String& key, const String& path, uint32_t size) {
      if (key < startKey || key > endKey) return;
      uint64_t fromMs = static_cast<uint64_t>(from) * 1000;
      uint32_t startOffset = key == startKey ? size - bytesFrom(path, fromMs, size) : 0;
      uint32_t endOffset = endSec != 0 && key == endKey ? size - bytesFrom(path, dump_.endMs, size) : size;
      files.push_back({path, fromMs, size, startOffset, std::max(startOffset, endOffset)});
    });
    // パスに「年月日/時」が入っているので、文字の順に並べると時刻の順になる
    std::sort(files.begin(), files.end(), [](const DumpFile& a, const DumpFile& b) { return a.path < b.path; });
    for (const DumpFile& file : files) dump_.totalBytes += file.endOffset - file.startOffset;
    dump_.files.insert(dump_.files.end(), files.begin(), files.end());
  }
  if (!dump_.files.empty()) dump_.offset = dump_.files[0].startOffset;
  dump_.active = true;
  *totalBytes = dump_.totalBytes;
  Serial.printf("[REC] dump start: %u files, %luKB\n", dump_.files.size(),
                static_cast<unsigned long>(dump_.totalBytes / 1024));
  return true;
}

bool DataRecorder::stopDump() {
  if (!dump_.active) return false;
  finishDump(DumpEnd::Stopped);
  return true;
}

// 読んでいるファイルは、1回ごとに開いて閉じる。記録の書き込み (flush) と同じファイルを同時に開かないため
void DataRecorder::dumpStep(size_t maxBytes, const DumpSender& send) {
  if (!dump_.active) return;
  if (!mounted_) {
    finishDump(DumpEnd::CardError);
    return;
  }
  uint8_t record[kMaxRecordSize];
  size_t sentBytes = 0;
  size_t readBytes = 0;
  while (dump_.fileIndex < dump_.files.size() && sentBytes < maxBytes && readBytes < kMaxReadPerStep) {
    const DumpFile& target = dump_.files[dump_.fileIndex];
    File file = SD.open(target.path, FILE_READ);
    bool endOfFile = !file || !file.seek(dump_.offset);
    while (!endOfFile && sentBytes < maxBytes && readBytes < kMaxReadPerStep) {
      uint8_t length[RecordFormat::kLengthSize];
      if (file.read(length, sizeof(length)) != sizeof(length)) {
        endOfFile = true;
        break;
      }
      size_t size = (static_cast<size_t>(length[0]) << 8) | length[1];
      // 長さがおかしい、または書きかけで切れている (電源が落ちたときなど) なら、このファイルはここまで
      if (size == 0 || size > sizeof(record) || file.read(record, size) != size) {
        endOfFile = true;
        break;
      }
      dump_.offset += RecordFormat::kLengthSize + size;
      dump_.readBytes += RecordFormat::kLengthSize + size;
      readBytes += RecordFormat::kLengthSize + size;
      uint64_t timeMs = RecordFormat::packetTimeMs(record, size);
      if (timeMs < target.startMs) continue;
      // ファイルの中は時刻の順なので、終わりの時刻を過ぎたら、このファイルの残りは読まない
      if (timeMs >= dump_.endMs) {
        endOfFile = true;
        break;
      }
      send(record, size);
      dump_.sent++;
      sentBytes += size;
    }
    if (file) file.close();
    if (endOfFile) {
      dump_.fileIndex++;
      dump_.offset = dump_.fileIndex < dump_.files.size() ? dump_.files[dump_.fileIndex].startOffset : 0;
    }
  }
  if (dump_.fileIndex >= dump_.files.size()) finishDump(DumpEnd::Completed);
}

void DataRecorder::finishDump(DumpEnd end) {
  dump_.active = false;
  dump_.ended = true;
  dump_.end = end;
  dump_.files.clear();
  dump_.files.shrink_to_fit();
  Serial.printf("[REC] dump end: %u, sent %lu\n", static_cast<unsigned>(end),
                static_cast<unsigned long>(dump_.sent));
}

bool DataRecorder::takeDumpEnd(DumpEnd* end, uint32_t* sent) {
  if (!dump_.ended) return false;
  dump_.ended = false;
  *end = dump_.end;
  *sent = dump_.sent;
  return true;
}

uint32_t DataRecorder::dumpRemainingKB() const {
  if (!dump_.active || dump_.readBytes >= dump_.totalBytes) return 0;
  return (dump_.totalBytes - dump_.readBytes + 1023) / 1024;
}

int DataRecorder::clear(uint8_t source, uint32_t beforeSec) {
  if (!mounted_ || dump_.active) return -1;
  int removed = 0;
  for (size_t i = 0; i < RecordFormat::kSourceCount; i++) {
    if (!selected(source, i)) continue;
    auto s = static_cast<RecordFormat::Source>(i);
    // 印は戻さない (一度降ろせたと確かめた分を、また「まだ」に戻さない)
    if (beforeSec > releasedSec_[i]) {
      releasedSec_[i] = beforeSec;
      saveReleaseMark(s);
    }
    // 印より前に終わった1時間分のファイルを消す。消したあとに空になった日のフォルダも消す
    String releasedKey = hourKey(releasedSec_[i]);
    std::vector<String> paths;
    forEachFile(s, [&](const String& key, const String& path, uint32_t) {
      if (key < releasedKey) paths.push_back(path);
    });
    for (const String& path : paths) {
      if (SD.remove(path)) removed++;
      SD.rmdir(path.substring(0, path.lastIndexOf('/')));  // まだファイルが残っていれば消えない
    }
  }
  scanContents(false);
  Serial.printf("[REC] clear: %d files\n", removed);
  return removed;
}
