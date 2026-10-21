/*
  RX candidato - ESP32 Dev Module
  Ligacao: GPIO 16 do RX <- GPIO 17 do TX; GND comum.
  Quadro: [7E][SEQ][LEN=2][P0][P1][DETECCAO]
  Verdade posterior: [D5][P0 verdadeiro][P1 verdadeiro].
  A decisao ACEITO/DESCARTADO usa apenas o quadro principal.
*/
#include <Arduino.h>

// BLOCO 1 - UART, marcadores e tempos. Valores iguais aos do TX aprovado.
HardwareSerial telemetryUart(2);
constexpr int RX_PIN = 16, TX_PIN = 17;
constexpr uint8_t START = 0x7E, DEBUG = 0xD5, LEN = 2;
constexpr uint16_t TOTAL = 1000;
constexpr uint32_t PERIOD = 20000, MAIN_TIMEOUT = 5000;
constexpr uint32_t DEBUG_IDLE = 3000, START_TOLERANCE = 10000;

// BLOCO 2 - Mude SOMENTE esta ultima linha e use a mesma opcao no TX.
#define DETECTION_CHECKSUM 0
#define DETECTION_CRC8 1
#define DETECTION_MODE DETECTION_CHECKSUM

// BLOCO 3 - Uma linha do CSV. O historico fica na RAM ate reiniciar o ESP32.
struct Row {
  uint8_t expected, seq, len, p0, p1, det, residue, real0, real1;
  uint8_t decision, match, status, seqState, gap;
  float verifyUs;
  uint32_t recoveryUs, timestampUs;
};
Row history[TOTAL];
enum State { WAIT_START, READ_MAIN, WAIT_DEBUG, READ_DEBUG };
State state = WAIT_START;
uint8_t mainBytes[5], truth[2], mainIndex = 0, truthIndex = 0;
bool running=false, complete=false, valid=false, scheduled=false, seqSynced=false, waitingRecovery=false;
uint8_t mode=0, expectedSeq=0;
uint16_t slots=0, ok=0, corrupted=0, lost=0, undetected=0, verified=0;
uint32_t frameStart=0, lastByte=0, nextExpected=0, lastLossUs=0;
float latestUs=0, minUs=1e9, sumUs=0, maxUs=0;

// BLOCO 4 - Algoritmos de deteccao. O RX recalcula usando os bytes recebidos.
uint8_t sumResidue(const uint8_t *d, uint8_t n) {
  uint16_t s=0; while(n--){ s+=*d++; s=(s&255)+(s>>8); } return (s&255)+(s>>8);
}
uint8_t crc8(const uint8_t *d, uint8_t n) {
  uint8_t c=0; while(n--){ c^=*d++; for(uint8_t b=0;b<8;b++) c=(c&0x80)?(c<<1)^0x07:c<<1; } return c;
}
uint8_t residue() {
#if DETECTION_MODE == DETECTION_CRC8
  return crc8(mainBytes,5);
#else
  return sumResidue(mainBytes,5);
#endif
}
bool residueValid(uint8_t r) {
#if DETECTION_MODE == DETECTION_CRC8
  return r==0;
#else
  return r==0xFF;
#endif
}
const char *technique() {
#if DETECTION_MODE == DETECTION_CRC8
  return "CRC8_POLY_07";
#else
  return "CHECKSUM_1_COMPLEMENTO";
#endif
}

// BLOCO 5 - Nomes usados no CSV para que a planilha fique legivel.
const char *decisionName(uint8_t v){ return v==0?"ACEITO":v==1?"DESCARTADO":"NAO_CONCLUIDA"; }
const char *matchName(uint8_t v){ return v==1?"SIM":v==0?"NAO":"NA"; }
const char *statusName(uint8_t v){ return v==0?"OK":v==1?"CORROMPIDO":v==2?"PERDIDO":"NAO_DETECTADO"; }
const char *seqName(uint8_t v){ return v==0?"NA":v==1?"PRIMEIRO":v==2?"EM_ORDEM":v==3?"LACUNA":"REPETIDO_OU_ANTIGO"; }
void header(){ Serial.println("tecnica,modo,slot,seq_esperada,seq_rx,len,p0_rx,p1_rx,det_rx,residuo,decisao,p0_real,p1_real,payload_confere,status,estado_seq,lacuna_seq,tempo_verificacao_us,tempo_recuperacao_us,timestamp_us"); }
void printRow(uint16_t i){
  Row &r=history[i];
  Serial.printf("%s,%u,%u,%02X,%02X,%02X,%02X,%02X,%02X,%02X,%s,%02X,%02X,%s,%s,%s,%u,%.3f,%lu,%lu\n",
    technique(),mode,i+1,r.expected,r.seq,r.len,r.p0,r.p1,r.det,r.residue,
    decisionName(r.decision),r.real0,r.real1,matchName(r.match),statusName(r.status),
    seqName(r.seqState),r.gap,r.verifyUs,(unsigned long)r.recoveryUs,(unsigned long)r.timestampUs);
}

// BLOCO 6 - Sequencia: modulo 256; 255->0 e normal. Lacunas indicam saltos.
void classifySequence(Row &r){
  if(!seqSynced){ r.expected=r.seq; r.seqState=1; seqSynced=true; expectedSeq=r.seq+1; return; }
  r.expected=expectedSeq; uint8_t delta=(uint8_t)(r.seq-expectedSeq);
  if(delta==0){ r.seqState=2; expectedSeq=r.seq+1; }
  else if(delta<128){ r.seqState=3; r.gap=delta; expectedSeq=r.seq+1; }
  else r.seqState=4;
}
void clearFrame(){ memset(mainBytes,0,sizeof(mainBytes)); memset(truth,0,sizeof(truth)); mainIndex=truthIndex=0; complete=valid=false; latestUs=0; }
void record(bool hasDebug,uint32_t now){
  if(slots>=TOTAL) return; Row &r=history[slots]; memset(&r,0,sizeof(r));
  r.seq=mainBytes[0]; r.len=mainBytes[1]; r.p0=mainBytes[2]; r.p1=mainBytes[3]; r.det=mainBytes[4];
  r.real0=truth[0]; r.real1=truth[1]; r.residue=complete?residue():0;
  r.decision=complete?(valid?0:1):2;
  r.match=complete&&hasDebug?(mainBytes[2]==truth[0]&&mainBytes[3]==truth[1]):2;
  r.status=!complete||!hasDebug?2:!valid?1:r.match?0:3;
  r.verifyUs=complete?latestUs:0; r.timestampUs=now; r.seqState=0;
  if(r.status!=2 && valid) classifySequence(r); else r.expected=seqSynced?expectedSeq:0;
  if(r.status==2){ waitingRecovery=true; lastLossUs=now; }
  else if(waitingRecovery){ r.recoveryUs=now-lastLossUs; waitingRecovery=false; }
  if(r.status==0)ok++; else if(r.status==1)corrupted++; else if(r.status==2)lost++; else undetected++;
  printRow(slots++);
}

// BLOCO 7 - Recepcao do quadro e medida do tempo de verificacao.
void beginMain(uint32_t now){ clearFrame(); frameStart=now; state=READ_MAIN; if(!scheduled){scheduled=true; nextExpected=now+PERIOD;} }
void verifyMain(){
  uint32_t t0=micros(); uint8_t r=residue(); valid=mainBytes[1]==LEN&&residueValid(r);
  latestUs=(float)(micros()-t0); minUs=min(minUs,latestUs); maxUs=max(maxUs,latestUs); sumUs+=latestUs; verified++;
  complete=true; state=WAIT_DEBUG;
}
void advanceExpected(uint32_t marker){ if(scheduled) nextExpected=marker+PERIOD; }
void finishRun(){
  running=false; state=WAIT_START; float rate=corrupted+undetected?100.0f*undetected/(corrupted+undetected):0;
  Serial.printf("\nRESUMO DA RODADA\nTecnica: %s | Modo: %u | OK: %u | CORROMPIDO: %u | PERDIDO: %u | NAO_DETECTADO: %u | Total: %u\n",technique(),mode,ok,corrupted,lost,undetected,slots);
  Serial.printf("Taxa de nao deteccao: %.4f %%\nTempo de verificacao: minimo=%.3f us, medio=%.3f us, maximo=%.3f us\n",rate,verified?minUs:0,verified?sumUs/verified:0,verified?maxUs:0);
  Serial.println("Digite h para reimprimir o historico CSV.");
}

// BLOCO 8 - Maquina de estados. A copia D5 so e lida DEPOIS da decisao.
void receiveByte(uint8_t b,uint32_t now){
  uint32_t idle=now-lastByte; lastByte=now;
  if(state==WAIT_START){ if(b==START){advanceExpected(now);beginMain(now);} else if(b==DEBUG&&idle>=DEBUG_IDLE){clearFrame();state=READ_DEBUG;} }
  else if(state==READ_MAIN){ mainBytes[mainIndex++]=b; if(mainIndex==2&&mainBytes[1]!=LEN)state=WAIT_DEBUG; else if(mainIndex==5)verifyMain(); }
  else if(state==WAIT_DEBUG){
    if(b==DEBUG&&idle>=DEBUG_IDLE){truthIndex=0;state=READ_DEBUG;}
    else if(b==START&&idle>=DEBUG_IDLE){record(false,now);if(slots>=TOTAL)finishRun();else{advanceExpected(now);beginMain(now);}}
  } else { truth[truthIndex++]=b; if(truthIndex==2){record(true,now);if(slots>=TOTAL)finishRun();else state=WAIT_START;} }
}
void checkTimeouts(uint32_t now){
  if(!running)return;
  if(state==READ_MAIN&&now-frameStart>MAIN_TIMEOUT){complete=false;state=WAIT_DEBUG;}
  if(scheduled&&state==WAIT_START&&(int32_t)(now-(nextExpected+START_TOLERANCE))>=0){
    clearFrame();record(false,now);nextExpected+=PERIOD;if(slots>=TOTAL)finishRun();
  }
}

// BLOCO 9 - Comandos USB: 0..3 escolhe modo; r inicia; h mostra historico.
void commands(){
  while(Serial.available()){
    char c=Serial.read();
    if(c>='0'&&c<='3'&&!running){mode=c-'0';Serial.printf("Modo registrado: %u\n",mode);}
    else if((c=='r'||c=='R')&&!running){
      slots=ok=corrupted=lost=undetected=verified=0;sumUs=maxUs=0;minUs=1e9;scheduled=seqSynced=waitingRecovery=false;
      clearFrame();state=WAIT_START;lastByte=micros();while(telemetryUart.available())telemetryUart.read();running=true;
      Serial.printf("CSV ao vivo - tecnica=%s, modo=%u\n",technique(),mode);header();
    } else if((c=='h'||c=='H')&&!running){Serial.println("HISTORICO CSV");header();for(uint16_t i=0;i<slots;i++)printRow(i);}
  }
}
void setup(){Serial.begin(115200);telemetryUart.begin(115200,SERIAL_8N1,RX_PIN,TX_PIN);Serial.printf("RX candidato pronto. Tecnica: %s\nEnvie 0..3, depois r; ao final envie h.\n",technique());}
void loop(){commands();while(running&&telemetryUart.available())receiveByte((uint8_t)telemetryUart.read(),micros());checkTimeouts(micros());}
