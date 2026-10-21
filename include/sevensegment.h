#pragma once

#include <Arduino.h>
#include <TM1637Display.h>

// Driver sederhana untuk display TM1637 4 digit.
// Default wiring: CLK -> GPIO19, DIO -> GPIO18.
class SevenSegment {
public:
  explicit SevenSegment(uint8_t clkPin = 17,
                        uint8_t dioPin = 18,
                        uint8_t brightness = 5);

  // Panggil sekali dari setup().
  void begin();

  // Tampilkan integer 0..9999. leadingZero=true menampilkan 0007, dst.
  void showNumber(uint16_t value, bool leadingZero = true);

  // Tampilkan integer mulai dari posisi digit tertentu (0 = paling kiri).
  void showNumberAt(uint16_t value,
                    uint8_t length = 4,
                    uint8_t position = 0,
                    bool leadingZero = true);

  // Mode uji: tampilkan status float switch (HIGH -> 1, LOW -> 0).
  void showFloatSwitch(bool high);

  void clear();
  void setBrightness(uint8_t brightness);  // 0..7

private:
  TM1637Display display_;
  uint8_t brightness_;
};
