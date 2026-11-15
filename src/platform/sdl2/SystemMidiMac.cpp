// macOS: CoreMIDI. Every source is connected to one input port; sends go to every destination.
// Sources added later are connected when CoreMIDI reports a setup change.
#include <CoreMIDI/CoreMIDI.h>

// MIDIInputPortCreate is deprecated in favour of the MIDI 2.0 protocol API, but it is what every
// macOS version this targets supports, and MIDI 1.0 is all we need.
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "platform/sdl2/SystemMidi.h"

namespace twill {

namespace {

class CoreMidi : public MidiPort {
 public:
  CoreMidi() {
    MIDIClientCreate(CFSTR("Twill Studio"), &CoreMidi::notify, this, &client_);
    if (!client_) return;
    MIDIInputPortCreate(client_, CFSTR("in"), &CoreMidi::read, this, &in_);
    MIDIOutputPortCreate(client_, CFSTR("out"), &out_);
    connect_all();
  }
  ~CoreMidi() override {
    if (client_) MIDIClientDispose(client_);
  }

  void poll(std::vector<MidiMessage>& in) override {
    if (setup_changed_.exchange(false)) connect_all();
    std::lock_guard<std::mutex> lock(mutex_);  // the read callback runs on CoreMIDI's thread
    in.insert(in.end(), pending_.begin(), pending_.end());
    pending_.clear();
  }

  void send(const MidiMessage& m) override { send_except(m, -1); }

  // A device's input and output are separate endpoints of one entity; the entity is the source id.
  void send_except(const MidiMessage& m, int32_t source) override {
    if (!out_) return;
    Byte buffer[64];
    MIDIPacketList* list = reinterpret_cast<MIDIPacketList*>(buffer);
    MIDIPacket* packet = MIDIPacketListInit(list);
    const Byte bytes[3] = {m.status, m.data1, m.data2};
    packet = MIDIPacketListAdd(list, sizeof buffer, packet, 0, ByteCount(m.size()), bytes);
    if (!packet) return;
    for (ItemCount i = 0; i < MIDIGetNumberOfDestinations(); ++i) {
      const MIDIEndpointRef dest = MIDIGetDestination(i);
      if (source != -1 && id_of(dest) == source) continue;
      MIDISend(out_, dest, list);
    }
  }

  std::string describe() const override {
    const ItemCount ins = MIDIGetNumberOfSources(), outs = MIDIGetNumberOfDestinations();
    if (!ins && !outs) return "no devices";
    return std::to_string(ins) + " in, " + std::to_string(outs) + " out";
  }

 private:
  // Virtual endpoints have no entity: they stand for themselves.
  static int32_t id_of(MIDIEndpointRef e) {
    MIDIEntityRef entity = 0;
    MIDIEndpointGetEntity(e, &entity);
    return int32_t(entity ? entity : e);
  }

  void connect_all() {
    if (!in_) return;
    for (ItemCount i = 0; i < MIDIGetNumberOfSources(); ++i) {
      const MIDIEndpointRef src = MIDIGetSource(i);
      MIDIPortConnectSource(in_, src, reinterpret_cast<void*>(uintptr_t(src)));
    }
  }

  static void notify(const MIDINotification* n, void* self) {
    if (n->messageID == kMIDIMsgSetupChanged) static_cast<CoreMidi*>(self)->setup_changed_.store(true);
  }

  static void read(const MIDIPacketList* list, void* self_ptr, void* src_ptr) {
    CoreMidi* self = static_cast<CoreMidi*>(self_ptr);
    const MIDIEndpointRef src = MIDIEndpointRef(uintptr_t(src_ptr));
    std::vector<MidiMessage> msgs;
    MidiParser& parser = self->parsers_[src];  // one per source, so their bytes don't mix
    const MIDIPacket* p = &list->packet[0];
    for (UInt32 i = 0; i < list->numPackets; ++i) {
      parser.feed(p->data, p->length, msgs);
      p = MIDIPacketNext(p);
    }
    const int32_t id = id_of(src);
    const int64_t now = std::chrono::duration_cast<std::chrono::microseconds>(
                            std::chrono::steady_clock::now().time_since_epoch()).count();
    for (MidiMessage& m : msgs) {
      m.source = id;
      m.time_us = now;  // CoreMIDI calls this as the bytes arrive
    }
    std::lock_guard<std::mutex> lock(self->mutex_);
    self->pending_.insert(self->pending_.end(), msgs.begin(), msgs.end());
  }

  MIDIClientRef client_ = 0;
  MIDIPortRef in_ = 0, out_ = 0;
  std::map<MIDIEndpointRef, MidiParser> parsers_;  // used only on CoreMIDI's thread
  std::mutex mutex_;
  std::vector<MidiMessage> pending_;
  std::atomic<bool> setup_changed_{false};
};

}  // namespace

std::unique_ptr<MidiPort> make_system_midi() { return std::make_unique<CoreMidi>(); }

}  // namespace twill
