/******************************************************/
//       THIS IS A GENERATED FILE - DO NOT EDIT       //
/******************************************************/

#include "application.h"
#line 1 "/Users/chipmc/Documents/Maker/Particle/Projects/Cellular-Sample-Stats/src/Cellular-Sample-Stats.ino"
/*
* Project Environmental Sensor - Soil Sensor that can go an entire season on one set of LiPo batteries
* Description: Cellular Connected Boron with soil and environmental sensors powered by 18650 LiPo batteries for 150 days
* Method of operation - device will wake 60 times an hour and take a sample.  At the end of the hour, the device will send data and stats on hourly data set
*

* Author: Chip McClelland chip@seeinsights.com
* Sponsor: Colorado State University
* License: GPL v3
* Date: 8 September 2019
*/

//**************************************************************
//******** Proof of Concept Code - Not for Production **********
//**************************************************************

// v1.00 - Initial Release - Rough program outline

void setup();
void loop();
void sendEvent();
bool takeMeasurements();
bool connectToParticle();
bool disconnectFromParticle();
bool notConnected();
int measureNow(String command);
int setLowPowerMode(String command);
bool meterParticlePublish(void);
void fullModemReset();
#line 19 "/Users/chipmc/Documents/Maker/Particle/Projects/Cellular-Sample-Stats/src/Cellular-Sample-Stats.ino"
#define SOFTWARERELEASENUMBER "1.00"               // Keep track of release numbers

// Prototypes and System Mode calls
SYSTEM_MODE(SEMI_AUTOMATIC);                        // This will enable user code to start executing automatically.
SYSTEM_THREAD(ENABLED);                             // Means my code will not be held up by Particle processes.

// Included Libraries
#include "RunningAverage.h"

// Add libraries for sensors here

// Initalize library objects here
RunningAverage myRA(60);


// State Machine Variables
enum State { INITIALIZATION_STATE, ERROR_STATE, IDLE_STATE, MEASURING_STATE, REPORTING_STATE, RESP_WAIT_STATE, SLEEPING_STATE};
char stateNames[8][14] = {"Initialize", "Error", "Idle", "Measuring", "Reporting", "Response Wait", "Sleeping"};
State state = INITIALIZATION_STATE;
State oldState = INITIALIZATION_STATE;

// Pin Constants
const int blueLED =       D7;                                           // This LED is on the Electron itself

// Timing Variables
const int wakeBoundary = 0*3600 + 1*60 + 0;                             // 0 hour 1 minutes 0 seconds
const unsigned long stayAwakeLong = 25000;                              // In lowPowerMode, how long to stay awake every hour
const unsigned long stayAwakeShort = 100;                               // In LowPowerMode how long to we stay awake after a sample
unsigned long stayAwakeTimeStamp = 0;                                   // Timestamps for our timing variables..
unsigned long stayAwake = stayAwakeLong;                                // Stores the time we need to wait before napping
const unsigned long resetWait = 30000;                                  // How long will we wait in ERROR_STATE until reset
unsigned long resetTimeStamp = 0;                                       // Resets - this keeps you from falling into a reset loop

// Program Variables
bool ledState = LOW;                                // variable used to store the last LED status, to toggle the light
const char* releaseNumber = SOFTWARERELEASENUMBER;  // Displays the release on the menu
byte controlRegister;                               // Stores the control register values
int samples = 0;
bool lowPowerMode = false;                          // Tells the device whether to go to sleep or not

// Variables for montoring / troubleshooting
char SignalString[64];                              // Used to communicate Wireless RSSI and Description
const char* radioTech[8] = {"Unknown","None","WiFi","GSM","UMTS","CDMA","LTE","IEEE802154"};
float soilMoisture;                                 // Where we will store the soilMoisture data
char soilMoistureString[16];                       // Where we can dis

// Timing Variable
byte currentMinutePeriod;                                         // control timing when using 5-min samp intervals

void setup()                                                      // Note: Disconnected Setup()
{
  char StartupMessage[64] = "Startup Successful";                 // Messages from Initialization
  state = IDLE_STATE;

  pinMode(blueLED, OUTPUT);                                             // declare the Blue LED Pin as an output
  digitalWrite(blueLED,HIGH);                                           // Will turn on the BlueLed when the device is awake

  myRA.clear();                                                         // Clear the buffer

  Particle.variable("lowPowerMode",lowPowerMode);
  Particle.variable("Release", releaseNumber);

  Particle.function("Measure-Now",measureNow);
  Particle.function("LowPowerMode",setLowPowerMode);

  if (!connectToParticle()) state = ERROR_STATE;

  // Load time variables
  currentMinutePeriod = Time.minute();

  // And set the flags from the control register
  controlRegister = EEPROM.read(0);                                     // Read the Control Register for system modes so they stick even after reset
  lowPowerMode    = (0b00000001 & controlRegister);                     // Set the lowPowerMode

  if(Particle.connected()) Particle.publish("Startup",StartupMessage,PRIVATE);   // Let Particle know how the startup process went

  stayAwakeTimeStamp = millis();                                        // Time stamp to keep us from going to sleep too early
}

void loop()
{
  switch(state) {
  case IDLE_STATE:
    if (lowPowerMode && (millis() - stayAwakeTimeStamp) >= stayAwake) state = SLEEPING_STATE;
    if (Time.minute() != currentMinutePeriod) state = MEASURING_STATE; 
    break;

  case MEASURING_STATE: {
    takeMeasurements();                                                 // Take samples from sensors

    if (Time.minute() == 0) state = REPORTING_STATE;
    else state = IDLE_STATE;

    if (Particle.connected()) {
      char data[64];                                                      // Store the date in this character array - not global
      waitUntil(meterParticlePublish);
      snprintf(data, sizeof(data), "Sample %4.0f%%, Average %4.0f%% with %i samples", soilMoisture, myRA.getAverage(), myRA.getCount());
      if(Particle.connected()) Particle.publish("Minute Sample", data, PRIVATE); // Publish hourly average
    }

    } break;

  case REPORTING_STATE:
    if (connectToParticle()) {
      if (Time.hour() == 12) Particle.syncTime();                       // Set the clock each day at noon
      sendEvent();                                                      // Send data via Webhook or other Method  
      stayAwake = stayAwakeLong;
      state = IDLE_STATE;                                               // Done sampling - back to idle
    }
    else state = ERROR_STATE;
    break;

  case SLEEPING_STATE: {                                                // This state is triggered once the park closes and runs until it opens
    if (Particle.connected()) {
      waitUntil(meterParticlePublish);
      Particle.publish("State","Going to Sleep",PRIVATE);
      delay(1000);                                                      // So the message gets out before disconnecting
      disconnectFromParticle();                                         // If connected, we need to disconned and power down the modem
    }
    digitalWrite(blueLED,LOW);                                          // Turn off the LED
    stayAwake = stayAwakeShort;                                         // Go to sleep quickly.
    // Off to bed till the next minute period
    System.sleep(D6,RISING,60 - Time.second());                         // Wake in time for next minute reading.  Pin is just a placeholder - only wake on time in this example
    // Once we wake back up - do these things
    state = IDLE_STATE;                                                 // need to go back to idle immediately after wakup
    digitalWrite(blueLED,HIGH);
    stayAwakeTimeStamp = millis();                                      // Time stamp to keep us from going to sleep too early
    } break;

  case ERROR_STATE:                                                     // Can be more complex - where we deal with errors
     if (millis() > resetTimeStamp + resetWait) {
      if (Particle.connected()) Particle.publish("State","Error State - Reset", PRIVATE);    // Brodcast Reset Action
      delay(2000);
      fullModemReset();                                                 // Full Modem reset and reboot
    }
    break;
  }
}

void sendEvent()                                                        // Simple publish for this example - would be a Particle Webhook in real use
{
  char data[64];                                                        // Store the date in this character array - not global
  waitUntil(meterParticlePublish);
  snprintf(data, sizeof(data), "The average SoilMoisture is %4.0f%% with %i samples", myRA.getAverage(), myRA.getCount());
  if(Particle.connected()) Particle.publish("Hourly Report", data, PRIVATE); // Publish hourly average
  samples = 0;                                                          // Data received - clear the running average and start over for the next hour
  myRA.clear();
}

// These are the functions that are part of the takeMeasurements call

bool takeMeasurements() {                                               // Mocked up here for the call - need to replace with your real readings
  soilMoisture = random(100);                                           // SoilMoisture Measurements here
  snprintf(soilMoistureString,sizeof(soilMoistureString), "%4.1f %%", soilMoisture);

  myRA.addValue(soilMoisture);                                          // Add this sample to the running average
  samples++;                                                            // Increment the sample currentCountsTimeAddr

  currentMinutePeriod = Time.minute();                                  // So we only count once in a minute
  return 1;                                                             // Done, measurements take and the data array is stored as an obeect in EEPROM                                         
}

// These functions control the connection and disconnection from Particle
bool connectToParticle() {
  Cellular.on();
  Particle.connect();
  // wait for *up to* 5 minutes
  for (int retry = 0; retry < 300 && !waitFor(Particle.connected,1000); retry++) {
    // Code I want to run while connecting
    Particle.process();
  }
  if (Particle.connected()) {
    waitFor(Time.isValid, 60000);
    return 1;                                                           // Were able to connect successfully
  }
  else return 0;                                                        // Failed to connect
}

bool disconnectFromParticle()
{
  Particle.disconnect();                                                // Otherwise Electron will attempt to reconnect on wake
  Cellular.off();
  delay(1000);                                                          // Bummer but only should happen once an hour
  return true;
}

bool notConnected() {
  return !Particle.connected();                                         // This is a requirement to use waitFor
}


// These are the particle functions that allow you to configure and run the device
// They are intended to allow for customization and control during installations
// and to allow for management.

int measureNow(String command)                                          // Function to force sending data in current hour
{
  if (command == "1")
  {
    state = MEASURING_STATE;
    return 1;
  }
  else return 0;
}

int setLowPowerMode(String command)                                     // This is where we can put the device into low power mode if needed
{
  if (command != "1" && command != "0") return 0;                       // Before we begin, let's make sure we have a valid input
    controlRegister = EEPROM.read(0);
  if (command == "1")                                                   // Command calls for setting lowPowerMode
  {
    Particle.publish("Mode","Low Power",PRIVATE);
    controlRegister = (0b00000001 | controlRegister);                   // If so, flip the lowPowerMode bit
    lowPowerMode = true;
  }
  else if (command == "0")                                              // Command calls for clearing lowPowerMode
  {
    Particle.publish("Mode","Normal Operations",PRIVATE);
    controlRegister = (0b1111110 & controlRegister);                    // If so, flip the lowPowerMode bit
    lowPowerMode = false;
  }
  EEPROM.write(0,controlRegister);           // Write it to the register
  return 1;
}

bool meterParticlePublish(void)
{
  static unsigned long lastPublish = 0;
  if(millis() - lastPublish >= 1000) {                                  // Particle only lets us publish once a second
    lastPublish = millis();
    return 1;
  }
  else return 0;
}

void fullModemReset() {  // Adapted form Rikkas7's https://github.com/rickkas7/electronsample

	Particle.disconnect(); 	                                              // Disconnect from the cloud
	unsigned long startTime = millis();  	                                // Wait up to 15 seconds to disconnect
	while(Particle.connected() && millis() - startTime < 15000) {
		delay(100);
	}
	// Reset the modem and SIM card
	// 16:MT silent reset (with detach from network and saving of NVM parameters), with reset of the SIM card
	Cellular.command(30000, "AT+CFUN=16\r\n");
	delay(1000);
	// Go into deep sleep for 10 seconds to try to reset everything. This turns off the modem as well.
	System.sleep(SLEEP_MODE_DEEP, 10);
}
