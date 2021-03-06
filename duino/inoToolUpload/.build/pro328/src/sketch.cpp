#include <Arduino.h>
#include <Arduino.h>
inline void fixedDelay(int millis);
void setPWM(uint8_t m, int val);
void allOff();
String parseCmdSV(String data, char separator, int index);
void setup();
int applyCmd();
void readLine();
void loop();
#line 1 "src/sketch.ino"
/*
 *
 * CHANGELOG:
 * 2.1 waveDir>0 bug fix
 * 2.2 added set all command
 * 2.3 TODO: pulse train
 *
 * TODO:
 * 3.0 	migrate to floats (% to 0-1)
 * 		enable offsets and gains for each motor
 * */

//#include <Arduino.h>

// FIRMWARE
#define FW 			"2.2"

// 1 for DRV, 0 for mosfet
#define DRV2603 	0

// wb
#define PIN_ENABLE 	4 		// enable motors
#define PIN_TYPE 	8		// toggle type

// all commands are <CMD>;<Par1>;<Par2>\r
#define CMD_SETVAL	"SET"	// set PWM value in % "SET;2;50"
#define CMD_SETALL	"SETA"	// set all PWM value in % "SET;50"

#define CMD_INFO 	"INFO"	// get firmware info "INFO"
#define CMD_ENABLE 	"EN"	// enable motors "EN;1"
#define CMD_SET_LRA "LRA"	// toggle LRA "LRA;1"
#define CMD_OK 		"OK"	// reply OK
#define CMD_ERROR 	"ERR"	// reply ERROR

#define CMD_SQ 		"SQ"	// set motor sequence "SQ;5;0,2,1,4,5"

#define WAVE_2P 	"W2P"	// set amp [%] and on-time [ms] "W2P;60;50"
#define WAVE_EN 	"WEN" 	// wave direction +/-1 and 0 to disable "WEN;-1"

#define WAVE_PARAMS "WP"	// set all params "WP;<float>CSV" @ deprecated
#define WAVE_F0 	"WF0"
#define WAVE_FK 	"WFK"
#define WAVE_A0 	"WA0"
#define WAVE_AK 	"WAK"


//uint8_t pwmPins[6]={11,9,5,3,6,10};
uint8_t pwmPins	[6]={3,5,6,9,10,11};
uint8_t sequence[6]={3,4,0,5,2,1};
uint8_t seqLen=6;

// comm
uint8_t buff[128];
uint8_t res=0;

// wave state
bool waveRunning=false;
int wavetOff=1; // ms
int wavetOn=1;	// ms
int waveDir=1;
int waveA=50;	// % PWM

// wave memory
int mId=0;
int mState=0;
unsigned long time;
unsigned long time_1, sinceLastOnTrans;

// communication
String inputString = "";         // a string to hold incoming data
String cmd="";
String val1="";
String val2="";
boolean stringComplete = false;  // whether the string is complete

// haven't checked if "inline" works here
/*If you change TCCR0B, it affects millis() and delay(). They will count time faster or slower than normal if you
 * change the TCCR0B settings. Below is the adjustment factor to maintain consistent behavior of these functions */
inline void fixedDelay(int millis) {delay(64*millis);}

void setPWM(uint8_t m, int val) {
	if (DRV2603)
		analogWrite(pwmPins[sequence[m]], 127+val*127/100);
	else
		analogWrite(pwmPins[sequence[m]],val*255/100);
}

void allOff() {
	for (int i=0;i<6;i++)
		setPWM(i,0);
}

/* parse values */
String parseCmdSV(String data, char separator, int index) {
 	int found = 0;
	int strIndex[] = {0, -1  };
	int maxIndex = data.length()-1;
	for(int i=0; i<=maxIndex && found<=index; i++){
		if(data.charAt(i)==separator || i==maxIndex){
		found++;
		strIndex[0] = strIndex[1]+1;
		strIndex[1] = (i == maxIndex) ? i+1 : i;
  		}
	}
  	return found>index ? data.substring(strIndex[0], strIndex[1]) : "";
}

void setup()  {
	// set PWM timer resolution highest
	TCCR0B = TCCR0B & 0b11111000 | 0x01;
	TCCR1B = TCCR1B & 0b11111000 | 0x01;
	TCCR2B = TCCR2B & 0b11111000 | 0x01;
	// config PWM outputs
	for (int i=0;i<6;i++) {
		pinMode(pwmPins[i], OUTPUT);
		setPWM(pwmPins[i], 0);
	}
	if (DRV2603) {
		// init enable pin
		pinMode(PIN_ENABLE, OUTPUT);
		digitalWrite(PIN_ENABLE, LOW);
		// init driver type to ERM
		pinMode(PIN_TYPE, OUTPUT);
		digitalWrite(PIN_TYPE, LOW);
	}
		inputString.reserve(30);
		Serial.begin(38400);//,SERIAL_8N1);

}

int applyCmd(){
	cmd =  parseCmdSV(inputString, ';', 0);
	val1 = parseCmdSV(inputString, ';', 1);
    val2 = parseCmdSV(inputString, ';', 2);
   
	// GENERAL
	if (cmd==CMD_SETVAL) {
		setPWM(val1.toInt(),val2.toInt());
	} else
	if (cmd==CMD_SETALL) {
		for (int i=0;i<6;i++)
			setPWM(i,val2.toInt());
	} else
	if (cmd==CMD_INFO) {
		Serial.print("FW: ");
		Serial.print(FW);
		Serial.print("  DRV: ");
		Serial.print(DRV2603);
		//Serial.print("  SEQ: ");
		//Serial.print(sequence);
		Serial.print(" * ");
	} else
	if (cmd==CMD_ENABLE) {
		digitalWrite(PIN_ENABLE,val1.toInt()>0 ? HIGH : LOW);			
	} else
	if (cmd==CMD_SET_LRA) {
		if (DRV2603)
			digitalWrite(PIN_TYPE, 	val1.toInt()>0 ? HIGH : LOW);
		else return -1; // NOT IMPLEMENTED
	} else
	if (cmd==CMD_SQ) {
		seqLen=val1.toInt();
		if (seqLen>6) return -1;
		for (int i=0; i<seqLen; i++){
			sequence[i]=parseCmdSV(val2, ',', i).toInt();
		}
	} else

	// WAVE
	if (cmd==WAVE_2P) {
		wavetOff=1;
		wavetOn=val1.toInt();
		waveDir=0;
		waveA=val2.toInt();
	} else 
	if (cmd==WAVE_EN) {
		waveDir=val1.toInt();
		waveRunning=waveDir!=0;
		if (!waveRunning)
			allOff();
	} else
		return -1;
	return 1;
}

void readLine() {
	while (Serial.available()) {
		char inChar = (char)Serial.read();
		inputString += inChar;
		if (inChar == '\r') {
			stringComplete = true;
			return;
		}
	}
}

void loop()  {
	// process commands
	readLine();
	if (stringComplete) {
		//Serial.println(inputString); 
		if (applyCmd()>0)
			Serial.println(CMD_OK);
		else 
			Serial.println(CMD_ERROR);	
		inputString = "";
		stringComplete = false;
	}

	// wave mode
	if (waveRunning) {
		time = (millis()/64);		
		sinceLastOnTrans=time-time_1;
		
		// turn off
		if (sinceLastOnTrans > wavetOn && mState==1) {
			mState = 0;
			setPWM(mId,0);
			if (waveDir>0)
				mId<seqLen-1? mId++ : mId=0;
			else
				mId>0 		? mId-- : mId=seqLen-1;	
		}
		if ((sinceLastOnTrans > wavetOff+wavetOn) && mState==0){
			mState=1;
			setPWM(mId,waveA);
			time_1=time;
		}
	}
}



