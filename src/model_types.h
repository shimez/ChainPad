#pragma once
#include <cstddef>
#include <cstdint>

namespace chimera {
constexpr size_t MAX_OSC_ADDRESS_BYTES = 192;
enum class Transport : uint8_t { Wifi, Usb, Ble, Both };
}
