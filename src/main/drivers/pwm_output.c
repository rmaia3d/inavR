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
#include <math.h>
#include <string.h>

#include "platform.h"

#if !defined(SITL_BUILD)

#include "build/debug.h"

#include "common/log.h"
#include "common/maths.h"
#include "common/circular_queue.h"

#include "drivers/io.h"
#include "drivers/timer.h"
#include "drivers/pwm_mapping.h"
#include "drivers/pwm_output.h"
#include "io/servo_sbus.h"
#include "sensors/esc_sensor.h"

#include "config/feature.h"

#include "fc/config.h"
#include "fc/runtime_config.h"

#include "drivers/timer_impl.h"
#include "drivers/timer.h"

#define MULTISHOT_5US_PW    (MULTISHOT_TIMER_HZ * 5 / 1000000.0f)
#define MULTISHOT_20US_MULT (MULTISHOT_TIMER_HZ * 20 / 1000000.0f / 1000.0f)

#ifdef USE_DSHOT
#define MOTOR_DSHOT600_HZ     12000000
#define MOTOR_DSHOT300_HZ     6000000
#define MOTOR_DSHOT150_HZ     3000000


#define DSHOT_MOTOR_BIT_0       7
#define DSHOT_MOTOR_BIT_1       14
#define DSHOT_MOTOR_BITLENGTH   20

#define DSHOT_DMA_BUFFER_SIZE   18 /* resolution + frame reset (2us) */
#define MAX_DMA_TIMERS          8

#define DSHOT_COMMAND_DELAY_US 1000
#define DSHOT_COMMAND_INTERVAL_US 10000
#define DSHOT_COMMAND_QUEUE_LENGTH 8
#define DHSOT_COMMAND_QUEUE_SIZE   DSHOT_COMMAND_QUEUE_LENGTH * sizeof(dshotCommands_e)
#endif

typedef void (*pwmWriteFuncPtr)(uint8_t index, uint16_t value);  // function pointer used to write motors

#ifdef USE_DSHOT_DMAR
    timerDMASafeType_t dmaBurstBuffer[MAX_DMA_TIMERS][DSHOT_DMA_BUFFER_SIZE * 4];
#endif

typedef struct {
    TCH_t * tch;
    bool configured;
    uint16_t value;

    // PWM parameters
    volatile timCCR_t *ccr;         // Shortcut for timer CCR register
    float pulseOffset;
    float pulseScale;

#ifdef USE_DSHOT
    // DSHOT parameters
    timerDMASafeType_t dmaBuffer[DSHOT_DMA_BUFFER_SIZE];
#ifdef USE_DSHOT_DMAR
    timerDMASafeType_t *dmaBurstBuffer;
#endif
#endif
} pwmOutputPort_t;

typedef struct {
    pwmOutputPort_t *   pwmPort;        // May be NULL if motor doesn't use the PWM port
    uint16_t            value;          // Used to keep track of last motor value
    bool                requestTelemetry;
} pwmOutputMotor_t;

static DMA_RAM pwmOutputPort_t pwmOutputPorts[MAX_PWM_OUTPUTS];

static pwmOutputMotor_t        motors[MAX_MOTORS];
static motorPwmProtocolTypes_e initMotorProtocol;
static pwmWriteFuncPtr         motorWritePtr = NULL;    // Function to write value to motors

static pwmOutputPort_t *       servos[MAX_SERVOS];
static pwmWriteFuncPtr         servoWritePtr = NULL;    // Function to write value to motors

static pwmOutputPort_t  beeperPwmPort;
static pwmOutputPort_t *beeperPwm;
static uint16_t beeperFrequency = 0;

static uint8_t allocatedOutputPortCount = 0;

static bool pwmMotorsEnabled = true;

#ifdef USE_DSHOT
static timeUs_t digitalMotorUpdateIntervalUs = 0;
static timeUs_t digitalMotorLastUpdateUs;
static timeUs_t lastCommandSent = 0;
static timeUs_t commandPostDelay = 0;
    
static circularBuffer_t commandsCircularBuffer;
static uint8_t commandsBuff[DHSOT_COMMAND_QUEUE_SIZE];
static currentExecutingCommand_t currentExecutingCommand;
#endif

static void pwmOutConfigTimer(pwmOutputPort_t * p, TCH_t * tch, uint32_t hz, uint16_t period, uint16_t value)
{
    p->tch = tch;

    timerConfigBase(p->tch, period, hz);
    timerPWMConfigChannel(p->tch, value);
    timerPWMStart(p->tch);

    timerEnable(p->tch);

    p->ccr = timerCCR(p->tch);
    *p->ccr = 0;
}

static pwmOutputPort_t *pwmOutAllocatePort(void)
{
    if (allocatedOutputPortCount >= MAX_PWM_OUTPUTS) {
        LOG_ERROR(PWM, "Attempt to allocate PWM output beyond MAX_PWM_OUTPUT_PORTS");
        return NULL;
    }

    pwmOutputPort_t *p = &pwmOutputPorts[allocatedOutputPortCount++];

    p->tch = NULL;
    p->configured = false;

    return p;
}

static pwmOutputPort_t *pwmOutConfig(const timerHardware_t *timHw, resourceOwner_e owner, uint32_t hz, uint16_t period, uint16_t value, bool enableOutput)
{
    // Attempt to allocate TCH
    TCH_t * tch = timerGetTCH(timHw);
    if (tch == NULL) {
        return NULL;
    }

    // Allocate motor output port
    pwmOutputPort_t *p = pwmOutAllocatePort();
    if (p == NULL) {
        return NULL;
    }

    const IO_t io = IOGetByTag(timHw->tag);
    IOInit(io, owner, RESOURCE_OUTPUT, allocatedOutputPortCount);

    pwmOutConfigTimer(p, tch, hz, period, value);

    if (enableOutput) {
        IOConfigGPIOAF(io, IOCFG_AF_PP, timHw->alternateFunction);
    }
    else {
        // If PWM outputs are disabled - configure as GPIO and drive low
        IOConfigGPIO(io, IOCFG_OUT_OD);
        IOLo(io);
    }

    return p;
}

static void pwmWriteNull(uint8_t index, uint16_t value)
{
    (void)index;
    (void)value;
}

static void pwmWriteStandard(uint8_t index, uint16_t value)
{
    if (motors[index].pwmPort) {
        *(motors[index].pwmPort->ccr) = lrintf((value * motors[index].pwmPort->pulseScale) + motors[index].pwmPort->pulseOffset);
    }
}

void pwmWriteMotor(uint8_t index, uint16_t value)
{
    if (motorWritePtr && index < MAX_MOTORS && pwmMotorsEnabled) {
        motorWritePtr(index, value);
    }
}

void pwmShutdownPulsesForAllMotors(uint8_t motorCount)
{
    for (int index = 0; index < motorCount; index++) {
        // Set the compare register to 0, which stops the output pulsing if the timer overflows
        if (motors[index].pwmPort) {
            *(motors[index].pwmPort->ccr) = 0;
        }
    }
}

void pwmDisableMotors(void)
{
    pwmMotorsEnabled = false;
}

void pwmEnableMotors(void)
{
    pwmMotorsEnabled = true;
}

bool isMotorBrushed(uint16_t motorPwmRateHz)
{
    return (motorPwmRateHz > 500);
}

static pwmOutputPort_t * motorConfigPwm(const timerHardware_t *timerHardware, float sMin, float sLen, uint32_t motorPwmRateHz, bool enableOutput)
{
    const uint32_t baseClockHz = timerGetBaseClockHW(timerHardware);
    const uint32_t prescaler = ((baseClockHz / motorPwmRateHz) + 0xffff) / 0x10000; /* rounding up */
    const uint32_t timerHz = baseClockHz / prescaler;
    const uint32_t period = timerHz / motorPwmRateHz;

    pwmOutputPort_t * port = pwmOutConfig(timerHardware, OWNER_MOTOR, timerHz, period, 0, enableOutput);

    if (port) {
        port->pulseScale = ((sLen == 0) ? period : (sLen * timerHz)) / 1000.0f;
        port->pulseOffset = (sMin * timerHz) - (port->pulseScale * 1000);
        port->configured = true;
    }

    return port;
}

#ifdef USE_DSHOT
uint32_t getDshotHz(motorPwmProtocolTypes_e pwmProtocolType)
{
    switch (pwmProtocolType) {
        case(PWM_TYPE_DSHOT600):
            return MOTOR_DSHOT600_HZ;
        case(PWM_TYPE_DSHOT300):
            return MOTOR_DSHOT300_HZ;
        default:
        case(PWM_TYPE_DSHOT150):
            return MOTOR_DSHOT150_HZ;
    }
}

#ifdef USE_DSHOT_DMAR
burstDmaTimer_t burstDmaTimers[MAX_DMA_TIMERS];
uint8_t burstDmaTimersCount = 0;

static uint8_t getBurstDmaTimerIndex(TIM_TypeDef *timer)
{
    for (int i = 0; i < burstDmaTimersCount; i++) {
        if (burstDmaTimers[i].timer == timer) {
            return i;
        }
    }
    burstDmaTimers[burstDmaTimersCount++].timer = timer;
    return burstDmaTimersCount - 1;
}
#endif

static pwmOutputPort_t * motorConfigDshot(const timerHardware_t * timerHardware, uint32_t dshotHz, bool enableOutput)
{
    // Try allocating new port
    pwmOutputPort_t * port = pwmOutConfig(timerHardware, OWNER_MOTOR, dshotHz, DSHOT_MOTOR_BITLENGTH, 0, enableOutput);

    if (!port) {
        return NULL;
    }

    // Configure timer DMA
#ifdef USE_DSHOT_DMAR
    //uint8_t timerIndex = lookupTimerIndex(timerHardware->tim);
    uint8_t burstDmaTimerIndex = getBurstDmaTimerIndex(timerHardware->tim);
    if (burstDmaTimerIndex >= MAX_DMA_TIMERS) {
        return NULL;
    }

    port->dmaBurstBuffer = &dmaBurstBuffer[burstDmaTimerIndex][0];
    burstDmaTimer_t *burstDmaTimer = &burstDmaTimers[burstDmaTimerIndex];
    burstDmaTimer->dmaBurstBuffer = port->dmaBurstBuffer;

    if (timerPWMConfigDMABurst(burstDmaTimer, port->tch, port->dmaBurstBuffer, sizeof(port->dmaBurstBuffer[0]), DSHOT_DMA_BUFFER_SIZE)) {
        port->configured = true;
    }
#else
    if (timerPWMConfigChannelDMA(port->tch, port->dmaBuffer, sizeof(port->dmaBuffer[0]), DSHOT_DMA_BUFFER_SIZE)) {
        // Only mark as DSHOT channel if DMA was set successfully
        ZERO_FARRAY(port->dmaBuffer);
        port->configured = true;
    }
#endif

    return port;
}

#ifdef USE_DSHOT_DMAR
static void loadDmaBufferDshotStride(timerDMASafeType_t *dmaBuffer, int stride, uint16_t packet)
{
    int i;
    for (i = 0; i < 16; i++) {
        dmaBuffer[i * stride] = (packet & 0x8000) ? DSHOT_MOTOR_BIT_1 : DSHOT_MOTOR_BIT_0;  // MSB first
        packet <<= 1;
    }
    dmaBuffer[i++ * stride] = 0;
    dmaBuffer[i++ * stride] = 0;
}
#else
static void loadDmaBufferDshot(timerDMASafeType_t *dmaBuffer, uint16_t packet)
{
    for (int i = 0; i < 16; i++) {
        dmaBuffer[i] = (packet & 0x8000) ? DSHOT_MOTOR_BIT_1 : DSHOT_MOTOR_BIT_0;  // MSB first
        packet <<= 1;
    }
}
#endif

static uint16_t prepareDshotPacket(const uint16_t value, bool requestTelemetry)
{
    uint16_t packet = (value << 1) | (requestTelemetry ? 1 : 0);

    // compute checksum
    int csum = 0;
    int csum_data = packet;
    for (int i = 0; i < 3; i++) {
        csum ^=  csum_data;   // xor data by nibbles
        csum_data >>= 4;
    }
    csum &= 0xf;

    // append checksum
    packet = (packet << 4) | csum;

    return packet;
}
#endif

#if defined(USE_DSHOT)
static void motorConfigDigitalUpdateInterval(uint16_t motorPwmRateHz)
{
    digitalMotorUpdateIntervalUs = 1000000 / motorPwmRateHz;
    digitalMotorLastUpdateUs = 0;
}

static void pwmWriteDigital(uint8_t index, uint16_t value)
{
    // Just keep track of motor value, actual update happens in pwmCompleteMotorUpdate()
    // DSHOT and some other digital protocols use 11-bit throttle range [0;2047]
    motors[index].value = constrain(value, 0, 2047);
}

bool isMotorProtocolDshot(void)
{
    // We look at cached `initMotorProtocol` to make sure we are consistent with the initialized config
    // motorConfig()->motorPwmProtocol may change at run time which will cause uninitialized structures to be used
    return getMotorProtocolProperties(initMotorProtocol)->isDSHOT;
}

bool isMotorProtocolDigital(void)
{
    return isMotorProtocolDshot();
}

void pwmRequestMotorTelemetry(int motorIndex)
{
    if (!isMotorProtocolDigital()) {
        return;
    }

    const int motorCount = getMotorCount();
    for (int index = 0; index < motorCount; index++) {
        if (motors[index].pwmPort && motors[index].pwmPort->configured && index == motorIndex) {
            motors[index].requestTelemetry = true;
        }
    }
}

#ifdef USE_DSHOT
void sendDShotCommand(dshotCommands_e cmd) {
    circularBufferPushElement(&commandsCircularBuffer, (uint8_t *) &cmd);
}

void initDShotCommands(void) {
    circularBufferInit(&commandsCircularBuffer, commandsBuff,DHSOT_COMMAND_QUEUE_SIZE, sizeof(dshotCommands_e));

    currentExecutingCommand.remainingRepeats = 0;
}

static int getDShotCommandRepeats(dshotCommands_e cmd) {
    int repeats = 1;

    switch (cmd) {
        case DSHOT_CMD_SPIN_DIRECTION_NORMAL:
        case DSHOT_CMD_SPIN_DIRECTION_REVERSED:
            repeats = 10;
            break;
        default:
            break;
    }

    return repeats;
}

static bool executeDShotCommands(void){
    
    timeUs_t tNow = micros();

    if(currentExecutingCommand.remainingRepeats == 0) {
       const int isTherePendingCommands = !circularBufferIsEmpty(&commandsCircularBuffer);
        if (isTherePendingCommands && (tNow - lastCommandSent > DSHOT_COMMAND_INTERVAL_US)){
            //Load the command
            dshotCommands_e cmd;
            circularBufferPopHead(&commandsCircularBuffer, (uint8_t *) &cmd);
            currentExecutingCommand.cmd = cmd;
            currentExecutingCommand.remainingRepeats = getDShotCommandRepeats(cmd);
            commandPostDelay = DSHOT_COMMAND_INTERVAL_US;
        } else {
            if (commandPostDelay) {
                if (tNow - lastCommandSent < commandPostDelay) {
                    return false;
                }
                commandPostDelay = 0;
            }

            return true;
        }  
    }
    for (uint8_t i = 0; i < getMotorCount(); i++) {
         motors[i].requestTelemetry = true;
         motors[i].value = currentExecutingCommand.cmd;
    }
    if (tNow - lastCommandSent >= DSHOT_COMMAND_DELAY_US) {
        currentExecutingCommand.remainingRepeats--; 
        lastCommandSent = tNow;
        return true;
    } else {
        return false;
    }
}
#endif

void pwmCompleteMotorUpdate(void) {
    // This only makes sense for digital motor protocols
    if (!isMotorProtocolDigital()) {
        return;
    }

    int motorCount = getMotorCount();
    timeUs_t currentTimeUs = micros();

    // Enforce motor update rate
    if ((digitalMotorUpdateIntervalUs == 0) || ((currentTimeUs - digitalMotorLastUpdateUs) <= digitalMotorUpdateIntervalUs)) {
        return;
    }

    digitalMotorLastUpdateUs = currentTimeUs;

#ifdef USE_DSHOT
    if (isMotorProtocolDshot()) {

        if (!executeDShotCommands()) {
            return;
        }

#ifdef USE_DSHOT_DMAR
        for (int index = 0; index < motorCount; index++) {
            if (motors[index].pwmPort && motors[index].pwmPort->configured) {
                uint16_t packet = prepareDshotPacket(motors[index].value, motors[index].requestTelemetry);
                loadDmaBufferDshotStride(&motors[index].pwmPort->dmaBurstBuffer[motors[index].pwmPort->tch->timHw->channelIndex], 4, packet);
                motors[index].requestTelemetry = false;
            }
        }

        for (int burstDmaTimerIndex = 0; burstDmaTimerIndex < burstDmaTimersCount; burstDmaTimerIndex++) {
            burstDmaTimer_t *burstDmaTimer = &burstDmaTimers[burstDmaTimerIndex];
            pwmBurstDMAStart(burstDmaTimer, DSHOT_DMA_BUFFER_SIZE * 4);
        }
#else
        // Generate DMA buffers
        for (int index = 0; index < motorCount; index++) {
            if (motors[index].pwmPort && motors[index].pwmPort->configured) {
                uint16_t packet = prepareDshotPacket(motors[index].value, motors[index].requestTelemetry);
                loadDmaBufferDshot(motors[index].pwmPort->dmaBuffer, packet);
                timerPWMPrepareDMA(motors[index].pwmPort->tch, DSHOT_DMA_BUFFER_SIZE);
                motors[index].requestTelemetry = false;
            }
        }

        // Start DMA on all timers
        for (int index = 0; index < motorCount; index++) {
            if (motors[index].pwmPort && motors[index].pwmPort->configured) {
                timerPWMStartDMA(motors[index].pwmPort->tch);
            }
        }
#endif
    }
#endif
}

#else // digital motor protocol

// This stub is needed to avoid ESC_SENSOR dependency on DSHOT
void pwmRequestMotorTelemetry(int motorIndex)
{
    UNUSED(motorIndex);
}

#endif

void pwmMotorPreconfigure(void)
{
    // Keep track of initial motor protocol
    initMotorProtocol = motorConfig()->motorPwmProtocol;

#ifdef BRUSHED_MOTORS
    initMotorProtocol = PWM_TYPE_BRUSHED;   // Override proto
#endif

    // Protocol-specific configuration
    switch (initMotorProtocol) {
        default:
            motorWritePtr = pwmWriteNull;
            break;

        case PWM_TYPE_STANDARD:
        case PWM_TYPE_BRUSHED:
        case PWM_TYPE_ONESHOT125:
        case PWM_TYPE_MULTISHOT:
            motorWritePtr = pwmWriteStandard;
            break;

#ifdef USE_DSHOT
        case PWM_TYPE_DSHOT600:
        case PWM_TYPE_DSHOT300:
        case PWM_TYPE_DSHOT150:
            motorConfigDigitalUpdateInterval(getEscUpdateFrequency());
            motorWritePtr = pwmWriteDigital;
            break;
#endif
    }
}

/**
 * This function return the PWM frequency based on ESC protocol. We allow customer rates only for Brushed motors
 */ 
uint32_t getEscUpdateFrequency(void) {
    switch (initMotorProtocol) {
        case PWM_TYPE_BRUSHED:
            return motorConfig()->motorPwmRate;

        case PWM_TYPE_STANDARD:
            return 400;

        case PWM_TYPE_MULTISHOT:
            return 2000;

        case PWM_TYPE_DSHOT150:
            return 4000;

        case PWM_TYPE_DSHOT300:
            return 8000;

        case PWM_TYPE_DSHOT600:
            return 16000;

        case PWM_TYPE_ONESHOT125:
        default:
            return 1000;

    }
}

bool pwmMotorConfig(const timerHardware_t *timerHardware, uint8_t motorIndex, bool enableOutput)
{
    switch (initMotorProtocol) {
    case PWM_TYPE_BRUSHED:
        motors[motorIndex].pwmPort = motorConfigPwm(timerHardware, 0.0f, 0.0f, getEscUpdateFrequency(), enableOutput);
        break;

    case PWM_TYPE_ONESHOT125:
        motors[motorIndex].pwmPort = motorConfigPwm(timerHardware, 125e-6f, 125e-6f, getEscUpdateFrequency(), enableOutput);
        break;

    case PWM_TYPE_MULTISHOT:
        motors[motorIndex].pwmPort = motorConfigPwm(timerHardware, 5e-6f, 20e-6f, getEscUpdateFrequency(), enableOutput);
        break;

#ifdef USE_DSHOT
    case PWM_TYPE_DSHOT600:
    case PWM_TYPE_DSHOT300:
    case PWM_TYPE_DSHOT150:
        motors[motorIndex].pwmPort = motorConfigDshot(timerHardware, getDshotHz(initMotorProtocol), enableOutput);
        break;
#endif

    case PWM_TYPE_STANDARD:
        motors[motorIndex].pwmPort = motorConfigPwm(timerHardware, 1e-3f, 1e-3f, getEscUpdateFrequency(), enableOutput);
        break;

    default:
        motors[motorIndex].pwmPort = NULL;
        break;
    }

    return (motors[motorIndex].pwmPort != NULL);
}

// Helper function for ESC passthrough
ioTag_t pwmGetMotorPinTag(int motorIndex)
{
    if (motors[motorIndex].pwmPort) {
        return motors[motorIndex].pwmPort->tch->timHw->tag;
    }
    else {
        return IOTAG_NONE;
    }
}

static void pwmServoWriteStandard(uint8_t index, uint16_t value)
{
    if (servos[index]) {
        *servos[index]->ccr = value;
    }
}

#ifdef USE_SERVO_SBUS
static void sbusPwmWriteStandard(uint8_t index, uint16_t value)
{
    pwmServoWriteStandard(index, value);
    sbusServoUpdate(index, value);
}
#endif

void pwmServoPreconfigure(void)
{
    // Protocol-specific configuration
    switch (servoConfig()->servo_protocol) {
        default:
        case SERVO_TYPE_PWM:
            servoWritePtr = pwmServoWriteStandard;
            break;

#ifdef USE_SERVO_SBUS
        case SERVO_TYPE_SBUS:
            sbusServoInitialize();
            servoWritePtr = sbusServoUpdate;
            break;

        case SERVO_TYPE_SBUS_PWM:
            sbusServoInitialize();
            servoWritePtr = sbusPwmWriteStandard;
            break;
#endif
    }
}

bool pwmServoConfig(const timerHardware_t *timerHardware, uint8_t servoIndex, uint16_t servoPwmRate, uint16_t servoCenterPulse, bool enableOutput)
{
    pwmOutputPort_t * port = pwmOutConfig(timerHardware, OWNER_SERVO, PWM_TIMER_HZ, PWM_TIMER_HZ / servoPwmRate, servoCenterPulse, enableOutput);

    if (port) {
        servos[servoIndex] = port;
        return true;
    }

    return false;
}

void pwmWriteServo(uint8_t index, uint16_t value)
{
    if (servoWritePtr && index < MAX_SERVOS) {
        servoWritePtr(index, value);
    }
}

void pwmWriteBeeper(bool onoffBeep)
{
    if (beeperPwm == NULL)
        return;

    if (onoffBeep == true) {
        *beeperPwm->ccr = (1000000 / beeperFrequency) / 2;
    } else {
        *beeperPwm->ccr = 0;
    }
}

void beeperPwmInit(ioTag_t tag, uint16_t frequency)
{
    beeperPwm = NULL;

    const timerHardware_t *timHw = timerGetByTag(tag, TIM_USE_BEEPER);

    if (timHw) {
        // Attempt to allocate TCH
        TCH_t * tch = timerGetTCH(timHw);
        if (tch == NULL) {
            return;
        }

        beeperPwm = &beeperPwmPort;
        beeperFrequency = frequency;
        IOConfigGPIOAF(IOGetByTag(tag), IOCFG_AF_PP, timHw->alternateFunction);
        pwmOutConfigTimer(beeperPwm, tch, PWM_TIMER_HZ, 1000000 / beeperFrequency, (1000000 / beeperFrequency) / 2);
    }
}

#endif
