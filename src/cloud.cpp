#include "cloud.h"

static float random01() {
    uint32_t r = esp_random();
    if (r == 0) r = 1;
    return static_cast<float>(r) / 4294967295.0f;
}

static float sampleExponential(float mean) {
    if (mean <= 0.0f) return 0.0f;
    float u = random01();
    if (u < 1e-6f) u = 1e-6f;
    return -logf(u) * mean;
}

bool cloudChannelConfigured(uint8_t ch) {
    if (ch >= LED_CHANNEL_COUNT) return false;
    return cloudChannelEnabled[ch] && cloudEventsPerDay[ch] > 0;
}

void scheduleNextCloudForChannel(uint8_t ch, bool immediateBase) {
    if (ch >= LED_CHANNEL_COUNT || !cloudSimEnabled || !cloudChannelConfigured(ch)) {
        cloudRuntime[ch].nextStartMs = 0;
        return;
    }

    const float meanIntervalSec = 86400.0f / static_cast<float>(cloudEventsPerDay[ch]);
    float delaySec = sampleExponential(meanIntervalSec);
    if (delaySec < 0.2f) delaySec = 0.2f;

    const unsigned long now  = millis();
    const unsigned long base = immediateBase ? now
                             : (cloudRuntime[ch].active ? cloudRuntime[ch].endMs : now);
    cloudRuntime[ch].nextStartMs = base + static_cast<unsigned long>(delaySec * 1000.0f);
}

void stopCloudChannel(uint8_t ch) {
    cloudRuntime[ch].active         = false;
    cloudRuntime[ch].startMs        = 0;
    cloudRuntime[ch].endMs          = 0;
    cloudRuntime[ch].peakDimPercent = 0;
    cloudCurrentDimPercent[ch]      = 0;
}

void resetCloudSimulationRuntime(bool keepNextSchedule) {
    for (uint8_t ch = 0; ch < LED_CHANNEL_COUNT; ++ch) {
        stopCloudChannel(ch);
        if (!keepNextSchedule) scheduleNextCloudForChannel(ch, true);
    }
}

int cloudNextInSecondsForChannel(uint8_t ch) {
    if (!cloudSimEnabled || !cloudChannelConfigured(ch)) return -1;
    if (cloudRuntime[ch].active) return 0;
    const unsigned long now = millis();
    if (cloudRuntime[ch].nextStartMs <= now) return 0;
    return static_cast<int>((cloudRuntime[ch].nextStartMs - now) / 1000UL);
}

int cloudNextInSeconds() {
    if (!cloudSimEnabled) return -1;
    int best = -1;
    for (uint8_t ch = 0; ch < LED_CHANNEL_COUNT; ++ch) {
        const int sec = cloudNextInSecondsForChannel(ch);
        if (sec < 0) continue;
        if (best < 0 || sec < best) best = sec;
    }
    return best;
}

uint8_t cloudActiveCount() {
    uint8_t count = 0;
    for (uint8_t ch = 0; ch < LED_CHANNEL_COUNT; ++ch)
        if (cloudRuntime[ch].active) count++;
    return count;
}

void updateCloudSimulation() {
    const unsigned long now = millis();

    if (previewActive || !cloudSimEnabled) {
        for (uint8_t ch = 0; ch < LED_CHANNEL_COUNT; ++ch) {
            if (cloudRuntime[ch].active) stopCloudChannel(ch);
            if (!cloudSimEnabled || !cloudChannelConfigured(ch))
                cloudRuntime[ch].nextStartMs = 0;
        }
        return;
    }

    for (uint8_t ch = 0; ch < LED_CHANNEL_COUNT; ++ch) {
        if (!cloudChannelConfigured(ch)) {
            if (cloudRuntime[ch].active) stopCloudChannel(ch);
            cloudRuntime[ch].nextStartMs = 0;
            continue;
        }

        if (cloudRuntime[ch].active) {
            if (now >= cloudRuntime[ch].endMs) {
                stopCloudChannel(ch);
                scheduleNextCloudForChannel(ch, false);
            }
            continue;
        }

        if (cloudRuntime[ch].nextStartMs == 0) {
            scheduleNextCloudForChannel(ch, true);
            continue;
        }

        if (now < cloudRuntime[ch].nextStartMs) continue;

        const float jitter = 0.8f + random01() * 0.4f;
        cloudRuntime[ch].peakDimPercent = clampCloudPercent(
            static_cast<int>(roundf(cloudDimPercent[ch] * jitter)));

        float durationSec = sampleExponential(static_cast<float>(cloudAvgDurationSec[ch]));
        if (durationSec < cloudMinDurationSec[ch])
            durationSec = static_cast<float>(cloudMinDurationSec[ch]);

        cloudRuntime[ch].active  = true;
        cloudRuntime[ch].startMs = now;
        cloudRuntime[ch].endMs   = now + static_cast<unsigned long>(durationSec * 1000.0f);
    }

    for (uint8_t ch = 0; ch < LED_CHANNEL_COUNT; ++ch) {
        cloudCurrentDimPercent[ch] = 0;
        if (!cloudRuntime[ch].active) continue;
        const unsigned long start = cloudRuntime[ch].startMs;
        const unsigned long end   = cloudRuntime[ch].endMs;
        if (end <= start) continue;
        const float t = (now <= start) ? 0.0f
                      : (now >= end)   ? 1.0f
                      : (static_cast<float>(now - start) / static_cast<float>(end - start));
        const float triangle = (t <= 0.5f) ? (t * 2.0f) : ((1.0f - t) * 2.0f);
        cloudCurrentDimPercent[ch] = clampCloudPercent(
            static_cast<int>(roundf(cloudRuntime[ch].peakDimPercent * triangle)));
    }
}
