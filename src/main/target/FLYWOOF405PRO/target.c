/*
 * This file is part of INAV.
 *
 * INAV is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * INAV is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with INAV.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdbool.h>
#include <platform.h>
#include "drivers/io.h"
#include "drivers/pwm_mapping.h"
#include "drivers/timer.h"

timerHardware_t timerHardware[] = {
    DEF_TIM(TIM3,  CH3, PB0,  TIM_USE_OUTPUT_AUTO,   0, 0), // S1 D(1,3,2)
    DEF_TIM(TIM3,  CH4, PB1,  TIM_USE_OUTPUT_AUTO,   0, 0), // S2 D(1,0,2)
    DEF_TIM(TIM2,  CH4, PA3,  TIM_USE_OUTPUT_AUTO,   0, 1), // S3 D(1,7,5)
    DEF_TIM(TIM2,  CH3, PA2,  TIM_USE_OUTPUT_AUTO,   0, 0), // S4 D(1,2,5)
    DEF_TIM(TIM3,  CH2, PB5,  TIM_USE_OUTPUT_AUTO,   0, 0), // S5 D(2,4,7)
    DEF_TIM(TIM8,  CH4, PC9,  TIM_USE_OUTPUT_AUTO,   0, 0), // S6
    DEF_TIM(TIM3,  CH1, PB4,  TIM_USE_OUTPUT_AUTO,   0, 0), // S7
    DEF_TIM(TIM8,  CH3, PC8,  TIM_USE_OUTPUT_AUTO,   0, 0), // S8
    DEF_TIM(TIM1,  CH2, PA9,  TIM_USE_LED,   0, 0), //2812LED  D(1,5,3)
    // DEF_TIM(TIM10, CH1, PB8,  TIM_USE_PPM,   0, 0), // PPM
};

const int timerHardwareCount = sizeof(timerHardware) / sizeof(timerHardware[0]);
