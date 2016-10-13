/*

   CHANGELOG:
   2.1 waveDir>0 bug fix
   2.2 added set all command
   3.0 i2c
   TODO: pulse train

   TODO:
   3.0 	migrate to floats (% to 0-1)
  		enable offsets and gains for each motor
 * */

//#include <Arduino.h>
#include "DRV2605.h"
#include "I2C.h"

// FIRMWARE
#define FW 			"3.0"

// all commands are <CMD>;<Par1>;<Par2>;<Par3>...\r
/* analytics */
#define CMD_TEST    "TEST"
#define CMD_SCAN    "SCAN"
#define CMD_INFO 	  "INFO"	// get firmware info "INFO"

/* buzz & clicks */
#define CMD_SETVAL  "SET" // set PWM value in % "SET;2;50"
#define CMD_SETALL  "SETA"  // set all PWM value [0,100] "SETA;;50"
#define CMD_CLICK   "C"     // set PWM duration "C;2;50;50"
#define CMD_CLICK_ALL   "CA"     // set PWM duration "CA;;50;50"

#define CMD_ENABLE 	"EN"	// enable motors "EN;1"
#define CMD_SET_LRA "LRA"  // toggle LRA "LRA;1"

#define CMD_OK 		"OK"	// reply OK
#define CMD_ERROR 	"ERR"	// reply ERROR

#define CMD_SQ 	"SQ"	// set motor sequence "SQ;5;0,2,1,4,5"
#define CMD_SETOFS  "SETOFF" // analogue syntax
#define CMD_SETGAIN "SETGAIN"// analogue syntax

#define WAVE_2P 	"W2P"	// set on-time [s] and amp [0, 1]  "W2P;60;50"
#define WAVE_EN 	"WEN" 	// wave direction +/-1 and 0 to disable, larger than 1 is duration in milis "WEN;-1;"

DRV2605 drv;

uint8_t hapAdr[] = {0x0A, 0x1A, 0x2A, 0x4A, 0x5A, 0x6A};

uint8_t seqLen = 6;
uint8_t sequence[] = {4, 1, 2, 0, 5, 3};

// TBD
float hapOffs[32];
float hapGains[32];

// comm
uint8_t buff[64];
uint8_t res = 0;

// wave param
int wavetOff = 1; // ms
int wavetOn = 1;	// ms
int waveDir = 1;
int waveA = 50;	// % PWM
unsigned long timeExpire = 0;

// wave memory
bool waveRunning = false;
int mId = 0;
int mState = 0;
unsigned long time;
unsigned long time_1, sinceLastOnTrans;


// communication
boolean stringComplete = false;  // whether the string is complete
char inpoutStr[100];
int strLen=0;

void setPWM(uint8_t m, int val) {
  drv.setPWM(hapAdr[sequence[m]], val * 127 / 100);
}

void allOff() {
  for (int i = 0; i < 6; i++)
    setPWM(i, 0);
}

/* parse values, assumes first cell contains string, the rest contain ints */
int parseCMD(char * name, int * ints) {
  int count=0;
  char * pch;
  pch = strtok (inpoutStr,",;");
  while (pch != NULL && count < 11)  {
    if (!count)
        strcpy(name, pch);
    else
        ints[count-1]=atoi(pch);
    count++;
    pch = strtok (NULL, ",;");
  }
  return count;
}

/* walk through cmds and execute */
int applyCmd() {
  char cmd[10];
  int ints[10];
  int cmdPlusInts=parseCMD(cmd, ints);
  /* 
  // debug
  Serial.println();
  Serial.println(cmd);
  for (int i=0;i<cmdPlusInts-1;i++) {
    Serial.println(ints[i]);  
  }
  return 1;
  */
  // GENERAL
  if      (strcmp(cmd,CMD_SETVAL)==0) {
    setPWM(ints[0], ints[1]);
  } 
  else if (strcmp(cmd,CMD_SETALL)==0) {
    for (int i = 0; i < 6; i++)
      setPWM(i, ints[0]);
  } 
  else if (strcmp(cmd,CMD_CLICK)==0) {
    setPWM(ints[0], ints[1]);
    delay(ints[2]); 
    setPWM(ints[0], 0);
  }
  else if (strcmp(cmd,CMD_CLICK_ALL)==0) {
    for (int i = 0; i < 6; i++)
      setPWM(i, ints[0]);
    delay(ints[1]); 
    for (int i = 0; i < 6; i++)
      setPWM(i, 0);
  }

  
  else if (strcmp(cmd,CMD_INFO)==0) {
    Serial.print("FW: ");
    Serial.print(FW);
    Serial.print("  DRV2605l ");
    Serial.print("  SEQ: ");
    for (int i=0;i<seqLen;i++){
      Serial.print(sequence[i]);
      Serial.print(' ');
    }
    Serial.print(" * ");
  } 
  
  else if (strcmp(cmd,CMD_TEST)==0) {
    for (uint8_t j = 0; j < 6; j++) {
      uint8_t i=hapAdr[sequence[j]];
      setPWM(j,20);
      delay(100);
      setPWM(j, 0);
      delay(100);
    }
  } 
  
  else if (strcmp(cmd,CMD_SCAN)==0) {
    pinMode(13,OUTPUT);
    digitalWrite(13,I2c.scan());
  } 
  
  else if (strcmp(cmd,CMD_ENABLE)==0) {
    return -1; // NOT IMPLEMENTED
  } 
  
  else if (strcmp(cmd,CMD_SET_LRA)==0) {
    return -1; // NOT IMPLEMENTED
  } 
  
  else if (strcmp(cmd,CMD_SQ)==0) {
    seqLen = ints[0];
    if (seqLen != (cmdPlusInts-2)) return -1;
    for (int i = 0; i < seqLen; i++) {
      sequence[i] = ints[i+1];
    }
  } 
  
  else if (strcmp(cmd,WAVE_2P)==0) {
      wavetOff = 1;
      wavetOn = ints[0];
      waveDir = 0;
      waveA = ints[1];
  } 
  
  else if (strcmp(cmd,WAVE_EN)==0) {
      waveDir = ints[0];
      if (waveDir*waveDir>1)
        timeExpire=millis()+long(waveDir>0 ? waveDir : -waveDir);
      else timeExpire=0;
      waveRunning = waveDir != 0;
      if (!waveRunning)
        allOff();
  } 
  
  else
      return -1;
  return 1;
}

/* read all chars and raise flag if command complete (\r found) */
void readLine() {
  while (Serial.available()) {
    char inChar = (char)Serial.read();
    inpoutStr[strLen] = inChar;
    strLen++;
    if (inChar == '\r') {
      stringComplete = true;
      return;
    }
  }
}



void setup()  {
  Serial.begin(38400);//,SERIAL_8N1);
  I2c.begin();
  drv.begin();
  for (uint8_t j = 0; j < 6; j++) {
    uint8_t i=hapAdr[sequence[j]];
    drv.setAddress(i);
    drv.init(i,false);
    drv.setRealtimeValue(0);
    delay(10);
  }
}

/* reads serial buffer, executes command if complete, controls the wave */
void loop()  {
  // read all chars in the buffer
  readLine();

  // execute command if complete and reset buffer
  if (stringComplete) {
    //Serial.println(inputString);
    if (applyCmd() > 0)
      Serial.println(CMD_OK);
    else
      Serial.println(CMD_ERROR);
    
    memset(inpoutStr, 0, strLen);
    strLen=0;
    stringComplete = false;
  }

  // wave mode
  if (waveRunning) {
    time = millis();
    if (timeExpire > 0)
      if (long(timeExpire-time) < 0){
        waveRunning=false;
        allOff();
        return;
    }  
    sinceLastOnTrans = time - time_1;
    // turn off
    if (sinceLastOnTrans > wavetOn && mState == 1) {
      mState = 0;
      setPWM(mId, 0);
      if (waveDir > 0)
        mId < seqLen - 1 ? mId++ : mId = 0;
      else
        mId > 0 		     ? mId-- : mId = seqLen - 1;
    }
    if ((sinceLastOnTrans > wavetOff + wavetOn) && mState == 0) {
      mState = 1;
      setPWM(mId, waveA);
      time_1 = time;
    }
  }
}



