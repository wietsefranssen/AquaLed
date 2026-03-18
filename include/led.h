#pragma once

#include "globals.h"

float    calcMoonPhase();
void     sortAndNormalizeCurve(ChannelCurve &curve);
void     fillDefaultPreset(Preset &preset, const String &name);
void     initDefaultData();
uint16_t evaluateCurve(const ChannelCurve &curve, float minuteOfDay);
uint16_t gammaCorrectedDuty(float value);
void     writePwm(uint8_t channel, uint16_t value);
void     writePwmFloat(uint8_t channel, float value);
void     updateOutputs();
void     printStatusToSerial();
void     setupPwm();
