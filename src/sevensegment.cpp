
#include "sevensegment.h"

SevenSegment::SevenSegment(uint8_t clkPin,
                           uint8_t dioPin,
                           uint8_t brightness)
    : display_(clkPin, dioPin),
      brightness_(brightness > 7 ? 7 : brightness) {
}

void SevenSegment::begin() {
    display_.setBrightness(brightness_);
    display_.clear();
}

void SevenSegment::showNumber(uint16_t value,
                              bool leadingZero) {
    // Display TM1637 memiliki 4 digit.
    if (value > 9999U) {
        display_.showNumberDec(9999, leadingZero);
        return;
    }

    display_.showNumberDec(
        static_cast<int>(value),
        leadingZero
    );
}

void SevenSegment::showNumberAt(uint16_t value,
                                uint8_t length,
                                uint8_t position,
                                bool leadingZero) {
    if (position > 3) {
        position = 3;
    }

    if (length < 1) {
        length = 1;
    }

    if (length > 4) {
        length = 4;
    }

    // Pastikan digit tidak melewati batas display.
    if (position + length > 4) {
        length = 4 - position;
    }

    if (value > 9999U) {
        value = 9999U;
    }

    display_.showNumberDec(
        static_cast<int>(value),
        leadingZero,
        length,
        position
    );
}

void SevenSegment::showFloatSwitch(bool high) {
    // Digit paling kanan saja yang menyala: "   1" atau "   0".
    display_.clear();
    display_.showNumberDec(high ? 1 : 0, false, 1, 3);
}

void SevenSegment::clear() {
    display_.clear();
}

void SevenSegment::setBrightness(uint8_t brightness) {
    brightness_ = brightness > 7 ? 7 : brightness;
    display_.setBrightness(brightness_);
}
