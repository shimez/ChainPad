#pragma once
#include <stdint.h>
#ifndef CHAINPAD_HID_TRACE
#define CHAINPAD_HID_TRACE 0
#endif
#if CHAINPAD_HID_TRACE
#include <Arduino.h>
#endif
namespace chimera { namespace hidtrace {
enum Event : uint8_t {
  Input, InputQueued, InputDropped, InputConsumed, ActionAccepted, ActionRejected,
  ChainRejected, Push, Pop, Discard, Overflow, Neutral, NotifyCall, NotifyReturn,
  Status, Subscribe, Disconnect, Reset, Dispatch, Count
};
#if CHAINPAD_HID_TRACE
void record(Event event, uint8_t transport = 255, uint16_t usage = 0,
            uint32_t id = 0, uint32_t epoch = 0, int32_t value = 0);
uint32_t nextId(); // loop task only; never placed in a HID report
void context(uint8_t transport, uint32_t id);
uint32_t context(uint8_t transport);
bool command(int ch); // 1=counters, 2=events, 0=stop, p=dump
bool active();
void dumpTick(Print& out);
#else
inline void record(Event, uint8_t = 255, uint16_t = 0, uint32_t = 0, uint32_t = 0, int32_t = 0) {}
#endif
}}
