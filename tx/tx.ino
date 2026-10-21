/* TX enxuto - ESP32 Dev Module
   Ligacao: GPIO 17 do TX -> GPIO 16 do RX, com GND comum.
   Quadro: [7E][SEQ][LEN=2][P0][P1][DETECCAO]
   Depois dele: [D5][P0 verdadeiro][P1 verdadeiro] (copia de depuracao). */
#include <Arduino.h>

// BLOCO 1 - Porta serial usada para levar o quadro ao outro ESP32.
// O numero 2 significa UART2; RX_PIN existe porque a funcao begin pede os dois pinos.
HardwareSerial telemetryUart(2);
constexpr int RX_PIN = 16, TX_PIN = 17;

// BLOCO 2 - Regras do quadro principal e da copia de depuracao posterior.
constexpr uint8_t START = 0x7E, DEBUG = 0xD5, LEN = 2;
constexpr uint16_t TOTAL = 1000;
constexpr uint32_t PERIOD = 20000, GUARD = 2000, DEBUG_GAP = 4000;

// BLOCO 3 - Escolha da tecnica. Mude somente esta linha no TX E no RX.
#define DETECTION_CHECKSUM 0
#define DETECTION_CRC8 1
#define DETECTION_MODE DETECTION_CHECKSUM  // Troque nos dois: TX e RX.

// BLOCO 4 - Memoria da rodada atual: sequencia, quantidade e tempos medidos.
bool running = false;
uint8_t seq = 0;
uint16_t sent = 0;
uint32_t nextFrame;
double sumUs, minUs, maxUs;

// BLOCO 5 - Checksum: soma os bytes e devolve o complemento de 1 da soma.
uint8_t checksum(const uint8_t *v, uint8_t n) {
  uint16_t s = 0;
  while (n--) { s += *v++; s = (s & 255) + (s >> 8); }
  s = (s & 255) + (s >> 8);
  return ~s;
}

// BLOCO 6 - CRC-8: outra forma de criar o byte de deteccao (polinomio 0x07).
uint8_t crc8(const uint8_t *v, uint8_t n) {
  uint8_t c = 0;
  while (n--) {
    c ^= *v++;
    for (uint8_t b = 0; b < 8; b++) c = c & 0x80 ? (c << 1) ^ 0x07 : c << 1;
  }
  return c;
}

// BLOCO 7 - Escolhe checksum ou CRC conforme a chave DETECTION_MODE.
uint8_t detection(const uint8_t *v, uint8_t n) {
#if DETECTION_MODE == DETECTION_CHECKSUM
  return checksum(v, n);
#else
  return crc8(v, n);
#endif
}

const char *technique() {
#if DETECTION_MODE == DETECTION_CHECKSUM
  return "CHECKSUM_1_COMPLEMENTO";
#else
  return "CRC8_POLY_07";
#endif
}

// BLOCO 8 - Espera o inicio do proximo slot de 20 ms sem perder comandos USB.
void waitFor(uint32_t target) {
  while ((int32_t)(target - micros()) > 0) yield();
}

// BLOCO 9 - Monta e envia UM quadro completo.
void sendFrame() {
  // O payload de teste e sempre [FF][SEQ]. Isso permite ao RX saber o esperado.
  uint8_t data[4] = {seq, LEN, 0xFF, seq}; // Os quatro bytes protegidos.
  uint32_t begin = ESP.getCycleCount();
  uint8_t check = detection(data, sizeof(data));
  double us = (ESP.getCycleCount() - begin) / (double)ESP.getCpuFreqMHz();
  sumUs += us;
  if (!sent || us < minUs) minUs = us;
  if (!sent || us > maxUs) maxUs = us;

  // Primeiro vai apenas o marcador. Depois da guarda, seguem dados e deteccao.
  telemetryUart.write(START); telemetryUart.flush();
  delayMicroseconds(GUARD);       // Tempo para o ESP de ruido ver o marcador.
  telemetryUart.write(data, sizeof(data)); telemetryUart.write(check); telemetryUart.flush();

  // Esta copia e o "gabarito" do experimento. Nao participa do checksum/CRC.
  // O RX so pode le-la DEPOIS de ja ter aceitado ou descartado o quadro principal.
  delayMicroseconds(DEBUG_GAP);
  telemetryUart.write(DEBUG); telemetryUart.write(data + 2, LEN); telemetryUart.flush();

  if (!sent || (sent + 1) % 50 == 0)
    Serial.printf("TX: quadro=%u seq=%u payload=FF %02X det=%02X calc=%.3f us\n",
                  sent + 1, seq, seq, check, us);
  seq++; sent++;
}

// BLOCO 10 - Inicio/fim e comandos recebidos no Monitor Serial.
void startRun() {
  seq = sent = 0; sumUs = minUs = maxUs = 0; nextFrame = micros(); running = true;
  Serial.printf("\nTX iniciou: %s. Enviando 1000 quadros. 's' para parar.\n", technique());
}

void finishRun() {
  running = false;
  Serial.printf("\nFim. Enviados: %u\nCalculo: min=%.3f us, medio=%.3f us, max=%.3f us\n",
                sent, minUs, sent ? sumUs / sent : 0, maxUs);
  Serial.println("Envie r para outra rodada.");
}

void commands() {
  while (Serial.available()) {
    char c = Serial.read();
    if ((c == 'r' || c == 'R') && !running) startRun();
    if ((c == 's' || c == 'S') && running) finishRun();
  }
}

// BLOCO 11 - setup roda uma vez; loop roda para sempre.
void setup() {
  Serial.begin(115200);
  telemetryUart.begin(115200, SERIAL_8N1, RX_PIN, TX_PIN);
  Serial.printf("TX pronto. Tecnica: %s\nGPIO 17 -> GPIO 16 do RX; GND comum.\n", technique());
}

void loop() {
  commands();
  if (!running) { delay(1); return; }
  waitFor(nextFrame); sendFrame(); nextFrame += PERIOD;
  if (sent == TOTAL) finishRun();
}
