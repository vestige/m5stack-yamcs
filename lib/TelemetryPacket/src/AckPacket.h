#pragma once

#include <stddef.h>
#include <stdint.h>

// 受け取ったTCへの応答 (ACK) を CCSDS Space Packet (TM) に詰める。
// YAMCSはTCのシーケンス番号を照らし合わせて Command Verification を進める。
//
// offset size  field
//      0    6  CCSDS primary header (APID=102)
//      6    2  uint16  TC sequence count (どのコマンドへの応答か)
//      8    1  uint8   command id
//      9    1  uint8   stage (Stage)
//     10    1  uint8   error code (Telecommand::ErrorCode)
namespace AckPacket {

const uint16_t kApid = 102;
const size_t kPacketSize = 11;

enum class Stage : uint8_t {
  Accepted = 1,   // 受理した
  Rejected = 2,   // 受理しなかった
  Completed = 3,  // 実行が完了した
  Failed = 4,     // 実行に失敗した
};

size_t encode(uint16_t tcSequenceCount, uint8_t commandId, Stage stage, uint8_t errorCode,
              uint16_t sequenceCount, uint8_t* buffer, size_t size);

}  // namespace AckPacket
