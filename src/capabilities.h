#pragma once

// Hardware capability, not a user setting. USB Serial/JTAG on the C-series
// is a console only; it cannot provide the S3's USB MIDI/HID interfaces.
#ifndef CHAINPAD_HAS_USB
#if defined(CONFIG_IDF_TARGET_ESP32S3)
#define CHAINPAD_HAS_USB 1
#else
#define CHAINPAD_HAS_USB 0
#endif
#endif

namespace chimera {
constexpr bool HAS_USB_MIDI = CHAINPAD_HAS_USB;
constexpr bool HAS_USB_KEYBOARD = CHAINPAD_HAS_USB;
#if defined(CONFIG_IDF_TARGET_ESP32C3)
constexpr char HARDWARE_NAME[] = "XIAO ESP32C3 / ChainOSCPad PCB";
#elif defined(CONFIG_IDF_TARGET_ESP32C6)
constexpr char HARDWARE_NAME[] = "XIAO ESP32C6 / ChainOSCPad PCB";
#elif defined(CONFIG_IDF_TARGET_ESP32C5)
constexpr char HARDWARE_NAME[] = "XIAO ESP32C5 / ChainOSCPad PCB";
#else
constexpr char HARDWARE_NAME[] = "XIAO ESP32S3 / ChainOSCPad PCB";
#endif
}
