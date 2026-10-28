/*
  noise.ino - injetor fisico de ruido para o protocolo de telemetria

  Placa na Arduino IDE: ESP32 Dev Module

  Este ESP32 observa a mesma linha que liga o TX ao RX. Depois de detectar
  o marcador 0x7E, ele usa esp_timer para acionar um transistor NPN e puxar
  bits selecionados para nivel baixo.

  Ligacoes logicas:
    Linha TX GPIO 17 -> GPIO 16 deste ESP32 (monitoracao)
    GPIO 25 deste ESP32 -> resistor de 1 kohm -> base do transistor
    Emissor do transistor -> GND comum
    Coletor do transistor -> linha TX GPIO 17 / RX GPIO 16

  ATENCAO: confira a ordem dos terminais do seu 2N2222 no datasheet.
*/

#include <Arduino.h>
#include <esp_timer.h>
#include <esp_system.h>

// -------------------------- UART --------------------------
constexpr uint32_t USB_BAUD = 115200;
constexpr uint32_t UART_BAUD = 115200;
constexpr int UART_MONITOR_PIN = 16;
constexpr int UART_UNUSED_TX_PIN = 17;

HardwareSerial monitoredUart(2);

// ---------------------- Circuito de ruido -----------------
constexpr int NOISE_CONTROL_PIN = 18;
constexpr uint8_t NOISE_OFF_LEVEL = LOW;
constexpr uint8_t NOISE_ON_LEVEL = HIGH;

// ------------------------ Protocolo -----------------------
constexpr uint8_t FRAME_START = 0x7E;
constexpr uint32_t MARKER_GUARD_US = 2000;
constexpr uint32_t FRAME_IGNORE_WINDOW_US = 10000;
constexpr uint16_t FRAMES_PER_RUN = 1000;

// Depois do marcador existem cinco bytes que podem receber ruido:
// 0=SEQ, 1=LEN, 2=PAYLOAD0, 3=PAYLOAD1, 4=DETECCAO.
constexpr uint8_t TARGET_BYTES = 5;
constexpr uint8_t MAX_EVENTS_PER_FRAME = 8;

constexpr double BIT_TIME_US = 1000000.0 / 115200.0;
constexpr double BYTE_TIME_US = 10.0 * BIT_TIME_US;
constexpr uint32_t PULSE_WIDTH_US = 5;

struct PulseEvent {
  uint8_t byteIndex;
  uint8_t bitIndex;
  volatile uint8_t levelBefore;
  volatile bool changed;
  volatile bool started;
  volatile bool ended;
  volatile uint32_t actualStartUs;
  esp_timer_handle_t startTimer;
  esp_timer_handle_t endTimer;
};

PulseEvent events[MAX_EVENTS_PER_FRAME] = {};
portMUX_TYPE pulseMux = portMUX_INITIALIZER_UNLOCKED;

volatile uint8_t activePulls = 0;
uint8_t plannedEvents = 0;
uint8_t selectedMode = 0;
uint16_t framesDetected = 0;
uint32_t ignoreMarkersUntilUs = 0;
uint32_t currentMarkerTimestampUs = 0;
bool runActive = false;
bool frameLogPending = false;

void ARDUINO_ISR_ATTR startPulseCallback(void *argument) {
  PulseEvent *event = static_cast<PulseEvent *>(argument);

  // A leitura acontece imediatamente antes de ligar o transistor.
  // HIGH significa que esta tentativa realmente pode mudar 1 -> 0.
  event->levelBefore = static_cast<uint8_t>(digitalRead(UART_MONITOR_PIN));
  event->changed = event->levelBefore == HIGH;
  event->actualStartUs = micros();
  event->started = true;

  portENTER_CRITICAL(&pulseMux);
  ++activePulls;
  digitalWrite(NOISE_CONTROL_PIN, NOISE_ON_LEVEL);
  portEXIT_CRITICAL(&pulseMux);
}

void ARDUINO_ISR_ATTR endPulseCallback(void *argument) {
  PulseEvent *event = static_cast<PulseEvent *>(argument);

  portENTER_CRITICAL(&pulseMux);
  if (activePulls > 0) {
    --activePulls;
  }
  if (activePulls == 0) {
    digitalWrite(NOISE_CONTROL_PIN, NOISE_OFF_LEVEL);
  }
  portEXIT_CRITICAL(&pulseMux);

  event->ended = true;
}

bool createTimers() {
  for (uint8_t i = 0; i < MAX_EVENTS_PER_FRAME; ++i) {
    esp_timer_create_args_t startArgs = {};
    startArgs.callback = &startPulseCallback;
    startArgs.arg = &events[i];
    startArgs.dispatch_method = ESP_TIMER_TASK;
    startArgs.name = "noise_on";

    esp_timer_create_args_t endArgs = {};
    endArgs.callback = &endPulseCallback;
    endArgs.arg = &events[i];
    endArgs.dispatch_method = ESP_TIMER_TASK;
    endArgs.name = "noise_off";

    if (esp_timer_create(&startArgs, &events[i].startTimer) != ESP_OK) {
      return false;
    }
    if (esp_timer_create(&endArgs, &events[i].endTimer) != ESP_OK) {
      return false;
    }
  }

  return true;
}

void clearEvent(uint8_t index) {
  events[index].byteIndex = 0;
  events[index].bitIndex = 0;
  events[index].levelBefore = LOW;
  events[index].changed = false;
  events[index].started = false;
  events[index].ended = false;
  events[index].actualStartUs = 0;
}

void chooseTargetPositions() {
  plannedEvents = 0;

  if (selectedMode == 0) {
    return;
  }

  if (selectedMode == 1) {
    const uint8_t position = static_cast<uint8_t>(random(TARGET_BYTES * 8));
    events[0].byteIndex = position / 8;
    events[0].bitIndex = position % 8;
    plannedEvents = 1;
    return;
  }

  if (selectedMode == 2) {
    const uint8_t firstPosition = static_cast<uint8_t>(random(TARGET_BYTES * 8));
    uint8_t secondPosition;

    do {
      secondPosition = static_cast<uint8_t>(random(TARGET_BYTES * 8));
    } while (secondPosition == firstPosition);

    events[0].byteIndex = firstPosition / 8;
    events[0].bitIndex = firstPosition % 8;
    events[1].byteIndex = secondPosition / 8;
    events[1].bitIndex = secondPosition % 8;
    plannedEvents = 2;
    return;
  }

  // Modo 3: de 3 a 8 posicoes consecutivas dentro do mesmo byte.
  const uint8_t burstLength = static_cast<uint8_t>(random(3, 9));
  const uint8_t byteIndex = static_cast<uint8_t>(random(TARGET_BYTES));
  const uint8_t firstBit = static_cast<uint8_t>(random(0, 9 - burstLength));

  plannedEvents = burstLength;
  for (uint8_t i = 0; i < burstLength; ++i) {
    events[i].byteIndex = byteIndex;
    events[i].bitIndex = firstBit + i;
  }
}

uint64_t pulseStartDelayUs(uint8_t byteIndex, uint8_t bitIndex) {
  // Cada byte UART: start bit, oito bits de dados (LSB primeiro) e stop bit.
  // O pulso de 5 us fica centralizado no bit de dados escolhido.
  const double centerUs =
    static_cast<double>(MARKER_GUARD_US) +
    static_cast<double>(byteIndex) * BYTE_TIME_US +
    (1.0 + static_cast<double>(bitIndex) + 0.5) * BIT_TIME_US;

  return static_cast<uint64_t>(centerUs - (PULSE_WIDTH_US / 2.0));
}

void scheduleNoiseForCurrentFrame() {
  for (uint8_t i = 0; i < MAX_EVENTS_PER_FRAME; ++i) {
    clearEvent(i);
  }

  chooseTargetPositions();

  for (uint8_t i = 0; i < plannedEvents; ++i) {
    const uint64_t startDelayUs = pulseStartDelayUs(
      events[i].byteIndex,
      events[i].bitIndex
    );
    const uint64_t endDelayUs = startDelayUs + PULSE_WIDTH_US;

    esp_timer_start_once(events[i].startTimer, startDelayUs);
    esp_timer_start_once(events[i].endTimer, endDelayUs);
  }

  frameLogPending = true;
}

bool allEventsFinished() {
  for (uint8_t i = 0; i < plannedEvents; ++i) {
    if (!events[i].ended) {
      return false;
    }
  }
  return true;
}

void printFrameLog() {
  uint8_t changedMask = 0;

  for (uint8_t i = 0; i < plannedEvents; ++i) {
    if (events[i].changed) {
      changedMask |= static_cast<uint8_t>(1u << i);
    }
  }

  Serial.printf(
    "%u,%u,%u,\"",
    framesDetected,
    selectedMode,
    plannedEvents
  );

  for (uint8_t i = 0; i < plannedEvents; ++i) {
    if (i > 0) {
      Serial.print('|');
    }
    Serial.printf("B%u.b%u", events[i].byteIndex, events[i].bitIndex);
  }

  Serial.printf(
    "\",0x%02X,%lu\n",
    changedMask,
    static_cast<unsigned long>(currentMarkerTimestampUs)
  );

  frameLogPending = false;

  if (framesDetected >= FRAMES_PER_RUN) {
    runActive = false;
    digitalWrite(NOISE_CONTROL_PIN, NOISE_OFF_LEVEL);
    Serial.println("Rodada de ruido encerrada: 1000 quadros observados.");
    Serial.println("Envie um modo de 0 a 3 e depois r para outra rodada.");
  }
}

void handleFrameMarker(uint32_t timestampUs) {
  if (!runActive || framesDetected >= FRAMES_PER_RUN) {
    return;
  }

  ++framesDetected;
  currentMarkerTimestampUs = timestampUs;
  ignoreMarkersUntilUs = timestampUs + FRAME_IGNORE_WINDOW_US;
  scheduleNoiseForCurrentFrame();
}

void startRun() {
  framesDetected = 0;
  frameLogPending = false;
  ignoreMarkersUntilUs = 0;
  activePulls = 0;
  digitalWrite(NOISE_CONTROL_PIN, NOISE_OFF_LEVEL);

  while (monitoredUart.available() > 0) {
    monitoredUart.read();
  }

  runActive = true;
  Serial.println();
  Serial.printf("Injetor iniciado no modo %u. Aguardando 0x7E.\n", selectedMode);
  Serial.println("quadro,modo,qtd_bits,posicoes,mudou_mask,timestamp_marcador_us");
}

void stopRun() {
  runActive = false;
  digitalWrite(NOISE_CONTROL_PIN, NOISE_OFF_LEVEL);
  Serial.println("Injetor interrompido.");
}

void readUsbCommands() {
  while (Serial.available() > 0) {
    const char command = static_cast<char>(Serial.read());

    if (command >= '0' && command <= '3' && !runActive) {
      selectedMode = static_cast<uint8_t>(command - '0');
      Serial.printf("Modo selecionado: %u\n", selectedMode);
    } else if ((command == 'r' || command == 'R') && !runActive) {
      startRun();
    } else if ((command == 's' || command == 'S') && runActive) {
      stopRun();
    }
  }
}

void setup() {
  Serial.begin(USB_BAUD);
  pinMode(NOISE_CONTROL_PIN, OUTPUT);
  digitalWrite(NOISE_CONTROL_PIN, NOISE_OFF_LEVEL);
  pinMode(UART_MONITOR_PIN, INPUT);
  monitoredUart.begin(UART_BAUD, SERIAL_8N1, UART_MONITOR_PIN, UART_UNUSED_TX_PIN);

  randomSeed(esp_random());

  Serial.println();
  Serial.println("ESP32 injetor de ruido pronto.");
  Serial.println("Selecione um modo de 0 a 3 e depois envie r.");

  if (!createTimers()) {
    Serial.println("ERRO: nao foi possivel criar os timers de ruido.");
    while (true) {
      delay(1000);
    }
  }
}

void loop() {
  readUsbCommands();

  while (runActive && monitoredUart.available() > 0) {
    const uint8_t value = static_cast<uint8_t>(monitoredUart.read());
    const uint32_t timestampUs = micros();

    if (value == FRAME_START &&
        static_cast<int32_t>(timestampUs - ignoreMarkersUntilUs) >= 0) {
      handleFrameMarker(timestampUs);
    }
  }

  if (frameLogPending && allEventsFinished()) {
    printFrameLog();
  }

  yield();
}
