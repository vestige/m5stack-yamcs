#pragma once

#include <Arduino.h>
#include <RecordFormat.h>
#include <stddef.h>
#include <stdint.h>

#include <functional>
#include <vector>

// データレコーダ (#55): 送るテレメトリを、M5Stack の microSD に機器ごとに記録する。
// パスの外 (地上に届かない間) のデータも残り、パスの間にダンプ (#56) で降ろす。
// 書き込みの回数を減らすため、機器ごとに貯めて kFlushIntervalMs ごとにまとめて書く。
// microSD が無い・壊れたときも衛星は動き続け、記録だけ止める (状態は HK とイベントで知らせる)
//
// ダンプと消去 (#56):
// - 地上が「降ろせた」と確かめたら CLEAR_RECORDER で消す。消した時刻 (解放の印) を機器ごとに microSD に残し、
//   次のダンプは、何も言わなければ (開始=0) その印から始める。降ろしたと確かめていない分は消さない
// - ファイルは1時間ごとなので、消すのは印より前に終わった1時間分のファイルだけ。印のある1時間分は、
//   ファイルは残るが、ダンプでは印より前の記録を送らない
class DataRecorder {
 public:
  enum class State : uint8_t {
    NoCard = 0,       // microSD が無い (読めない)
    Recording = 1,    // 記録している
    WaitingTime = 2,  // 機上時刻がわからないので、まだ記録していない
    Error = 3,        // 書き込みに失敗した
  };

  // ダンプがどう終わったか (DUMP_ENDED の arg1)
  enum class DumpEnd : uint8_t { Completed = 0, Stopped = 1, CardError = 2 };

  // ダンプや消去で「全部の機器」を表す番号 (Telecommand::RecorderSource::All と同じ)
  static const uint8_t kAllSources = RecordFormat::kSourceCount;

  using DumpSender = std::function<void(const uint8_t* packet, size_t size)>;

  void begin();
  // 送るパケットを記録する (貯めておき、poll で書く)
  void record(const uint8_t* packet, size_t size);
  // まとめて書く、空きを確かめる、microSD をつなぎ直す
  void poll(uint32_t nowMs);

  // ダンプを始める。source は機器の番号か kAllSources。startSec=0 は解放の印から。
  // 降ろす量 (バイト) を totalBytes に返す。microSD が無い・ダンプ中なら false
  bool startDump(uint8_t source, uint32_t startSec, uint32_t endSec, uint32_t* totalBytes);
  bool stopDump();
  // 記録を読んで、maxBytes まで send で送る。ダンプしていなければ何もしない
  void dumpStep(size_t maxBytes, const DumpSender& send);
  // ダンプが終わったら一度だけ true を返す
  bool takeDumpEnd(DumpEnd* end, uint32_t* sent);

  // beforeSec より前を、降ろし終えた分として消す。消したファイルの数を返す。microSD が無い・ダンプ中なら -1
  int clear(uint8_t source, uint32_t beforeSec);

  State state() const { return state_; }
  uint16_t freeMB() const { return freeMB_; }
  uint32_t records() const { return records_; }
  // 解放の印より後の記録の量 (まだ降ろしたと確かめていない量)
  uint32_t pendingKB(RecordFormat::Source source) const;
  // 解放の印 (Unix秒。この時刻より前は、地上が降ろせたと確かめた。まだ一度も消していなければ0)
  uint32_t releasedSec(RecordFormat::Source source) const { return releasedSec_[static_cast<size_t>(source)]; }
  bool dumping() const { return dump_.active; }
  uint8_t dumpSource() const { return dump_.source; }
  uint32_t dumpSent() const { return dump_.sent; }
  uint32_t dumpRemainingKB() const;

 private:
  static const size_t kBufferSize = 4096;
  static const uint32_t kFlushIntervalMs = 5000;
  static const uint32_t kSpaceCheckIntervalMs = 60000;
  static const uint32_t kRetryIntervalMs = 10000;
  // 空きがこれより少なくなったら、古い記録から消す
  static const uint16_t kMinFreeMB = 100;
  // ダンプの1回 (dumpStep) で読む量の上限。範囲の外の記録を読み飛ばすのにも時間がかかるので
  static const size_t kMaxReadPerStep = 8192;

  struct Buffer {
    uint8_t data[kBufferSize];
    size_t used = 0;
    char path[RecordFormat::kMaxPathLength] = "";
  };

  // ダンプで読むファイル。機器ごとに解放の印が違うので、ファイルごとに始めの時刻を持つ
  struct DumpFile {
    String path;
    uint64_t startMs;
    uint32_t size;
    // 読む範囲。始めの1時間分は始めの時刻より前を、終わりの1時間分は終わりの時刻から後を読まない
    uint32_t startOffset;
    uint32_t endOffset;
  };

  struct Dump {
    bool active = false;
    uint8_t source = 0;
    std::vector<DumpFile> files;
    size_t fileIndex = 0;
    uint32_t offset = 0;
    uint64_t endMs = 0;
    uint32_t totalBytes = 0;
    uint32_t readBytes = 0;
    uint32_t sent = 0;
    bool ended = false;
    DumpEnd end = DumpEnd::Completed;
  };

  using FileVisitor = std::function<void(const String& key, const String& path, uint32_t size)>;

  bool mount();
  bool flush(Buffer& buffer);
  void flushAll();
  void updateFreeSpace();
  bool removeOldest(RecordFormat::Source source);
  // その機器の1時間分のファイルを、「年月日/時」とともに渡す (順番は決まっていない)
  void forEachFile(RecordFormat::Source source, const FileVisitor& visit);
  // まだ降ろしたと確かめていない量を数え直す。print なら、記録の量をシリアルに出す
  void scanContents(bool print);
  // ファイルのうち、時刻が fromMs から後の記録のバイト数。ファイルの中は時刻の順なので、fromMs に届くまでだけ読む
  uint32_t bytesFrom(const String& path, uint64_t fromMs, uint32_t size);
  void loadReleaseMarks();
  void saveReleaseMark(RecordFormat::Source source);
  void finishDump(DumpEnd end);

  Buffer buffers_[RecordFormat::kSourceCount];
  State state_ = State::NoCard;
  bool mounted_ = false;
  uint16_t freeMB_ = 0;
  uint32_t records_ = 0;
  uint32_t lastFlushMs_ = 0;
  uint32_t lastSpaceCheckMs_ = 0;
  uint32_t lastRetryMs_ = 0;
  // 解放の印 (この時刻より前は、地上が降ろせたと確かめた。Unix秒)
  uint32_t releasedSec_[RecordFormat::kSourceCount] = {};
  uint64_t pendingBytes_[RecordFormat::kSourceCount] = {};
  Dump dump_;
};
