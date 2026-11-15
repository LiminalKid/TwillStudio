#include "platform/Midi.h"

namespace twill {

namespace {
// Data bytes that follow a status byte.
int data_bytes(uint8_t status) {
  switch (status & 0xF0) {
    case 0xC0: case 0xD0: return 1;  // program change, channel pressure
    case 0xF0:
      switch (status) {
        case 0xF1: case 0xF3: return 1;  // time code quarter frame, song select
        case 0xF2: return 2;             // song position
        default: return 0;
      }
    default: return 2;
  }
}
}  // namespace

int MidiMessage::size() const { return 1 + data_bytes(status); }

void MidiParser::feed(const uint8_t* bytes, size_t n, std::vector<MidiMessage>& out) {
  for (size_t i = 0; i < n; ++i) {
    const uint8_t b = bytes[i];
    if (b >= 0xF8) {  // real-time: may arrive anywhere, even inside another message
      out.push_back(MidiMessage{b, 0, 0});
      continue;
    }
    if (b & 0x80) {
      if (b == 0xF0) {
        sysex_ = true;
        continue;
      }
      if (b == 0xF7) {
        sysex_ = false;
        continue;
      }
      sysex_ = false;
      status_ = b;
      have_ = 0;
      if (data_bytes(b) == 0) {
        out.push_back(MidiMessage{b, 0, 0});
        if (b >= 0xF0) status_ = 0;  // system common: no running status
      }
      continue;
    }
    if (sysex_ || status_ == 0) continue;  // data with nothing to belong to
    data_[have_++] = b;
    if (have_ == data_bytes(status_)) {
      out.push_back(MidiMessage{status_, data_[0], have_ > 1 ? data_[1] : uint8_t(0)});
      have_ = 0;  // running status: the next data bytes reuse status_
      if (status_ >= 0xF0) status_ = 0;
    }
  }
}

}  // namespace twill
