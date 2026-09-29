#pragma once

#include <Arduino.h>

constexpr uint64_t operator""_MHz(uint64_t mhz) { return mhz * 1000000ULL; }

class Ring {
   private:
    static constexpr size_t CAPACITY = 256;

    uint32_t ring[CAPACITY];
    size_t   size;
    size_t   index;
    uint64_t sum;

   public:
    Ring() : size(0), index(0), sum(0) { memset(ring, 0, CAPACITY * sizeof(uint32_t)); }

    void push(uint32_t value) {
        sum         -= ring[index];
        ring[index]  = value;
        sum         += value;
        index        = (index + 1) % CAPACITY;
        if (size < CAPACITY) size++;
    }

    float avg() { return size ? (float)sum / size : 0.0f; }
};

class FpsCounter {
   private:
    Ring     durations;
    uint32_t tmr;

   public:
    FpsCounter() : durations(), tmr(0) {}

    void tick() {
        uint32_t now = millis();
        if (tmr) durations.push(now - tmr);
        tmr = now;
    }

    void pause() { tmr = 0; }

    float fps() {
        float duration = durations.avg();
        return duration ? 1000.0f / duration : 0.0f;
    }
};
