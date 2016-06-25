//  Based on https://github.com/matthijskooijman/arduino-lmic
/*******************************************************************************
 * Copyright (c) 2015 Matthijs Kooijman
 *
 * Permission is hereby granted, free of charge, to anyone
 * obtaining a copy of this document and accompanying files,
 * to do whatever they want with them without any restriction,
 * including, but not limited to, copying, modification and redistribution.
 * NO WARRANTY OF ANY KIND IS PROVIDED.
 *
 * This example transmits data on hardcoded channel and receives data
 * when not transmitting. Running this sketch on two nodes should allow
 * them to communicate.
 *******************************************************************************/

extern int transmission_mode;
////  TP-IoT: Mode 1 is max range but does NOT work with Dragino shield and Hope RF96 chip.
////  TP-IoT Gateway runs on:
////    case 1:     setCR(CR_5);        // CR = 4/5
////                setSF(SF_12);       // SF = 12
////                setBW(BW_125);      // BW = 125 KHz
//setModemConfig(Bw125Cr45Sf4096);  ////  TP-IoT Mode 1
int transmission_mode = 1;

////  Testing TP-IoT Gateway on mode 5 (better reach, medium time on air)
////  Works with Dragino shield and Hope RF96 chip.
////    case 5:     setCR(CR_5);        // CR = 4/5
////                setSF(SF_10);       // SF = 10
////                setBW(BW_250);      // BW = 250 KHz -> 0x80
//setModemConfig(Bw250Cr45Sf1024);  ////  TP-IoT Mode 5
//int transmission_mode = 5;

#include <lmic.h>
#include <hal/hal.h>
#include <SPI.h>

#if !defined(DISABLE_INVERT_IQ_ON_RX)
#error This example requires DISABLE_INVERT_IQ_ON_RX to be set. Update \
       config.h in the lmic library to set it.
#endif

// How often to send a packet. Note that this sketch bypasses the normal
// LMIC duty cycle limiting, so when you change anything in this sketch
// (payload length, frequency, spreading factor), be sure to check if
// this interval should not also be increased.
// See this spreadsheet for an easy airtime and duty cycle calculator:
// https://docs.google.com/spreadsheets/d/1voGAtQAjC1qBmaVuP1ApNKs1ekgUjavHuVQIXyYSvNc 
#define TX_INTERVAL 2000

/*  Original settings:
// Pin mapping
const lmic_pinmap lmic_pins = {
    .nss = 6,
    .rxtx = LMIC_UNUSED_PIN,
    .rst = 5,
    .dio = {2, 3, 4},
};
*/
//  TP-IoT Pin mapping
const lmic_pinmap lmic_pins = {
    .nss = 10,// Connected to pin D10
    .rxtx = LMIC_UNUSED_PIN,// For placeholder only, Do not connected on RFM92/RFM95
    .rst = 9,// Needed on RFM92/RFM95? (probably not)
    .dio = {2, 6, 7},// Specify pin numbers for DIO0, 1, 2 connected to D2, D6, D7 
};

// These callbacks are only used in over-the-air activation, so they are
// left empty here (we cannot leave them out completely unless
// DISABLE_JOIN is set in config.h, otherwise the linker will complain).
void os_getArtEui (u1_t* buf) { }
void os_getDevEui (u1_t* buf) { }
void os_getDevKey (u1_t* buf) { }

void onEvent (ev_t ev) {
}

osjob_t txjob;
osjob_t timeoutjob;
static void tx_func (osjob_t* job);

// Transmit the given string and call the given function afterwards
void tx(const char *str, osjobcb_t func) {
  os_radio(RADIO_RST); // Stop RX first
  delay(1); // Wait a bit, without this os_radio below asserts, apparently because the state hasn't changed yet
  LMIC.dataLen = 0;
  while (*str)
    LMIC.frame[LMIC.dataLen++] = *str++;
  LMIC.osjob.func = func;
  os_radio(RADIO_TX);
  Serial.println("TX");
}

// Enable rx mode and call func when a packet is received
void rx(osjobcb_t func) {
  LMIC.osjob.func = func;
  LMIC.rxtime = os_getTime(); // RX _now_
  // Enable "continuous" RX (e.g. without a timeout, still stops after
  // receiving a packet)
  os_radio(RADIO_RXON);
  Serial.println("RX");
}

static void rxtimeout_func(osjob_t *job) {
  digitalWrite(LED_BUILTIN, LOW); // off
}

static void rx_func (osjob_t* job) {
  // Blink once to confirm reception and then keep the led on
  digitalWrite(LED_BUILTIN, LOW); // off
  delay(10);
  digitalWrite(LED_BUILTIN, HIGH); // on

  // Timeout RX (i.e. update led status) after 3 periods without RX
  os_setTimedCallback(&timeoutjob, os_getTime() + ms2osticks(3*TX_INTERVAL), rxtimeout_func);

  // Reschedule TX so that it should not collide with the other side's
  // next TX
  os_setTimedCallback(&txjob, os_getTime() + ms2osticks(TX_INTERVAL/2), tx_func);

  Serial.print("Got ");
  Serial.print(LMIC.dataLen);
  Serial.println(" bytes");
  Serial.write(LMIC.frame, LMIC.dataLen);
  Serial.println();

  // Restart RX
  rx(rx_func);
}

static void txdone_func (osjob_t* job) {
  rx(rx_func);
}

// log text to USART and toggle LED
static void tx_func (osjob_t* job) {
  // say hello
  tx("4444||||1111||||2222||||3333", txdone_func);
  // reschedule job every TX_INTERVAL (plus a bit of random to prevent
  // systematic collisions), unless packets are received, then rx_func
  // will reschedule at half this time.
  os_setTimedCallback(job, os_getTime() + ms2osticks(TX_INTERVAL + random(500)), tx_func);
}

// application entry point
void setup() {
  Serial.begin(9600);
  Serial.println("Starting");
  #ifdef VCC_ENABLE
  // For Pinoccio Scout boards
  pinMode(VCC_ENABLE, OUTPUT);
  digitalWrite(VCC_ENABLE, HIGH);
  delay(1000);
  #endif

  pinMode(LED_BUILTIN, OUTPUT);

  // initialize runtime env
  os_init();

  // Set up these settings once, and use them for both TX and RX

  // Use a frequency in the g3 which allows 10% duty cycling.
  ////LMIC.freq = 869525000;
  ////  TP-IoT: uint32_t LORA_CH_10_868 = CH_10_868; //  0xD84CCC; // channel 10, central freq = 865.20MHz  ////  Lup Yuen
  LMIC.freq = 865200000; //// TP-IoT: 865.20 MHz
  
  // Maximum TX power
  LMIC.txpow = 27;
  // Use a medium spread factor. This can be increased up to SF12 for
  // better range, but then the interval should be (significantly)
  // lowered to comply with duty cycle limits as well.
  ////LMIC.datarate = DR_SF9;
  LMIC.datarate =
    (transmission_mode == 1) ? DR_SF12 :  ////  TP-IoT Mode 1.
    (transmission_mode == 5) ? DR_SF10 :  ////  TP-IoT Mode 5.
  
  // This sets CR 4/5, BW125 (except for DR_SF7B, which uses BW250)
  LMIC.rps = updr2rps(LMIC.datarate);

  ////  TP-IoT: Mode 1 is max range but does NOT work with Dragino shield and Hope RF96 chip.
  ////  TP-IoT Gateway runs on:
  ////    case 1:     setCR(CR_5);        // CR = 4/5
  ////                setSF(SF_12);       // SF = 12
  ////                setBW(BW_125);      // BW = 125 KHz
  //  TP-IoT Mode 1: Bw125Cr45Sf4096
  //writeReg(LORARegModemConfig1, FIXED_RH_RF95_BW_125KHZ + FIXED_RH_RF95_CODING_RATE_4_5);
  //writeReg(LORARegModemConfig2, RH_RF95_SPREADING_FACTOR_4096CPS /* + FIXED_RH_RF95_RX_PAYLOAD_CRC_IS_ON */);

  setCr(LMIC.rps, CR_4_5);
  setSf(LMIC.rps, SF12);
  setBw(LMIC.rps, BW125);
    
  Serial.println("Started");
  Serial.flush();

  // setup initial job
  os_setCallback(&txjob, tx_func);
}

void loop() {
  // execute scheduled jobs and events
  os_runloop_once();
}
