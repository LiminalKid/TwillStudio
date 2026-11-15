// MIDI (design §8): the message type, a byte-stream parser, and the port interface platforms
// implement.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace twill {

struct MidiMessage {
  uint8_t status = 0, data1 = 0, data2 = 0;
  int32_t source = -1;  // received: which device it came from (an id the port chooses); -1 unknown
  int64_t time_us = 0;  // received: when, on the steady clock (MidiSender::now_us()); 0 unknown
  uint8_t type() const { return status & 0xF0; }
  int channel() const { return status & 0x0F; }
  bool is_note_on() const { return type() == 0x90 && data2 > 0; }
  bool is_note_off() const { return type() == 0x80 || (type() == 0x90 && data2 == 0); }
  int size() const;  // bytes on the wire: 1, 2 or 3
};

// Turns a byte stream into messages: running status, real-time bytes in the middle of messages,
// and system exclusive (skipped).
class MidiParser {
 public:
  // Appends complete messages to `out`.
  void feed(const uint8_t* bytes, size_t n, std::vector<MidiMessage>& out);

 private:
  uint8_t status_ = 0;
  uint8_t data_[2] = {};
  int have_ = 0;
  bool sysex_ = false;
};

// Every connected device, merged: messages from all inputs, sends to all outputs.
class MidiPort {
 public:
  virtual ~MidiPort() = default;
  virtual void poll(std::vector<MidiMessage>& in) = 0;  // everything received since the last call
  virtual void send(const MidiMessage& m) = 0;
  // To every device but the one `source` names, so a note played on a synth's own keys isn't
  // sent back to it (it plays those itself, and one that echoes its input would loop).
  virtual void send_except(const MidiMessage& m, int32_t source) {
    (void)source;
    send(m);
  }
  virtual std::string describe() const = 0;             // e.g. "2 inputs, 1 output"
};

}  // namespace twill
