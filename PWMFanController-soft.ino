/*
   PWM Fan Controller
 */

byte charAuto[8] = {
	0b11111,
	0b10001,
	0b10101,
	0b10101,
	0b10001,
	0b10101,
	0b10101,
	0b11111,
};

byte charManual[8] = {
	0b11111,
	0b01110,
	0b00100,
	0b01010,
	0b01110,
	0b01110,
	0b01110,
	0b11111,
};

byte charOverride[8] = {
	0b11111,
	0b11011,
	0b11011,
	0b11011,
	0b11011,
	0b11111,
	0b11011,
	0b11111,
};

byte charPercent[8] = {
	0b00111,
	0b00110,
	0b11101,
	0b11011,
	0b10111,
	0b01100,
	0b11100,
	0b11111,
};

#include <LiquidCrystal.h>
/*
The circuit:
LCD       Arduino
VSS  1    GND
VDD  2    5V
V0   3    resistor (contrast)
RS   4    13
RW   5    GND
E    6    12
D0-3 7~10  -
D4  11    11
D5  12     7
D6  13     6
D7  14     5
A   15    resistor ↔ 5V
K   16    GND

http://www.arduino.cc/en/Tutorial/LiquidCrystal
*/
// initialize the library with numbers of the interface pins
LiquidCrystal lcd(13, 12, 11, 7, 6, 5);

// Analog output pin that the MOSFET is attached to
// only below pins support PWM
// pins / timer register / base freq / allowed divisors
// 5,6  / TCCR0B         / 31250     / 1, 8, 64, 256, 1024
// 9,10 / TCCR1B         / 31250     / 1, 8, 64, 256, 1024
// 3,11 / TCCR2B         / 62500     / 1, 8, 32, 64, 128, 256, 1024
// notes: messing with timer TCCR0B fucks up Arduino timing
const int mosfetPin = 9;
const int mosfetDivisor = 1;

// Encoder pins
// They need to support interrupts
// https://www.arduino.cc/en/Reference/AttachInterrupt
// interrupt pins (can be shared)
const int encoderInterruptPinA = 3;
const int encoderInterruptPinB = 3;
// output pins
const int encoderOutputPinA = 4;
const int encoderOutputPinB = 3;
// encoder switch/button
const int encoderInterruptPinS = 2;
const int encoderOutputPinS = 2;
// encoder edge values
const int encoderMin = 0;
const int encoderMax = 20;
// encoder current and default value
volatile int encoderValue = 4;

// value output to the PWM (analog out)
volatile int outputValue = 0;

// modes
const short int modeAuto = 0;
const short int modeManual = 1;
const short int modeOverride = 2;
// default mode
volatile short int mode = modeAuto;

// auto turn off after this many s (60*60 = 1h)
volatile unsigned long autoOffTime = 90*60;
volatile unsigned long idleSince = millis()/1000;

//////////////////////
// new interrupts

// treshold to counteract bouncing
const unsigned long int threshold = 2500;

// rotaryHalfSteps is the counter of half-steps. The actual
// number of steps will be equal to rotaryHalfSteps / 2
volatile unsigned long rotaryHalfSteps = encoderValue * 2;

// Working variables for the interrupt routines
volatile unsigned long int0time = 0;
volatile unsigned long int1time = 0;
volatile uint8_t int0signal = 0;
volatile uint8_t int1signal = 0;
volatile uint8_t int0history = 0;
volatile uint8_t int1history = 0;

void encoderPinAint() {
	if (micros() - int0time < threshold)
		return;
	int0history = int0signal;
	int0signal = bitRead(PIND, encoderOutputPinA);
	if ( int0history==int0signal )
		return;
	int0time = micros();
	if ( int0signal == int1signal ) {
		if (rotaryHalfSteps < (encoderMax*2)) {
			rotaryHalfSteps++;
		}
	} else {
		if (rotaryHalfSteps > (encoderMin*2)) {
			rotaryHalfSteps--;
		}
	}
	encoderValue = (rotaryHalfSteps / 2);
	// switch to manual mode on any change
	mode = modeManual;
	idleSince = millis()/1000;
}

void encoderPinBint() {
	if ( micros() - int1time < threshold )
		return;
	int1history = int1signal;
	int1signal = bitRead(PIND, encoderOutputPinB);
	if ( int1history==int1signal )
		return;
	int1time = micros();
}

// Handle all interrupts, detect which device caused it
// Can work with interrupts on single separate pin, different than output pins
//volatile unsigned short int encoderInt_PinAVal    = LOW;
//volatile unsigned short int encoderInt_PinAOldVal = LOW;
//volatile unsigned short int encoderInt_PinBVal    = LOW;
//volatile unsigned short int encoderInt_PinBOldVal = LOW;
void encoderInt() {
	// if interrupt by pin A
	//    do pin A interrupt
	//if () {
	encoderPinAint();
	//}

	// if interrupt by pin B
	//    do pin B interrupt
	//if () {
	encoderPinBint();
	//}
}

volatile unsigned long intTimeS = 0;
volatile uint8_t intSignalS = 0;
void encoderSwitchInt() {
	// react to only button moving down, not up
	intSignalS = bitRead(PIND, encoderOutputPinS);
	if (intSignalS != 0) return;
	// prevent double clicking
	if (millis() - intTimeS < threshold/10) return;
	intTimeS = millis();
	switch(mode) {
	case modeAuto:
		mode = modeManual;
		encoderValue = (rotaryHalfSteps / 2);
		break;
	case modeManual:
		mode = modeOverride;
		encoderValue = 15;
		idleSince = millis()/1000 - (autoOffTime/3)*2;
		break;
	case modeOverride:
		mode = modeAuto;
		encoderValue = 0;
		break;
	}
}
// new interrupts end
//////////////////////

void setup() {
	//void setPwmFrequency(int pin, int divisor) {
	byte mode;
	if (mosfetPin == 5 || mosfetPin == 6 || mosfetPin == 9 || mosfetPin == 10) {
		switch(mosfetDivisor) {
			case 1: mode = 0x01; break;
			case 8: mode = 0x02; break;
			case 64: mode = 0x03; break;
			case 256: mode = 0x04; break;
			case 1024: mode = 0x05; break;
			default: return;
		}
		if(mosfetPin == 5 || mosfetPin == 6) {
			TCCR0B = TCCR0B & 0b11111000 | mode;
		} else if(mosfetPin == 9 || mosfetPin == 10) {
			TCCR1B = TCCR1B & 0b11111000 | mode;
		}
	} else if (mosfetPin == 3 || mosfetPin == 11) {
		switch(mosfetDivisor) {
			case 1: mode = 0x01; break;
			case 8: mode = 0x02; break;
			case 32: mode = 0x03; break;
			case 64: mode = 0x04; break;
			case 128: mode = 0x05; break;
			case 256: mode = 0x06; break;
			case 1024: mode = 0x07; break;
			default: return;
		}
		TCCR2B = TCCR2B & 0b11111000 | mode;
	}
	//}

	attachInterrupt(digitalPinToInterrupt(encoderInterruptPinA), encoderInt, CHANGE);
	attachInterrupt(digitalPinToInterrupt(encoderInterruptPinB), encoderInt, CHANGE);

	attachInterrupt(digitalPinToInterrupt(encoderInterruptPinS), encoderSwitchInt, CHANGE);

	//Serial.begin(9600);

	// create custom chars
	// soemtimes they get corrupted, device reset helps
	lcd.createChar(0, charAuto);
	lcd.createChar(1, charManual);
	lcd.createChar(2, charOverride);
	lcd.createChar(6, charPercent);

	// set up the LCD
	lcd.begin(16, 2);
}

void loop() {
	// map encoder value to PWM value
	outputValue = map(encoderValue, encoderMin, encoderMax, 255, 0);

	analogWrite(mosfetPin, outputValue);

	// set the cursor to column 0, line 0
	lcd.setCursor(0, 0);

	//lcd.print("~");

	// print mode
	lcd.setCursor(1, 0);
	switch(mode) {
	case modeAuto:
		lcd.write(byte(modeAuto));
		break;
	case modeManual:
		lcd.write(byte(modeManual));
		break;
	case modeOverride:
		lcd.write(byte(modeOverride));
		break;
	}

	// print nice time info
	lcd.setCursor(3, 0);
	int offIn = autoOffTime-(millis()/1000-idleSince);
	lcd.print(offIn/60/60);
	lcd.print(":");
	lcd.print(offIn/60%60<10?"0":"");
	lcd.print(offIn/60%60);
	lcd.print(":");
	lcd.print(offIn%60<10?"0":"");
	lcd.print(offIn%60);

	// print nice power info
	lcd.setCursor(1, 1);
	lcd.write(byte(6)); // charPercent
	lcd.setCursor(3, 1);
	switch (encoderValue) {
	case encoderMin:
		lcd.print("OFF");
		break;
	case encoderMax:
		lcd.print("MAX");
		break;
	default:
		lcd.print(map(encoderValue, encoderMin, encoderMax, 0, 100));
		lcd.print("% ");
		break;
	}

	// turn off fan after set time
	//   and switch between on and off in auto mode
	if ((idleSince + autoOffTime) <= millis()/1000) {
		mode = modeAuto;
		if (encoderValue == 0) {
			encoderValue = 5;
			idleSince = millis()/1000 - (autoOffTime/6)*5; // work 1/6th of idle time
		} else {
			encoderValue = 0;
			idleSince = millis()/1000;
		}
	}

	// wait X milliseconds before the next loop
	delay(100);
}
