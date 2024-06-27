/*
 * This file is part of Cleanflight.
 *
 * Cleanflight is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Cleanflight is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Cleanflight.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#include <platform.h>

#include "blackbox/blackbox.h"
#include "blackbox/blackbox_fielddefs.h"

#include "build/debug.h"

#include "common/axis.h"
#include "common/maths.h"
#include "common/utils.h"

#include "drivers/time.h"

#include "config/feature.h"
#include "config/parameter_group.h"
#include "config/parameter_group_ids.h"

#include "fc/config.h"
#include "fc/controlrate_profile.h"
#include "fc/rc_controls.h"
#include "fc/rc_modes.h"
#include "fc/rc_adjustments.h"
#include "fc/runtime_config.h"
#include "fc/settings.h"

#include "flight/pid.h"

#define AUTOTUNE_FIXED_WING_MIN_FF              10
#define AUTOTUNE_FIXED_WING_MAX_FF              255
#define AUTOTUNE_FIXED_WING_MIN_ROLL_PITCH_RATE 40
#define AUTOTUNE_FIXED_WING_MIN_YAW_RATE        10
#define AUTOTUNE_FIXED_WING_MAX_RATE            720
#define AUTOTUNE_FIXED_WING_CONVERGENCE_RATE    10
#define AUTOTUNE_FIXED_WING_SAMPLE_INTERVAL     20      // ms
#define AUTOTUNE_FIXED_WING_SAMPLES             1000    // Use average over the last 20 seconds of hard maneuvers
#define AUTOTUNE_FIXED_WING_MIN_SAMPLES         250     // Start updating tune after 5 seconds of hard maneuvers

PG_REGISTER_WITH_RESET_TEMPLATE(pidAutotuneConfig_t, pidAutotuneConfig, PG_PID_AUTOTUNE_CONFIG, 2);

PG_RESET_TEMPLATE(pidAutotuneConfig_t, pidAutotuneConfig,
    .fw_min_stick = SETTING_FW_AUTOTUNE_MIN_STICK_DEFAULT,
    .fw_rate_adjustment = SETTING_FW_AUTOTUNE_RATE_ADJUSTMENT_DEFAULT,
    .fw_max_rate_deflection = SETTING_FW_AUTOTUNE_MAX_RATE_DEFLECTION_DEFAULT,
);

typedef enum {
    DEMAND_TOO_LOW,
    DEMAND_UNDERSHOOT,
    DEMAND_OVERSHOOT,
    TUNE_UPDATED,
} pidAutotuneState_e;

typedef struct {
    float   gainFF;
    float   rate;
    float   initialRate;
    float   absDesiredRateAccum;
    float   absReachedRateAccum;
    float   absPidOutputAccum;
    uint32_t updateCount;
} pidAutotuneData_t;

#define AUTOTUNE_SAVE_PERIOD        5000        // Save interval is 5 seconds - when we turn off autotune we'll restore values from previous update at most 5 sec ago

#if defined(USE_AUTOTUNE_FIXED_WING) || defined(USE_AUTOTUNE_MULTIROTOR)

static pidAutotuneData_t    tuneCurrent[XYZ_AXIS_COUNT];
static pidAutotuneData_t    tuneSaved[XYZ_AXIS_COUNT];
static timeMs_t             lastGainsUpdateTime;

void autotuneUpdateGains(pidAutotuneData_t * data)
{
    for (int axis = 0; axis < XYZ_AXIS_COUNT; axis++) {
        pidBankMutable()->pid[axis].FF = lrintf(data[axis].gainFF);
        ((controlRateConfig_t *)currentControlRateProfile)->stabilized.rates[axis] = lrintf(data[axis].rate/10.0f);
    }
    schedulePidGainsUpdate();
}

void autotuneCheckUpdateGains(void)
{
    const timeMs_t currentTimeMs = millis();

    if ((currentTimeMs - lastGainsUpdateTime) < AUTOTUNE_SAVE_PERIOD) {
        return;
    }

    // If pilot will exit autotune we'll restore values we are flying now
    memcpy(tuneSaved, tuneCurrent, sizeof(pidAutotuneData_t) * XYZ_AXIS_COUNT);
    autotuneUpdateGains(tuneSaved);
    lastGainsUpdateTime = currentTimeMs;
}

void autotuneStart(void)
{
    for (int axis = 0; axis < XYZ_AXIS_COUNT; axis++) {
        tuneCurrent[axis].gainFF = pidBank()->pid[axis].FF;
        tuneCurrent[axis].rate = currentControlRateProfile->stabilized.rates[axis] * 10.0f;
        tuneCurrent[axis].initialRate = currentControlRateProfile->stabilized.rates[axis] * 10.0f;
        tuneCurrent[axis].absDesiredRateAccum = 0;
        tuneCurrent[axis].absReachedRateAccum = 0;
        tuneCurrent[axis].absPidOutputAccum = 0;
        tuneCurrent[axis].updateCount = 0;
    }

    memcpy(tuneSaved, tuneCurrent, sizeof(pidAutotuneData_t) * XYZ_AXIS_COUNT);
    lastGainsUpdateTime = millis();
}

void autotuneUpdateState(void)
{
    if (isFwAutoModeActive(BOXAUTOTUNE) && STATE(AIRPLANE) && ARMING_FLAG(ARMED)) {
        if (!FLIGHT_MODE(AUTO_TUNE)) {
            autotuneStart();
            ENABLE_FLIGHT_MODE(AUTO_TUNE);
        }
        else {
            autotuneCheckUpdateGains();
        }
    } else {
        if (FLIGHT_MODE(AUTO_TUNE)) {
            autotuneUpdateGains(tuneSaved);
        }

        DISABLE_FLIGHT_MODE(AUTO_TUNE);
    }
}

static void blackboxLogAutotuneEvent(adjustmentFunction_e adjustmentFunction, int32_t newValue)
{
#ifndef USE_BLACKBOX
    UNUSED(adjustmentFunction);
    UNUSED(newValue);
#else
    if (feature(FEATURE_BLACKBOX)) {
        flightLogEvent_inflightAdjustment_t eventData;
        eventData.adjustmentFunction = adjustmentFunction;
        eventData.newValue = newValue;
        eventData.floatFlag = false;
        blackboxLogEvent(FLIGHT_LOG_EVENT_INFLIGHT_ADJUSTMENT, (flightLogEventData_t*)&eventData);
    }
#endif
}

#if defined(USE_AUTOTUNE_FIXED_WING)

void autotuneFixedWingUpdate(const flight_dynamics_index_t axis, float desiredRate, float reachedRate, float pidOutput)
{
    float maxRateSetting = tuneCurrent[axis].rate;
    float gainFF = tuneCurrent[axis].gainFF;
    float maxDesiredRate = maxRateSetting;

    const float pidSumLimit = getPidSumLimit(axis);
    const float absDesiredRate = fabsf(desiredRate);
    const float absReachedRate = fabsf(reachedRate);
    const float absPidOutput = fabsf(pidOutput);
    const bool correctDirection = (desiredRate>0) == (reachedRate>0);
    float rateFullStick;

    bool gainsUpdated = false;
    bool ratesUpdated = false;

    const timeMs_t currentTimeMs = millis();
    static timeMs_t previousSampleTimeMs = 0;
    const timeDelta_t timeSincePreviousSample = currentTimeMs - previousSampleTimeMs;

    // Use different max rate in ANLGE mode
    if (FLIGHT_MODE(ANGLE_MODE)) {
        float maxDesiredRateInAngleMode = DECIDEGREES_TO_DEGREES(pidProfile()->max_angle_inclination[axis] * 1.0f) * pidBank()->pid[PID_LEVEL].P * FP_PID_LEVEL_P_MULTIPLIER;
        maxDesiredRate = MIN(maxRateSetting, maxDesiredRateInAngleMode);
    }

    const float stickInput = absDesiredRate / maxDesiredRate;

    if ((stickInput > (pidAutotuneConfig()->fw_min_stick / 100.0f)) && correctDirection && (timeSincePreviousSample >= AUTOTUNE_FIXED_WING_SAMPLE_INTERVAL)) {
        // Record values every 20ms and calculate moving average over samples
        tuneCurrent[axis].updateCount++;
        tuneCurrent[axis].absDesiredRateAccum += (absDesiredRate - tuneCurrent[axis].absDesiredRateAccum) / MIN(tuneCurrent[axis].updateCount, (uint32_t)AUTOTUNE_FIXED_WING_SAMPLES);
        tuneCurrent[axis].absReachedRateAccum += (absReachedRate - tuneCurrent[axis].absReachedRateAccum) / MIN(tuneCurrent[axis].updateCount, (uint32_t)AUTOTUNE_FIXED_WING_SAMPLES);
        tuneCurrent[axis].absPidOutputAccum += (absPidOutput - tuneCurrent[axis].absPidOutputAccum) / MIN(tuneCurrent[axis].updateCount, (uint32_t)AUTOTUNE_FIXED_WING_SAMPLES);

        if ((tuneCurrent[axis].updateCount & 25) == 0 && tuneCurrent[axis].updateCount >= AUTOTUNE_FIXED_WING_MIN_SAMPLES) {
            if (pidAutotuneConfig()->fw_rate_adjustment != FIXED  && !FLIGHT_MODE(ANGLE_MODE)) { // Rate discovery is not possible in ANGLE mode

                // Target 80% control surface deflection to leave some room for P and I to work
                float pidSumTarget = (pidAutotuneConfig()->fw_max_rate_deflection / 100.0f) * pidSumLimit;

                // Theoretically achievable rate with target deflection
                rateFullStick = pidSumTarget / tuneCurrent[axis].absPidOutputAccum * tuneCurrent[axis].absReachedRateAccum;

                // Rate update
                if (rateFullStick > (maxRateSetting + 10.0f)) {
                    maxRateSetting += 10.0f;
                } else if (rateFullStick < (maxRateSetting - 10.0f)) {
                    maxRateSetting -= 10.0f;
                }

                // Constrain to safe values
                uint16_t minRate = (axis == FD_YAW) ? AUTOTUNE_FIXED_WING_MIN_YAW_RATE : AUTOTUNE_FIXED_WING_MIN_ROLL_PITCH_RATE;
                uint16_t maxRate = (pidAutotuneConfig()->fw_rate_adjustment == AUTO) ? AUTOTUNE_FIXED_WING_MAX_RATE : MAX(tuneCurrent[axis].initialRate, minRate);
                tuneCurrent[axis].rate = constrainf(maxRateSetting, minRate, maxRate);
                ratesUpdated = true;
            }

            // Update FF towards value needed to achieve current rate target
            gainFF += (tuneCurrent[axis].absPidOutputAccum / tuneCurrent[axis].absReachedRateAccum * FP_PID_RATE_FF_MULTIPLIER - gainFF) * (AUTOTUNE_FIXED_WING_CONVERGENCE_RATE / 100.0f);
            tuneCurrent[axis].gainFF = constrainf(gainFF, AUTOTUNE_FIXED_WING_MIN_FF, AUTOTUNE_FIXED_WING_MAX_FF);
            gainsUpdated = true;
        }

        // Reset timer
        previousSampleTimeMs = currentTimeMs;
    }

    if (gainsUpdated) {
        autotuneUpdateGains(tuneCurrent);

        switch (axis) {
            case FD_ROLL:
                blackboxLogAutotuneEvent(ADJUSTMENT_ROLL_FF, tuneCurrent[axis].gainFF);
                break;

            case FD_PITCH:
                blackboxLogAutotuneEvent(ADJUSTMENT_PITCH_FF, tuneCurrent[axis].gainFF);
                break;

            case FD_YAW:
                blackboxLogAutotuneEvent(ADJUSTMENT_YAW_FF, tuneCurrent[axis].gainFF);
                break;
        }
        gainsUpdated = false;
    }

    if (ratesUpdated) {
        autotuneUpdateGains(tuneCurrent);

        switch (axis) {
            case FD_ROLL:
                blackboxLogAutotuneEvent(ADJUSTMENT_ROLL_RATE, tuneCurrent[axis].rate);
                break;

            case FD_PITCH:
                blackboxLogAutotuneEvent(ADJUSTMENT_PITCH_RATE, tuneCurrent[axis].rate);
                break;

            case FD_YAW:
                blackboxLogAutotuneEvent(ADJUSTMENT_YAW_RATE, tuneCurrent[axis].rate);
                break;
        }

        // Debug
        DEBUG_SET(DEBUG_AUTOTUNE, axis, rateFullStick);
        ratesUpdated = false;
    }
}
#endif

#endif
