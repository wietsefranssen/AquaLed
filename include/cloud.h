#pragma once

#include "globals.h"

// ─── Cloud-simulation clamping helpers ───────────────────────────────────────

inline uint16_t clampCloudDurationSec(int seconds) {
    if (seconds < 1)    return 1;
    if (seconds > 3600) return 3600;
    return static_cast<uint16_t>(seconds);
}

inline uint16_t clampCloudMinDurationSec(int seconds) {
    if (seconds < 1)    return 1;
    if (seconds > 3600) return 3600;
    return static_cast<uint16_t>(seconds);
}

inline uint16_t clampCloudEventsPerDay(int count) {
    if (count < 1)    return 1;
    if (count > 5000) return 5000;
    return static_cast<uint16_t>(count);
}

inline uint8_t clampCloudPercent(int pct) {
    if (pct < 0)   return 0;
    if (pct > 100) return 100;
    return static_cast<uint8_t>(pct);
}

// ─── Function declarations ───────────────────────────────────────────────────

bool    cloudChannelConfigured(uint8_t ch);
void    scheduleNextCloudForChannel(uint8_t ch, bool immediateBase = false);
void    stopCloudChannel(uint8_t ch);
void    resetCloudSimulationRuntime(bool keepNextSchedule = false);
int     cloudNextInSecondsForChannel(uint8_t ch);
int     cloudNextInSeconds();
uint8_t cloudActiveCount();
void    updateCloudSimulation();
