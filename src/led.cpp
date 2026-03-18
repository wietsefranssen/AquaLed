#include <math.h>
#include "led.h"
#include "wifi_mgr.h"  // getMinuteOfDay()

// ─── Maanfase berekening ──────────────────────────────────────────────────────

float calcMoonPhase() {
    time_t now = time(nullptr);
    if (now < 86400) return 0.5f;
    double jd  = now / 86400.0 + 2440587.5;
    double age = fmod(jd - 2451550.1, MOON_CYCLE_DAYS);
    if (age < 0.0) age += MOON_CYCLE_DAYS;
    return static_cast<float>((1.0 - cos(2.0 * M_PI * age / MOON_CYCLE_DAYS)) / 2.0);
}

// ─── Curve helpers ────────────────────────────────────────────────────────────

void sortAndNormalizeCurve(ChannelCurve &curve) {
    if (curve.pointCount == 0) {
        curve.pointCount = 2;
        curve.points[0]  = {0, 0};
        curve.points[1]  = {1439, 0};
        return;
    }

    if (curve.pointCount > MAX_POINTS) curve.pointCount = MAX_POINTS;

    for (uint8_t i = 0; i < curve.pointCount; ++i) {
        curve.points[i].minute = clampMinute(curve.points[i].minute);
        curve.points[i].value  = clampValue(curve.points[i].value);
    }

    for (uint8_t i = 0; i < curve.pointCount; ++i) {
        for (uint8_t j = i + 1; j < curve.pointCount; ++j) {
            if (curve.points[j].minute < curve.points[i].minute) {
                KeyPoint t      = curve.points[i];
                curve.points[i] = curve.points[j];
                curve.points[j] = t;
            }
        }
    }

    KeyPoint dedup[MAX_POINTS];
    uint8_t  dedupCount = 0;
    for (uint8_t i = 0; i < curve.pointCount; ++i) {
        if (dedupCount == 0 || dedup[dedupCount - 1].minute != curve.points[i].minute)
            dedup[dedupCount++] = curve.points[i];
        else
            dedup[dedupCount - 1].value = curve.points[i].value;
    }

    if (dedupCount == 1) {
        dedup[1] = {1439, dedup[0].value};
        dedupCount = 2;
    }

    curve.pointCount = dedupCount;
    for (uint8_t i = 0; i < dedupCount; ++i) curve.points[i] = dedup[i];
}

void fillDefaultPreset(Preset &preset, const String &name) {
    preset.name = name;

    const uint8_t dayShape[LED_CHANNEL_COUNT][4] = {
        {10, 120, 220, 20},
        {5,  90,  180, 10},
        {0,  60,  140,  0},
        {0,  70,  170,  0},
        {0,  35,   90,  0},
    };
    const uint16_t times[4] = {0, 480, 1020, 1439};

    for (int ch = 0; ch < LED_CHANNEL_COUNT; ++ch) {
        preset.channels[ch].pointCount = 4;
        for (int i = 0; i < 4; ++i)
            preset.channels[ch].points[i] = {times[i], static_cast<uint16_t>(dayShape[ch][i] * 16)};
        sortAndNormalizeCurve(preset.channels[ch]);
    }
}

void initDefaultData() {
    gData.presetCount  = 1;
    gData.activePreset = 0;
    fillDefaultPreset(gData.presets[0], "Default preset");
    for (uint8_t ch = 0; ch < LED_CHANNEL_COUNT; ++ch) {
        gData.channelColors[ch]   = String(DEFAULT_COLORS[ch]);
        gData.channelMaxWatts[ch] = 0.0f;
    }
}

// ─── Curve evaluatie ─────────────────────────────────────────────────────────

uint16_t evaluateCurve(const ChannelCurve &curve, float minuteOfDay) {
    if (curve.pointCount == 0) return 0;
    if (curve.pointCount == 1) return curve.points[0].value;

    float minute = minuteOfDay;
    while (minute < 0.0f)     minute += 1440.0f;
    while (minute >= 1440.0f) minute -= 1440.0f;

    const KeyPoint *a = nullptr;
    const KeyPoint *b = nullptr;
    float segmentStart = 0.0f;
    float segmentEnd   = 0.0f;

    for (uint8_t i = 0; i < curve.pointCount - 1; ++i) {
        if (minute >= curve.points[i].minute && minute <= curve.points[i + 1].minute) {
            a            = &curve.points[i];
            b            = &curve.points[i + 1];
            segmentStart = a->minute;
            segmentEnd   = b->minute;
            break;
        }
    }

    if (!a || !b) {
        a            = &curve.points[curve.pointCount - 1];
        b            = &curve.points[0];
        segmentStart = a->minute;
        segmentEnd   = b->minute + 1440.0f;
        if (minute < curve.points[0].minute) minute += 1440.0f;
    }

    float span  = segmentEnd - segmentStart;
    float t     = (span > 0.0f) ? ((minute - segmentStart) / span) : 0.0f;
    float value = a->value + (b->value - a->value) * smoothStep(t);
    return clampValue(static_cast<int>(roundf(value)));
}

// ─── PWM output ──────────────────────────────────────────────────────────────

// Gamma 2.8 correctie: perceptueel lineair dimmen over volledig 12-bit bereik
// Input: 0-4095 (perceptueel), output: 0-4095 (lineaire PWM duty)
uint16_t gammaCorrectedDuty(float value) {
    if (value <= 0.0f)    return 0;
    if (value >= 4095.0f) return PWM_MAX_DUTY;
    float normalized = value / 4095.0f;
    float corrected  = powf(normalized, 2.8f);
    return static_cast<uint16_t>(roundf(corrected * PWM_MAX_DUTY));
}

void writePwm(uint8_t channel, uint16_t value) {
    ledcWrite(channel, value > PWM_MAX_DUTY ? PWM_MAX_DUTY : value);
}

void writePwmFloat(uint8_t channel, float value) {
    if (value <= 0.0f)                             { ledcWrite(channel, 0);            return; }
    if (value >= static_cast<float>(PWM_MAX_DUTY)) { ledcWrite(channel, PWM_MAX_DUTY); return; }
    ledcWrite(channel, static_cast<uint16_t>(roundf(value)));
}

void updateOutputs() {
    // Fade in perceptuele ruimte (0-4095): 2 sec van max naar 0 = 4095/40 ticks ≈ 102.4 per tick
    constexpr float MAX_STEP = 102.4f;

    if (gData.presetCount == 0) return;

    // Bij directe preview-waarden: outputs vasthouden, niet opnieuw berekenen
    if (previewActive && previewDirect) return;

    const Preset &active = gData.presets[gData.activePreset];
    float minute = getMinuteOfDay();
    moonlightCurrentlyActive = false;

    for (uint8_t ch = 0; ch < LED_CHANNEL_COUNT; ++ch) {
        float target = masterEnabled
            ? static_cast<float>(evaluateCurve(active.channels[ch], minute))
            : 0.0f;

        if (moonlightEnabled && moonlightChannel >= 0 && ch == static_cast<uint8_t>(moonlightChannel)) {
            if (masterEnabled) {
                const float moonTarget = moonlightIntensity * calcMoonPhase();
                if (moonTarget > target) {
                    target = moonTarget;
                    moonlightCurrentlyActive = true;
                }
            }
        }

        // Helderheidsscaling: schaal target met masterBrightness, limiteer op 4095
        target = fminf(target * masterBrightness, 4095.0f);

        // Wolken simulatie: per kanaal driehoek-profiel
        if (cloudCurrentDimPercent[ch] > 0)
            target *= (100.0f - cloudCurrentDimPercent[ch]) / 100.0f;

        float diff = target - smoothOutputs[ch];
        if (fabsf(diff) <= MAX_STEP)
            smoothOutputs[ch] = target;
        else
            smoothOutputs[ch] += (diff > 0.0f ? MAX_STEP : -MAX_STEP);

        // Gamma-correctie alleen bij het schrijven naar PWM
        writePwm(ch, gammaCorrectedDuty(smoothOutputs[ch]));
        currentOutputs[ch] = static_cast<uint16_t>(roundf(smoothOutputs[ch]));
    }
}

// ─── Debug output ────────────────────────────────────────────────────────────

void printStatusToSerial() {
    float minute = getMinuteOfDay();
    int hh = static_cast<int>(minute) / 60;
    int mm = static_cast<int>(minute) % 60;

    Serial.printf("[STAT] %02d:%02d | Preset %u: %s | Out:", hh, mm,
                  gData.activePreset, gData.presets[gData.activePreset].name.c_str());
    for (uint8_t ch = 0; ch < LED_CHANNEL_COUNT; ++ch) {
        uint16_t duty = ledcRead(ch);
        Serial.printf(" ch%u=%u(%u/4095)", ch + 1, currentOutputs[ch], duty);
    }
    if (moonlightEnabled && moonlightChannel >= 0) {
        float phase      = calcMoonPhase();
        float brightness = moonlightIntensity * phase / 4095.0f * 100.0f;
        Serial.printf(" | Maan: ch%d brandt op %.1f%% (fase %.0f%%)",
                      moonlightChannel + 1, brightness, phase * 100.0f);
    }
    Serial.println();
}

// ─── PWM setup ───────────────────────────────────────────────────────────────

void setupPwm() {
    for (uint8_t ch = 0; ch < LED_CHANNEL_COUNT; ++ch) {
        Serial.printf("[PWM] Init ch%u pin=%d\n", ch + 1, LED_PINS[ch]);
        ledcSetup(ch, PWM_FREQ, PWM_RESOLUTION);
        ledcAttachPin(LED_PINS[ch], ch);
        writePwm(ch, 0);
    }
    Serial.println("[PWM] Init klaar.");
}
