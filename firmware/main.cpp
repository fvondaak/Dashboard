#include <Arduino.h>

// ADC input pin for channel 0 voltage measurement.
constexpr uint8_t ADC0_PIN = A0;
// ADC input pin for channel 1 voltage measurement.
constexpr uint8_t ADC1_PIN = A1;
// Digital output pin for valve channel 0
constexpr uint8_t ACT0_PIN = 8;
// Digital output pin for valve channel 1.
constexpr uint8_t ACT1_PIN = 9;
// On-board LED pin for heartbeat indication.
constexpr uint8_t HEARTBEAT_LED_PIN = LED_BUILTIN;

// Scheduler tick period in milliseconds.
constexpr uint16_t TICK_MS = 10;
// Repeating actuator frame duration in milliseconds.
constexpr uint16_t FRAME_MS = 2000;
// Number of scheduler ticks in one frame.
constexpr uint16_t FRAME_TICKS = FRAME_MS / TICK_MS;

// Number of pattern segments per channel.
constexpr uint8_t PATTERN_SEGMENTS = 4;
// Number of time slots in each segment string.
constexpr uint8_t SEGMENT_LENGTH = 50;
// Total number of frame slots represented by one channel pattern.
constexpr uint16_t TOTAL_SLOTS = static_cast<uint16_t>(PATTERN_SEGMENTS) * SEGMENT_LENGTH;

// One transmitted sample with ADC readings and output states.
struct Sample {
  int16_t adc0;
  int16_t adc1;
  bool act0;
  bool act1;
};

const char *pattern0[PATTERN_SEGMENTS] = {
    "11001100110011001100110011001100110011001100110011", // 0-499 ms
    "00110011001100110011001100110011001100110011001100", // 500-999 ms
    "11001100110011001100110011001100110011001100110011", // 1000-1499 ms
    "00110011001100110011001100110011001100110011001100", // 1500-1999 ms
};

const char *pattern1[PATTERN_SEGMENTS] = {
    "00110011001100110011001100110011001100110011001100", // 0-499 ms
    "11001100110011001100110011001100110011001100110011", // 500-999 ms
    "00110011001100110011001100110011001100110011001100", // 1000-1499 ms
    "11001100110011001100110011001100110011001100110011", // 1500-1999 ms
};

uint16_t frameTick = 0;
uint32_t nextTickMs = 0;
bool heartbeatLedState = false;

// Functional description: Validate all four 50-character segments containing only '0' and '1'.
// Input parameter: pattern -> array of 4 segment strings, each expected to be 50 digits.
// Output parameter: Returns true when each segment has exact length 50 and contains only binary digits.
bool isValidPattern(const char *const pattern[PATTERN_SEGMENTS]) {
  for (uint8_t i = 0; i < PATTERN_SEGMENTS; ++i) {
    for (uint8_t j = 0; j < SEGMENT_LENGTH; ++j) {
      const char c = pattern[i][j];
      if ((c != '0') && (c != '1')) {
        return false;
      }
    }
    if (pattern[i][SEGMENT_LENGTH] != '\0') {
      return false;
    }
  }
  return true;
}

// Functional description: Evaluate whether a channel is active at a given frame time from string pattern slots.
// Input parameter: pattern -> array of 4x50 binary digits, frameTimeMs -> current frame position in ms.
// Output parameter: Returns true when the corresponding slot digit is '1'.
bool isChannelActive(const char *const pattern[PATTERN_SEGMENTS], uint16_t frameTimeMs) {
  const uint16_t slot = static_cast<uint16_t>(frameTimeMs / TICK_MS);
  const uint8_t segmentIndex = static_cast<uint8_t>(slot / SEGMENT_LENGTH);
  const uint8_t digitIndex = static_cast<uint8_t>(slot % SEGMENT_LENGTH);
  return pattern[segmentIndex][digitIndex] == '1';
}

// Functional description: Generate deterministic mock ADC values for test and desktop plotting.
// Input parameter: channel -> ADC channel index (0 for A0 waveform, 1 for A1 waveform).
// Output parameter: Returns a simulated 10-bit ADC value in range 0..1023.
int16_t mockAnalogRead(uint8_t channel) {
  const uint16_t slot = frameTick;
  if (channel == 0) {
    const uint16_t triangle = (slot < (FRAME_TICKS / 2)) ? (slot * 2) : ((FRAME_TICKS - 1 - slot) * 2);
    return static_cast<int16_t>((triangle * 1023U) / (FRAME_TICKS - 1));
  }

  const uint16_t pulse = ((slot / 20U) % 2U) ? 820U : 180U;
  return static_cast<int16_t>(pulse);
}

// Functional description: Acquire ADC values and package them with current actuator states.
// Input parameter: act0 -> current state for actuator 0, act1 -> current state for actuator 1.
// Output parameter: Returns one Sample struct with int16 ADC values and boolean states.
Sample acquireSample(bool act0, bool act1) {
  Sample sample = {};
  // sample.adc0 = static_cast<int16_t>(analogRead(ADC0_PIN));
  // sample.adc1 = static_cast<int16_t>(analogRead(ADC1_PIN));
  sample.adc0 = mockAnalogRead(0);
  sample.adc1 = mockAnalogRead(1);
  sample.act0 = act0;
  sample.act1 = act1;
  return sample;
}

// Functional description: Drive actuator output pins according to computed states.
// Input parameter: act0 -> desired state for actuator 0, act1 -> desired state for actuator 1.
// Output parameter: Updates digital outputs ACT0_PIN and ACT1_PIN.
void applyOutputs(bool act0, bool act1) {
  digitalWrite(ACT0_PIN, act0 ? HIGH : LOW);
  digitalWrite(ACT1_PIN, act1 ? HIGH : LOW);
}

// Functional description: Transmit one measurement/state sample to serial as CSV for plotting.
// Input parameter: sample -> ADC readings and actuator states to transmit.
// Output parameter: Writes frame start word 0x55AA followed by 4 little-endian uint16_t payload values.
void emitSample(const Sample &sample) {
  const uint16_t startWord = 0x55AA;
  const uint16_t outAdc0 = static_cast<uint16_t>(sample.adc0);
  const uint16_t outAdc1 = static_cast<uint16_t>(sample.adc1);
  const uint16_t outAct0 = sample.act0 ? static_cast<uint16_t>(1) : static_cast<uint16_t>(0);
  const uint16_t outAct1 = sample.act1 ? static_cast<uint16_t>(1) : static_cast<uint16_t>(0);

  Serial.write(reinterpret_cast<const uint8_t *>(&startWord), sizeof(startWord));
  Serial.write(reinterpret_cast<const uint8_t *>(&outAdc0), sizeof(outAdc0));
  Serial.write(reinterpret_cast<const uint8_t *>(&outAdc1), sizeof(outAdc1));
  Serial.write(reinterpret_cast<const uint8_t *>(&outAct0), sizeof(outAct0));
  Serial.write(reinterpret_cast<const uint8_t *>(&outAct1), sizeof(outAct1));

}

// Functional description: Initialize hardware, serial interface, timing state, and pulse patterns.
// Input parameter: none.
// Output parameter: Configures pin modes and prepares runtime state before loop execution.
void setup() {
  pinMode(ACT0_PIN, OUTPUT);
  pinMode(ACT1_PIN, OUTPUT);
  pinMode(HEARTBEAT_LED_PIN, OUTPUT);
  digitalWrite(ACT0_PIN, LOW);
  digitalWrite(ACT1_PIN, LOW);
  digitalWrite(HEARTBEAT_LED_PIN, LOW);

  Serial.begin(115200);
  if (!isValidPattern(pattern0) || !isValidPattern(pattern1)) {
    Serial.println("Pattern validation failed");
  }

  frameTick = 0;
  nextTickMs = millis();
}

// Functional description: Execute fixed-rate 10 ms scheduler ticks with catch-up behavior.
// Input parameter: none.
// Output parameter: Updates outputs and emits one serial sample per processed tick.
void loop() {
  const uint32_t now = millis();
  while (static_cast<int32_t>(now - nextTickMs) >= 0) {
    const uint16_t frameTimeMs = static_cast<uint16_t>(frameTick * TICK_MS);
    const bool act0 = isChannelActive(pattern0, frameTimeMs);
    const bool act1 = isChannelActive(pattern1, frameTimeMs);

    applyOutputs(act0, act1);
    const Sample sample = acquireSample(act0, act1);
    emitSample(sample);
    frameTick = static_cast<uint16_t>((frameTick + 1) % FRAME_TICKS);
    if (frameTick == 0) 
      { heartbeatLedState = !heartbeatLedState;
      digitalWrite(HEARTBEAT_LED_PIN, heartbeatLedState ? HIGH : LOW);
      // Serial.write("Tick\n");
      }
    nextTickMs += TICK_MS;
  }
}
