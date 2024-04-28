/** after time changes
* more repeats to connect to wifi, and try indefinitely
* when re-drawing screen for NOW resize rectable 1 bit wider (to cover
overflowing red line)

*/
#include <Arduino.h>
#include "SPIFFS.h"


#include <ArduinoJson.h>  // for JSON Processing (Shelly data)
#include <HTTPClient.h>   // To retrieve Shelly data
#include <SPI.h>  // SPI protocol to address the TFT
#include <TFT_eSPI.h>  // Graphics and font library for ST7735/ST7789/ILI9341/ILI9163/S6D02A1/etc
#include <WiFi.h>
#include <BH1750.h>   // Light sensor library

// #include "FS.h"  // touchscreen Calibration data is stored in SPIFFS so we need to include it
#include "configuration.h"  // personal configs and passwords
#include "time.h"           // to get and process the time


// bla bla - just a test
 
TFT_eSPI tft = TFT_eSPI();  // Invoke library, pins defined in User_Setup.h
TFT_eSprite spr = TFT_eSprite(&tft);  // use sprites

unsigned long lastTouchTime = 0;  // Stores the last time a touch was registered
unsigned long lastDisplayUpdate = 0;  // when display was last updated
unsigned long lastProcessSample = 0;  // when the last sample was processed


#define SDA_PIN 32  // GPIO pin number for SDA
#define SCL_PIN 33  // GPIO pin number for SCL

BH1750 lightSensor;

const long interval30s =
    30000;  // Interval at which to run function (milliseconds) (30 sec)

const long intveral4m = 240000;  // Interval at which to run function (milliseconds) (4 min)    
unsigned long previousMillis =
    interval30s * 3;  // Stores the last time the function was executed //
                   // Initialize with high value, so it runs right away

bool before4SecFlag =
    false;  // flag to to get the shelly data 4 sec before interval30s
bool before9SecFlag =
    false;  // flag to to get the shelly data 9 sec before interval30s
bool dailyTaskExecuted = false;  // flag to mark completed

#define TFT_DARKBLUE 0x000F  // A darker shade of blue for header of display
const int TFTBacklightPWMPin = 15;  // control the backlight of the TFT

const int dailyDataEntries = 360;  // we have a data entry for every 4 minutes
                                   // -> 15 / hour -> 360 per day

static const int lastDay = 3;  // Today (0) + 6 (1,2,... 6) previous days

time_t now;
struct tm timeinfo;  // Global timeinfo structure

// struct to hold the 3 relevant power data for a measurement
struct PowerData {
  int fremdverbrauch;  // External consumption
  int eigenverbrauch;  // Self-consumption
  int einspeisung;     // Feed-in
};

// struct to hold the 3 relevant power data for a measurement
struct PowerDataFloat {
  float fremdverbrauch;  // External consumption
  float eigenverbrauch;  // Self-consumption
  float einspeisung;     // Feed-in
};

// for keeping the date of the data
struct Date {
  int day;
  int month;
};

// struct for the shelly responses
struct ShellyDataResult {
  float data;
  bool isSuccess;
};

ShellyDataResult house;  // holding the data retrived for the house
ShellyDataResult solar;  // holding the data retrieved for the solar panel
PowerData data;          // used to store the powerData

int nextDisplay = -1;  // Start with screen for TODAY
int currentDisplay =
    0;  // Just be different for the start, so it erases all on the first change


class BacklightController {
  private:
    int pwmPin;
    BH1750& lightSensor;
    int currentBrightness = 255;
    int targetBrightness = 255;
    unsigned long previousMillis;
    const int interval = 10;  // Time interval for gradual adjustment

  public:
    BacklightController(int pin, BH1750& sensor) : pwmPin(pin), lightSensor(sensor) {
      pinMode(pwmPin, OUTPUT);
    }

    void updateBacklight() {
 
      unsigned long currentMillis = millis();
      if (currentMillis - previousMillis >= interval) {
              uint16_t lux = lightSensor.readLightLevel();
      targetBrightness = map(lux, 0, 100, 2, 255);
      if (targetBrightness > 255){
        targetBrightness = 255; }
        previousMillis = currentMillis;

        if (currentBrightness < targetBrightness) {
          currentBrightness++;
        } else if (currentBrightness > targetBrightness) {
          currentBrightness--;
        }

        analogWrite(pwmPin, currentBrightness);
        /* Serial.print("Light: ");
        Serial.print(lux);
        Serial.print("lx, ");
        Serial.print("Current Brightness: " + String(currentBrightness));
        Serial.println(", Target Brightness: " + String(targetBrightness)); */
      }
    }
};

BacklightController backlightController(TFTBacklightPWMPin, lightSensor);



class DailyDataManager {
 public:
  DailyDataManager() {}

  void Initialize() {
    sampleCount = 0;
    tempAverages = {0, 0, 0};

    Serial.println("**** Initialize DailyDataManager ");



    // Set other dates to -1, or implement as needed
    Serial.println("** initializing dates ");
    for (int i = 0; i < lastDay; ++i) {
      // Calculate the date for the i-th day in the past
      struct tm prevDay;
      memcpy(&prevDay, &timeinfo, sizeof(struct tm)); // Make a copy of timeinfo
      prevDay.tm_mday -= i; // Subtract one day
      mktime(&prevDay); // Normalize the time structure

      dates[i].day = prevDay.tm_mday;  // Day of the month (1-31)
      dates[i].month = prevDay.tm_mon + 1;  // Month (1-12), add 1 because tm_mon is 0-11
      // Serial.println("   i:" + String(i) +  " month:" + String(dates[i].month)+ " day:" + String(dates[i].day) );
    }
 
    // Initialize all elements in detailedValues to -1
    Serial.println("** initializing detailedValues and totalValues");

    for (int i = 0; i < lastDay ; ++i) {  // was lastDay
      totalValues[i].fremdverbrauch = 0;
      totalValues[i].eigenverbrauch = 0;
      totalValues[i].einspeisung = 0;
      Serial.print("Loading detailed values for: ");
      Serial.print(dates[i].month);
      Serial.print("_");
      Serial.println(dates[i].day);
      Serial.println("");

      int lastValidIndex = -1;  // Index of the last valid data entry
      for (int j = 0; j < dailyDataEntries; ++j) {
        PowerData readData =  loadDetailedValues(dates[i].day, dates[i].month, j);
        detailedValues[i][j] = readData;
         Serial.print("   index: ");
         Serial.print(j);
          Serial.print("  ");
         printValuesTableShort(readData);
        if (readData.fremdverbrauch > -1 && readData.eigenverbrauch > -1 &&
            readData.einspeisung > -1) {
            // every index is every 4 min. 
            // in case an index is not stored, the values are interpolated
            float factor = (j - lastValidIndex) * 4.0 / 60.0;
            totalValues[i].fremdverbrauch += readData.fremdverbrauch * factor;
            totalValues[i].eigenverbrauch += readData.eigenverbrauch * factor;
            totalValues[i].einspeisung += readData.einspeisung * factor;
            lastValidIndex = j;
        }
      }
    }

    // Calculate the todayDataIndex based on the time of day
    getLocalTime(&timeinfo);
    int  minutesSinceMidnight = timeinfo.tm_hour * 60 + timeinfo.tm_min;
    Serial.print("Minutes since Midnight ");
    Serial.println(minutesSinceMidnight);
    todayDataIndex = minutesSinceMidnight / 4;  // Calculate todayDataIndex based on 4-minute intervals
    Serial.print("todayDataIndex: ");
    Serial.println(todayDataIndex);


  }

  // Function to retrieve PowerData for a given index and day
  PowerData getPowerDataForIndex(int index, int day) {
    if (index < 0 || index >= dailyDataEntries || day < 0 || day >= lastDay) {
      Serial.println("ERROR - Index out of bounds.");
      return PowerData{-1, -1, -1};  // Return invalid data as error indicator
    }

    return detailedValues[day][index];
  }

  void dailyDataShift() {
    Serial.println("Shifting daily data.");
    Serial.println("Current time: ");
    Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S %Z");
    todayDataIndex = 0;  // Reset the todayDataIndex to 0 for the new day

    deleteLastDayData();

    // Shift all data one day forward, discarding the last day
    for (int i = lastDay - 1; i > 0; --i) {
      dates[i] = dates[i - 1];
      memcpy(detailedValues[i], detailedValues[i - 1],
             dailyDataEntries * sizeof(PowerData));
      totalValues[i] = totalValues[i - 1];
    }

    // Set today's data
    // Correctly assign current day and month
    dates[0].day = timeinfo.tm_mday;  // Day of the month (1-31)
    dates[0].month =
        timeinfo.tm_mon + 1;  // Month (1-12), add 1 because tm_mon is 0-11

    // Initialize detailedValues to -1
    for (int j = 0; j < dailyDataEntries; ++j) {
      detailedValues[0][j] = {-1, -1, -1};

      // Initialize totalValues to 0
      totalValues[0] = {0, 0, 0};  // Resets the first element to zero
    }
    Serial.println("DONE Shifting daily data.");
    Serial.println("Current time: ");
    Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S %Z");
  }

  PowerDataFloat getDailyTotals(int day) { return totalValues[day]; }

  Date getDailyDate(int day) { return dates[day]; }

  void addSample(PowerData sample) {


    // Calculate daily total consumption in Wh
    dailyTotalNowMillis = millis();
    millisSinceLastRun = dailyTotalNowMillis - dailyTotalLastMillis;


    // 50w for one hour make 50WH
    // 50w for 1 min ... makes 1/60 * 50 WH
    // 50w for xxx millisec makes 1 / 60*60*1000
    //(1 hour = 60 minutes * 60 seconds = 3600 seconds)
    Serial.println("");
    printValuesTableShort(sample);
        Serial.print("    Add Sample Seconds since last run: ");
    Serial.println(millisSinceLastRun / 1000.0);
 
    Serial.print("Sample: ");
    Serial.print(sample.fremdverbrauch);
    Serial.print(" (Fremdverbrauch), ");
    Serial.print(sample.eigenverbrauch);
    Serial.print(" (Eigenverbrauch), ");
    Serial.print(sample.einspeisung);
    Serial.println(" (Einspeisung)");

    Serial.print("Millis since last run: ");
    Serial.println(millisSinceLastRun);
    Serial.print("Seconds since last run: ");
    Serial.println(millisSinceLastRun / 1000.0);
    Serial.print("Total Values (Before Calculation): ");
    Serial.print("Fremdverbrauch: ");
    Serial.print(totalValues[0].fremdverbrauch);
    Serial.print(" (Wh), Eigenverbrauch: ");
    Serial.print(totalValues[0].eigenverbrauch);
    Serial.print(" (Wh), Einspeisung: ");
    Serial.print(totalValues[0].einspeisung);
    Serial.println(" (Wh)");

    Serial.print("Adding to Total Values: ");
    Serial.print("Fremdverbrauch: ");
    Serial.print(sample.fremdverbrauch * millisSinceLastRun / (60.0 * 60.0 * 1000.0));
    Serial.print(" (Wh), Eigenverbrauch: ");
    Serial.print(sample.eigenverbrauch * millisSinceLastRun / (60.0 * 60.0 * 1000.0));
    Serial.print(" (Wh), Einspeisung: ");
    Serial.print(sample.einspeisung * millisSinceLastRun / (60.0 * 60.0 * 1000.0));
    Serial.println(" (Wh)");

 

    totalValues[0].fremdverbrauch +=
      sample.fremdverbrauch * millisSinceLastRun / (60.0 * 60.0 * 1000.0);
    totalValues[0].eigenverbrauch +=
      sample.eigenverbrauch * millisSinceLastRun / (60.0 * 60.0 * 1000.0);
    totalValues[0].einspeisung +=
      sample.einspeisung * millisSinceLastRun / (60.0 * 60.0 * 1000.0);
 
    Serial.print("Total Values (After Calculation): ");
    Serial.print("Fremdverbrauch: ");
    Serial.print(totalValues[0].fremdverbrauch);
    Serial.print(" (Wh), Eigenverbrauch: ");
    Serial.print(totalValues[0].eigenverbrauch);
    Serial.print(" (Wh), Einspeisung: ");
    Serial.print(totalValues[0].einspeisung);
    Serial.println(" (Wh)");

    dailyTotalLastMillis = dailyTotalNowMillis;

    // Accumulate sample data
    tempAverages.fremdverbrauch += sample.fremdverbrauch;
    tempAverages.eigenverbrauch += sample.eigenverbrauch;
    tempAverages.einspeisung += sample.einspeisung;
    sampleCount++;


  }

  void processSample(){


      PowerData averages = {tempAverages.fremdverbrauch / sampleCount,
                            tempAverages.eigenverbrauch / sampleCount,
                            tempAverages.einspeisung / sampleCount};
      Serial.print("*PrpocessSample  sampleCount: ");
      Serial.print(sampleCount);
      Serial.print("  - Current time: ");
      Serial.print(&timeinfo, "%H:%M:%S");
      Serial.print("   todayDataIndex: ");
      Serial.print(todayDataIndex);
      Serial.print("   es:");
      Serial.print(averages.einspeisung);
      Serial.print("   ev:");
      Serial.print(averages.eigenverbrauch);
      Serial.print("   fv:");
      Serial.print(averages.fremdverbrauch);

      // Add the averaged data to the correct todayDataIndex based on the time of day
      if (todayDataIndex >= 0 && todayDataIndex < dailyDataEntries) {
        detailedValues[0][todayDataIndex] = averages;     // Use calculated todayDataIndex
        persistDetailedValues(todayDataIndex, averages);  // Save to SPIFFS
      } else {
        Serial.println("Error: Calculated todayDataIndex out of bounds.");
      }
      // Reset for the next set of samples
      tempAverages = {0, 0, 0};
      sampleCount = 0;
      todayDataIndex ++;
    

  }
  /* Print all data for this DailyDataManager - all averaged data, as well as
   * the sample
   */
  void printData() {
    Serial.println("TODAY Data (Averaged):");
    for (int i = 0; i < dailyDataEntries; ++i) {
      if (detailedValues[0][i].fremdverbrauch > -1) {
        Serial.print("todayDataIndex ");
        Serial.print(i);
        Serial.print(": Fremdverbrauch = ");
        Serial.print(detailedValues[0][i].fremdverbrauch);
        Serial.print(", Eigenverbrauch = ");
        Serial.print(detailedValues[0][i].eigenverbrauch);
        Serial.print(", Einspeisung = ");
        Serial.println(detailedValues[0][i].einspeisung);
      }
    }

    Serial.println("Current Samples (Not Averaged):");
    if (sampleCount > 0) {
      // Print the accumulated values for the current samples
      Serial.print("Accumulated Samples (");
      Serial.print(sampleCount);
      Serial.print(")  - Fremdverbrauch: ");
      Serial.print(tempAverages.fremdverbrauch);
      Serial.print(", Eigenverbrauch: ");
      Serial.print(tempAverages.eigenverbrauch);
      Serial.print(", Einspeisung: ");
      Serial.println(tempAverages.einspeisung);
    } else {
      Serial.println("No samples currently accumulated.");
    }
    Serial.print("Daily Total Values:");
    Serial.print("  Fremdverbrauch: ");
    Serial.print(totalValues[0].fremdverbrauch);
    Serial.print("(Wh)  Eigenverbrauch: ");
    Serial.print(totalValues[0].eigenverbrauch);
    Serial.print("(Wh)  Einspeisung: ");
    Serial.print(totalValues[0].einspeisung);
    Serial.println("(Wh)");
  }

 private:
  int todayDataIndex;       // Tracks the current index for daily data
  PowerData tempAverages;   // Temporarily holds sums of samples for averaging
  int sampleCount;          // Counts the samples accumulated towards the next average

  unsigned long dailyTotalNowMillis = millis();  // mills now
  unsigned long dailyTotalLastMillis = millis();  // mills on last run, initilize with now so first calculation is correct
  unsigned long millisSinceLastRun = millis();    // to calculate

  // daily totals in Wh.  (today is 0, yesterday is 1, the day before is 2, ...)
  PowerDataFloat totalValues[lastDay] =
      {};  // All fields in each struct are initialized to zero

  // powerData holds the power data for all days, and all daily data entries
  // is initialized to 0
  PowerData detailedValues[lastDay][dailyDataEntries];

  // save the date (day, month)
  Date dates[lastDay];

void deleteLastDayData() {
    String path = "/dv_" + String(dates[lastDay - 1].month) + "_" + String(dates[lastDay - 1].day);
    File dir = SPIFFS.open(path);
    File file = dir.openNextFile();
    while(file){
        if(file.isDirectory()){
            Serial.println("Is a directory");
        } else {
            String filePath = file.name();
            SPIFFS.remove(filePath);
            Serial.println("Deleted " + filePath);
        }
        file = dir.openNextFile();
    }
}
 
 void persistDetailedValues(int index, PowerData &averages) {
    // Construct the file path string based on the current date and data index
    String fileName = "/dv_" + String(dates[0].month) + "_" + String(dates[0].day) + "/data_" + String(index);
    
    // Attempt to open the file for writing
    File file = SPIFFS.open(fileName, FILE_WRITE);
    
    // Check if the file was successfully opened
    if (!file) {
        Serial.println("Failed to open file for writing");
        Serial.println("File Name: " + fileName); // Output the file name attempted to be opened
        return;
    }

    // Output the file name and the size of the data being written
    Serial.print("   Writing to file: ");
    Serial.println(fileName);
    //Serial.print("writing data: ");
    //printValuesTableShort(averages);

    // Write the data to the file
    size_t bytesWritten = file.write((uint8_t*)&averages, sizeof(PowerData));
    if (bytesWritten == sizeof(PowerData)) {
        //Serial.println("Write successful");
    } else {
        Serial.println("ERROR Write failed");
        Serial.print("Bytes written: ");
        Serial.println(bytesWritten);
    }

    // Close the file
    file.close();
    //Serial.println("File closed");
}

 
PowerData loadDetailedValues(int day, int month, int index) {
  PowerData result = {-1, -1, -1};  // Initialize all fields to -1
  String fileName = "/dv_" + String(month) + "_" + String(day) + "/data_" + String(index);
  File file = SPIFFS.open(fileName);
  if (!file) {
    Serial.println("Failed to open file for reading");
    return result;
  }
  file.read((uint8_t*)&result, sizeof(PowerData));
  file.close();
/*
  // Debugging output
  Serial.print("Loaded detailed values for day: ");
  Serial.print(day);
  Serial.print(", month: ");
  Serial.print(month);
  Serial.print(", index: ");
  Serial.print(index);
  Serial.print(" - ");
  Serial.print("Einspeisung: ");
  Serial.print(result.einspeisung);
  Serial.print(", Eigenverbrauch: ");
  Serial.print(result.eigenverbrauch);
  Serial.print(", Fremdverbrauch: ");
  Serial.println(result.fremdverbrauch);
*/
  return result;
}

  void printValuesTableShort(const PowerData &data) {
    Serial.print("   es:");
    Serial.print(data.einspeisung);
    Serial.print("   ev:");
    Serial.print(data.eigenverbrauch);
    Serial.print("   fv:");
    Serial.println(data.fremdverbrauch);
  }
};

DailyDataManager dailyDataManager;  // holding the data for the day

// Forward declarations of all functions to ensure they are recognized before
// their first use
void setup();
void loop();
void processTouch(uint16_t x, uint16_t y, int touchRight, int touchLeft);
void displayStartScreen(const char *messageLine2);
void updateDisplay(PowerData &data, DailyDataManager &dataManager);
void drawHeader(const String &title);
void drawDailyTotals(DailyDataManager &dataManager, int day);
void drawDailyChart(DailyDataManager &dataManager, int day);
void drawHorizontalHourScale(int x, int y, int startHour, int endHour, int width);
void drawVerticalScale(int x, int y, int height, int maxValue, bool scaleUp,
                       int numTicks);
void drawLabelsAndValues(PowerData &data, DailyDataManager &dataManager);
void drawLeftBarChartWithScale(int value);
void drawRightBarChartWithScale(int Eigenverbrauch, int Fremdverbrauch);
void drawScale(int x, int y, int width, int maxValue, bool isLeftBar,
               int numTicks);
void printLocalTime();
void initializeTime();
bool waitForTimeSync(int timeoutMs);
void connectToWiFi();
void printWifiStatus();
String sendPostRequest(const String &url, const String &payload);
ShellyDataResult retrieveShellyData(String clientID);
void checkAndExecuteDailyReset();
PowerData processPowerData(ShellyDataResult house, ShellyDataResult solar);
void printPowerDataX(const PowerData &data);
void printValuesTable(const PowerData &data);
// Function to update global timeinfo
bool updateTimeinfo();

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("Starting up ...");
  if (!SPIFFS.begin(true)) {
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
}
  Serial.print("SPIFFS Total bytes: ");
  Serial.println(SPIFFS.totalBytes());

  Serial.print("SPIFFS Used bytes: ");
  Serial.println(SPIFFS.usedBytes());

  pinMode(TFTBacklightPWMPin, OUTPUT);
  analogWrite(TFTBacklightPWMPin, 255);  // Set the backlight to full brightness

  Wire.begin(SDA_PIN, SCL_PIN);  // Initialize I2C communication
  lightSensor.begin();  // Initialize the light sensor

  tft.init();
  tft.invertDisplay(0);  // needed for the 3.5 " display
  tft.setRotation(1);    // Landscape mode
  tft.fillScreen(TFT_BLUE);


  // Write to file
  File file = SPIFFS.open("/example4.txt", FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open file for writing");
    return;
  }

  displayStartScreen("");  // display the startup screen
  connectToWiFi();
  initializeTime();
  displayStartScreen("Retrieving data ...");
  Serial.println("------ Starting initialize" );
  dailyDataManager.Initialize();
  Serial.println("------ Done with initialize" );
  displayStartScreen("Setup completed - reading live data ...");
  Serial.println("done with Setup");


}

void loop() {
  uint16_t x = 0, y = 0;  // used for touch screen input
     backlightController.updateBacklight();

  unsigned long currentMillis = millis();
  const unsigned long debounceDelay = 500;  // Debounce delay in milliseconds
  bool updateDisplayNow = false;  // indicates if the display should be updated
  int touchRight = touchRead(T4);
  int touchLeft = touchRead(T5);

  // See if there's any touch data for us
  if (tft.getTouch(&x, &y) || touchRight < 40 || touchLeft < 40) {
    // Check if the debounce delay has passed
    if (currentMillis - lastTouchTime > debounceDelay) {
      processTouch(x, y, touchRight, touchLeft);
      updateDisplayNow = true;
      lastTouchTime = currentMillis;
    }
  }

  // Check for 9 seconds before executePeriodically
  if (currentMillis - previousMillis >= interval30s - 9000 && !before9SecFlag) {
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi not connected. Attempting to connect...");
      connectToWiFi();  // Attempt to connect to WiFi using a globally defined
                        // function
    }
 //   Serial.println("getting 3em data.");

    before9SecFlag = true;
    house.isSuccess = false;
    house = retrieveShellyData(id_3em);
  }

  // Check for 4 seconds before executePeriodically
  if (currentMillis - previousMillis >= interval30s - 4000 && !before4SecFlag) {
 //   Serial.println("getting pm2 data.");

    before4SecFlag = true;
    solar.isSuccess = false;
    solar = retrieveShellyData(id_pm2);
  }

  // Execute periodically at the 30-second mark
  if (currentMillis - previousMillis >= interval30s) {
    previousMillis += interval30s;  // Increase previousMillis by interval (30 sec)
    before4SecFlag = false;          // Reset flags for the next cycle
    before9SecFlag = false;
    updateTimeinfo();
    if (house.isSuccess && solar.isSuccess) {
      // calculate the data for this run
      data = processPowerData(house, solar);
      dailyDataManager.addSample(data);

    //  printLocalTime();
      //dailyDataManager.printData();
      //printValuesTable(data);

      updateDisplayNow = true;  // ensure that display is updated
    }
    else{
      printLocalTime();

      Serial.print(" house.isSuccess: " );
      Serial.println(house.isSuccess);
      Serial.print(" solar.isSuccess: ");
      Serial.print(solar.isSuccess);
    }
 

    // Code to be executed every 4 minutes
    if (currentMillis - lastProcessSample >= intveral4m) {
      lastProcessSample += intveral4m;
      dailyDataManager.processSample();
    }

    checkAndExecuteDailyReset();
  }

  // check if anyone asked the display to be updated, and do so
  if (updateDisplayNow) {
    updateDisplay(data, dailyDataManager);
    updateDisplayNow = false;
  }
}


// Function to update global timeinfo
bool updateTimeinfo() {
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return false;
  }

  return true;
}

void checkAndExecuteDailyReset() {
  // Execute just after midnight
 // Serial.print("Checking for midnight - now : ");
 // Serial.print(timeinfo.tm_hour);
 // Serial.print(":");
  // Serial.println(timeinfo.tm_min);
  if (timeinfo.tm_hour == 0 && timeinfo.tm_min <= 5 && !dailyTaskExecuted) {
  //if (timeinfo.tm_hour == 11 && timeinfo.tm_min == 25 && !dailyTaskExecuted) {

    Serial.println("Executing Daily tasks");
    dailyDataManager.dailyDataShift();  // new function for the above
    Serial.print("nextDisplay: ");
    Serial.println(nextDisplay);
    nextDisplay = -1;  // Set display to NOW, not to worry about updates
    Serial.print("nextDisplay: ");
    Serial.println(nextDisplay);
    dailyTaskExecuted = true;
  } else if (timeinfo.tm_hour > 5) {
    dailyTaskExecuted = false;  // Reset flag for the next midnight
  }
}

// take the data retrieved from the shelly cloud, and do some calculations, to
// determine Eigenverbrauch, Fremdverbrauch, Einspeisung
PowerData processPowerData(ShellyDataResult house, ShellyDataResult solar) {
  PowerData results{0, 0, 0};  // Initialize all fields to 0

  if (house.isSuccess && solar.isSuccess) {
    float Produziert =
        solar.data == 0 ? 0 : -solar.data;  // Making the solar value negative
    float Verbrauch =
        -solar.data + house.data;  // Total consumption calculation

    // Eigenverbrauch - how much of the produced electricity is consumed
    if (house.data <= 0) {
      results.eigenverbrauch = Verbrauch;  // Use house data if it's negative
    } else if (solar.data == 0) {
      results.eigenverbrauch = 0;
    } else {
      results.eigenverbrauch =
          -solar.data;  // Use solar data if house data is not negative
    }
    results.eigenverbrauch = results.eigenverbrauch == -0.0f
                                 ? 0.0f
                                 : results.eigenverbrauch;  // Correct -0 to 0

    results.einspeisung = max(0.0f, Produziert - results.eigenverbrauch);

    // Fremdverbrauch - how much power is consumed that is not self-produced
    results.fremdverbrauch = max(0.0f, house.data);

    // Optionally print the results
    /*
    Serial.print("Produziert: ");
    Serial.println(Produziert);
    Serial.print("Verbrauch: ");
    Serial.println(Verbrauch);
    Serial.print("Eigenverbrauch: ");
    Serial.println(results.eigenverbrauch);
    Serial.print("Einspeisung: ");
    Serial.println(results.einspeisung);
    Serial.print("Fremdverbrauch: ");
    Serial.println(results.fremdverbrauch);
    */
  } else {
    // Handle the case where one or both ShellyDataResults are not successful
    Serial.println("Error: Failed to retrieve data for house or solar.");
  }

  return results;
}

void printPowerDataX(const PowerData &data) {
  Serial.print("Fremdverbrauch (External Consumption): ");
  Serial.println(data.fremdverbrauch);
  Serial.print("Eigenverbrauch (Self-Consumption): ");
  Serial.println(data.eigenverbrauch);
  Serial.print("Einspeisung (Feed-in): ");
  Serial.println(data.einspeisung);
}

void printValuesTable(const PowerData &data) {
  // Calculate spaces needed for alignment
  String eigenverbrauchSpaces =
      String("         ")
          .substring(0, 9 - String(data.eigenverbrauch).length());
  String fremdverbrauchSpaces =
      String("         ")
          .substring(0, 9 - String(data.fremdverbrauch).length());
  String einspeisungSpaces =
      String("         ").substring(0, 9 - String(data.einspeisung).length());

  Serial.println("--------------------------------");
  Serial.println("| Variable        |      Value |");
  Serial.println("--------------------------------");

  int produziert = data.einspeisung + data.eigenverbrauch;
  String produziertSpaces =
      String("         ").substring(0, 9 - String(produziert).length());

  Serial.print("| Produziert      | ");
  Serial.print(produziertSpaces);  // Adjust space based on the number's length
  Serial.print(produziert);
  Serial.println(" W|");

  int verbrauch = data.fremdverbrauch + data.eigenverbrauch;
  String verbrauchSpaces =
      String("         ").substring(0, 9 - String(verbrauch).length());

  Serial.print("| Verbrauch       | ");
  Serial.print(verbrauchSpaces);  // Adjust space based on the number's length
  Serial.print(verbrauch);
  Serial.println(" W|");

  Serial.print("| Eigenverbrauch  | ");
  Serial.print(
      eigenverbrauchSpaces);  // Adjust space based on the number's length
  Serial.print(data.eigenverbrauch);
  Serial.println(" W|");

  Serial.print("| Fremdverbrauch  | ");
  Serial.print(
      fremdverbrauchSpaces);  // Adjust space based on the number's length
  Serial.print(data.fremdverbrauch);
  Serial.println(" W|");

  Serial.print("| Einspeisung     | ");
  Serial.print(einspeisungSpaces);  // Adjust space based on the number's length
  Serial.print(data.einspeisung);
  Serial.println(" W|");

  Serial.println("--------------------------------");
}

/**
 * Prints the current local time formatted as a readable string, adjusted for
 * CET with DST. Requires prior time synchronization.
 */
void printLocalTime() {
  // Set the environment's timezone to Central European Time (CET) with
  // automatic adjustment for DST. This adjustment is necessary if the time was
  // obtained before setting the timezone or if the timezone might change.
  // setenv("TZ", "CET-1CEST,M3.5.0/02:00:00,M10.5.0/03:00:00", 1);
  // tzset();  // Apply the timezone setting to the system
// Get the current local time and store it in timeinfo

  // Print the obtained time formatted as a human-readable string, including
  // timezone information.
  Serial.println("Current time: ");
  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S %Z");
}

/**
 * Initializes device time via NTP for CET with DST adjustment. Requires
 * internet connection and global `ntpServer`.
 */
void initializeTime() {
  // Initialize the NTP client to synchronize the device's time. The first two
  // parameters for configTime are the GMT offset and daylight offset, but since
  // we're using timezone environment settings, both are set to 0.
  configTime(0, 0, ntpServer);
  delay(200);  // Short delay to ensure timezone settings are applied before
               // proceeding.

  // Configure the system timezone for Central European Time (CET) with
  // automatic Daylight Saving Time (DST) adjustment. The timezone string
  // specifies CET as UTC+1 and CEST (DST) as UTC+2, with DST starting on the
  // last Sunday of March at 02:00 and ending on the last Sunday of October at
  // 03:00.
  setenv("TZ", "CET-1CEST,M3.5.0/02:00:00,M10.5.0/03:00:00", 1);
  tzset();  // Apply the timezone settings.
  delay(200);  // Short delay to ensure timezone settings are applied before
               // proceeding.

  // Wait for NTP time synchronization to complete, or restart
  long start = millis();
  Serial.print("synchronizing time");
  while (!getLocalTime(&timeinfo)) {
    Serial.print(".");
    delay(100);
    if (millis() - start > 20000) {  // Timeout set for 20 seconds
      Serial.println("Failed to synchronize NTP time.");
      displayStartScreen("ERROR Time synchronization failed");
      delay(5000);
      displayStartScreen("Restarting ...");
      delay(1000);
      ESP.restart();
    }
  }

  Serial.println("  : synchronized successfully.");

  // Get the current date and time
  char dateTime[45];
  snprintf(dateTime, sizeof(dateTime), "Time Synchronized : %02d-%02d-%04d %02d:%02d:%02d", timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

  // Call displayStartScreen with the current date and time
  displayStartScreen(dateTime);
  printLocalTime();
}

/**
 * Attempts to connect to WiFi using predefined `ssid` and `password`. Retries
 * up to 30 times with a 1-second pause between attempts. Outputs connection
 * status to Serial. On failure, advises checking network settings.
 */
void connectToWiFi() {
  Serial.print("Connecting to WiFi network: ");
  Serial.println(ssid);
  displayStartScreen("Connecting to WiFi ... ");

  WiFi.begin(ssid, password);
  int attemptCount = 0;

  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
    attemptCount++;
    // After every 10 attempts, print more detailed status information
    if (attemptCount % 10 == 0) {
      printWifiStatus();
    }
    if (attemptCount > 48){
    Serial.println("Failed to connect to WiFi.");
    displayStartScreen("ERROR connecting to WiFi");
    delay(5000);
        Serial.println("Restarting ...");

    displayStartScreen("Restarting ...");
    delay(1000);
    ESP.restart();

  }
  }

  Serial.println("");
  Serial.println("WiFi connected successfully.");
  Serial.print("Assigned IP Address: ");
  Serial.println(WiFi.localIP());
  String message = "wifi connected IP Address: " + WiFi.localIP().toString();
  displayStartScreen(message.c_str());  // Convert to const char* and pass to
                                        // Print the strength of the signal



}
void printWifiStatus() {
  // Print the SSID of the network you're trying to connect to
  Serial.println("");
  Serial.print("Attempting to connect to SSID: ");
  Serial.println(ssid);

  // Print the strength of the signal
  long rssi = WiFi.RSSI();
  Serial.print("Signal strength (RSSI):");
  Serial.print(rssi);
  Serial.println(" dBm");

  // Print the WiFi status
  switch (WiFi.status()) {
    case WL_NO_SHIELD:
      Serial.println("WiFi shield not present");
      break;
    case WL_IDLE_STATUS:
      Serial.println("WiFi idle");
      break;
    case WL_NO_SSID_AVAIL:
      Serial.println("No SSID available. Check SSID name and signal strength.");
      break;
    case WL_SCAN_COMPLETED:
      Serial.println("Scan completed");
      break;
    case WL_CONNECTED:
      Serial.println("Connected.");
      break;
    case WL_CONNECT_FAILED:
      Serial.println("Connection failed. Check credentials.");
      break;
    case WL_CONNECTION_LOST:
      Serial.println("Connection lost. Attempting to reconnect...");
      break;
    case WL_DISCONNECTED:
      Serial.println(
          "Disconnected. Check if WiFi is disabled or out of range.");
      break;
    default:
      Serial.println("Unknown status.");
      break;
  }

  // If we're not connected, attempt to reconnect
 // if (WiFi.status() != WL_CONNECTED) {
 //   Serial.println("Attempting to reconnect to WiFi network...");
 //   WiFi.reconnect();
 // }
}

/**
 * Attempts to send an HTTP POST request to a specified URL with a given
 * payload. If the device is not connected to WiFi, it tries to establish a
 * connection using predefined WiFi credentials. It uses the HTTPClient library
 * for the ESP32 platform.
 *
 * @param url The URL to which the POST request is sent. This should be a fully
 * qualified URL including the HTTP or HTTPS protocol.
 * @param payload The data to be sent in the POST request, formatted as a
 * string.
 * @return A string containing the server's response payload if the request is
 * successful. Returns an empty string if the WiFi connection cannot be
 * established or if the HTTP request fails.
 *
 * Note: The function prints status messages and errors to the Serial monitor to
 * aid in debugging. It assumes WiFi credentials are defined globally or within
 * the function that attempts to connect to WiFi.
 */

String sendPostRequest(const String &url, const String &payload) {
  HTTPClient http;
  http.begin(url);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  int httpCode = http.POST(payload);
  String responsePayload = "";

  if (httpCode > 0) {
    responsePayload = http.getString();  // Get the request response payload
  } else {
    // Serial.print("Error on HTTP request, response code: ");
    // Serial.println(httpCode);
    Serial.printf("HTTP Request failed, error: %s\n",
                  http.errorToString(httpCode).c_str());
  }
  http.end();
  return responsePayload;
}

/**
 * Retrieves power consumption or generation data for a specified Shelly device
 * from the Shelly Cloud API.
 *
 * This function sends an HTTP POST request to the Shelly Cloud API, using the
 * provided clientID to identify the device (either a Shelly 3EM for whole-house
 * consumption or a Shelly PM for solar panel output). It then parses the JSON
 * response to extract the relevant power data. The function handles both
 * successful data retrieval and errors, such as connection issues or JSON
 * parsing failures.
 *
 * @param clientID The client ID of the Shelly device from which to retrieve
 * data. This ID is used in the POST request payload to specify the target
 * device. The function supports different client IDs for different types of
 * devices (e.g., 3EM or PM).
 * @return ShellyDataResult A struct containing the retrieved data (if
 * successful) and a success flag. The data field contains the power value in
 * watts. The isSuccess flag indicates whether the data was successfully
 * retrieved and parsed.
 *
 * Note: The function requires the `auth_key` global variable to be set with a
 * valid Shelly Cloud API authentication key. The URL for the Shelly Cloud API
 * is hardcoded within the function.
 */

ShellyDataResult retrieveShellyData(String clientID) {
  ShellyDataResult result;
  for (int attempt = 0; attempt < 2; attempt++) {  // Allow up to 2 attempts
    String postData = "id=" + clientID + "&auth_key=" + auth_key;
    String responsePayload = sendPostRequest(url, postData);

    if (!responsePayload.isEmpty()) {
      StaticJsonDocument<2048> doc;
      DeserializationError error = deserializeJson(doc, responsePayload);

      if (!error) {  // No error in deserialization, process the data
        // Extract and assign data based on clientID
        if (clientID == id_3em) {
          result.data = doc["data"]["device_status"]["total_power"].as<float>();
        } else if (clientID == id_pm2) {
          result.data =
              doc["data"]["device_status"]["switch:0"]["apower"].as<float>();
        }
        result.isSuccess = true;  // Indicate success
        return result;            // Exit the function on success
      }
    }

    // If reached here, it means the attempt failed
    result.isSuccess = false;

    if (attempt ==
        0) {  // If it was the first attempt, wait for 1 second before retrying
      delay(1000);  // 1 second pause
    }
  }
  // Return the result (failed) after two attempts or if the loop completes
  return result;
}

////////////////////

void processTouch(uint16_t x, uint16_t y, int touchRight, int touchLeft) {
 /* Serial.print("got Touch data: ");
  Serial.print(x);
  Serial.print(" ");
  Serial.println(y);
  Serial.print("TouchRight");
  Serial.println(touchRight);
  Serial.print("TouchLeft");
  Serial.println(touchLeft);
  */
  if ((x > (320 / 2)) || touchRight < 40) {  // right side of display pressed
    nextDisplay++;                           // Move to the next day or to "Now"
    if (nextDisplay >= lastDay) {
      nextDisplay = -1;  // Wrap around to "Now"
    }
  } else if ((x > 0 && x < 320 / 2) || touchLeft < 40) {
    // left side of display pressed
    nextDisplay--;  // Move to the previous day
    if (nextDisplay < -1) {
      nextDisplay = lastDay - 1;  // Wrap around to the latest day in the past
    }
  }
 // Serial.print("Changed display to :");
 // Serial.println(nextDisplay);
}

void displayStartScreen(const char *messageNewLine) {
  static String messageLine2 = "";
  messageLine2 += "\n";
  messageLine2 += messageNewLine;
  static int startDisplayCounter = 0;

  if (startDisplayCounter == 0){
    tft.fillScreen(TFT_DARKBLUE);
    tft.fillRoundRect(10, 10, 460, 300, 8, TFT_BLACK);


    tft.setTextColor(TFT_WHITE, TFT_BLACK);  //  text color black on white
    tft.setTextSize(1);
    tft.setTextFont(4);
    tft.setCursor(40, 40);
    String messageLine1 = "System Starting...";
    tft.print(messageLine1);
    startDisplayCounter ++;
  }
 

  spr.setColorDepth(8);
  spr.createSprite(320, 200);
  spr.fillSprite(TFT_BLACK);
  spr.setTextColor(TFT_WHITE, TFT_BLACK);  //  text color black on white
  spr.setTextSize(1);
  spr.setTextFont(2);
  spr.setCursor(0, 0);
  spr.print(messageLine2);
  spr.pushSprite(40, 80);
  spr.deleteSprite();

 
}
void updateDisplay(PowerData &data, DailyDataManager &dataManager) {
  const long refreshNOW = 30 * 1000;        // Refresh NOW every 30 seconds
  const long refreshToday = 4 * 60 * 1000;  // Refresh Today every 4 minutes
  const long refreshYesterday = 60 * 60 * 1000;  // Refresh Yesterday every hour

  unsigned long currentMillis = millis();
  static unsigned long lastDisplayUpdate =
      0;  // Track the last update time for any display mode

  bool updateNow = (currentDisplay !=
                    nextDisplay);  // Determine if a display change occurred

  if (updateNow) {
    tft.fillScreen(TFT_BLACK);     // Clear the entire screen for display change
    currentDisplay = nextDisplay;  // Update the current display mode
  }

  // Determine the refresh interval based on the current display mode
  long refreshInterval = refreshNOW;  // Default to NOW refresh interval
  if (currentDisplay == 0) {
    refreshInterval = refreshToday;
  } else if (currentDisplay > 0) {
    refreshInterval = refreshYesterday;
  }

  // Update the display if there's been a change or the refresh interval has
  // elapsed
  if (updateNow || currentMillis - lastDisplayUpdate > refreshInterval) {
    if (currentDisplay == -1) {  // Displaying NOW
      // Clear only the necessary parts for updating values
      tft.fillRect(230, 70, 270, 120, TFT_BLACK);  // Clear previous values area
      tft.fillRect(0, 220, 480, 30,
                   TFT_BLACK);  // Clear previous bar charts area

      char timeStr[6];
      strftime(timeStr, sizeof(timeStr), "%H:%M", &timeinfo);

      drawHeader("Jetzt   (" + String(timeStr) + ")");
      drawLabelsAndValues(data, dataManager);
      drawLeftBarChartWithScale(data.einspeisung);
      drawRightBarChartWithScale(data.eigenverbrauch, data.fremdverbrauch);
    } else {  // Display a full day (0 is today, 1, 2, is yesterday and before)

      if (currentDisplay == 0) {
        drawHeader("Heute");
      } else if (currentDisplay == 1) {
        drawHeader("Gestern");
      } else {
        Date thisDate = dataManager.getDailyDate(currentDisplay);
        std::string dayStr = (thisDate.day < 10) ? "0" + std::to_string(thisDate.day) : std::to_string(thisDate.day);
        std::string monthStr = (thisDate.month < 10) ? "0" + std::to_string(thisDate.month) : std::to_string(thisDate.month);
        std::string header = dayStr + "." + monthStr + ".";
        drawHeader(header.c_str());
      }
      drawDailyChart(dataManager, currentDisplay);
      drawDailyTotals(dataManager, currentDisplay);
    }
    lastDisplayUpdate = currentMillis;  // Record the time of this update
  }
}

void drawHeader(const String &title) {
  int screenWidth = tft.width();  // Get the width of the screen
  int headerHeight = 28;          // Set the height for the header
  int arrowWidth = 20;            // Width of the arrows
  int arrowMargin = 5;                 // Margin from the screen edges
  int arrowHeight = headerHeight / 2;  // Height of the arrows

  int headerY = 0;                // Y position of the header's top-left corner

 // Draw arrows on the left and right sides of the header
  int arrowY = headerY + (headerHeight - arrowHeight) / 2;  // Vertical position


 
  tft.fillRoundRect(arrowWidth+20,0,screenWidth - (2*(arrowWidth +20)),headerHeight,headerHeight/2,TFT_DARKBLUE);

  // Left arrow (pointing left)
  tft.fillTriangle(arrowMargin, arrowY + (arrowHeight / 2),
                   arrowMargin + arrowWidth, arrowY, arrowMargin + arrowWidth,
                   arrowY + arrowHeight, TFT_WHITE);

  // Right arrow (pointing right)
  tft.fillTriangle(screenWidth - arrowMargin - arrowWidth, arrowY,
                   screenWidth - arrowMargin, arrowY + (arrowHeight / 2),
                   screenWidth - arrowMargin - arrowWidth, arrowY + arrowHeight,
                   TFT_WHITE);
 

  // Calculate the width of the text to center it
  int textWidth = tft.textWidth(title);
  int textX = (screenWidth - textWidth) / 2;  // Center the text horizontally
  int textY = (headerHeight / 2) - 12;        // Adjust text vertical position
  tft.setTextFont(4);
  tft.setTextSize(1); 
  tft.setTextColor(TFT_WHITE, TFT_DARKBLUE);
  tft.drawString(title, textX, headerY + textY);

 
}

void drawDailyTotals(DailyDataManager &dataManager, int day) {
  //Serial.println("**drawDailyTotals");
  PowerDataFloat data = dataManager.getDailyTotals(day);

  float GV = ((data.eigenverbrauch + data.fremdverbrauch) / 1000.0f);
  float ES = (data.einspeisung / 1000.0f);
  float EV = (data.eigenverbrauch / 1000.0f);

  String gvtxt = "GV:";
  gvtxt += round(GV * 100) / 100.0;
  gvtxt += "kWh";
  String estxt = "ES:";
  estxt += round(ES * 100) / 100.0;
  estxt += "kWh";
  String evtxt = "EV:";
  evtxt += round(EV * 100) / 100.0;
  evtxt += "kWh";
  // TODO position a label and print the Value
 
  tft.setTextSize(2);
  tft.setTextFont(1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(45, 60);  // Set the cursor position to (x, y)
  tft.print(gvtxt);
  tft.setCursor(45+140, 60);
  tft.print(estxt);
  tft.setCursor(45+140+140, 60);
  tft.print(evtxt);
}

void drawDailyChart(DailyDataManager &dataManager, int day) {
 // Serial.println("*drawDailyChart");
 // Serial.print("day: ");
 // Serial.println(day);
  int screenWidth = tft.width();
  int screenHeight = tft.height();
  int headerSpace = 50;
  int chartWidth = screenWidth - 20;   // Width of the chart area     // was 300
  int chartHeight = screenHeight- headerSpace;  // Maximum height for the bars //was 200
  int chartUpperPart = chartHeight /4 *3; // the upper part is 3/4 of the chart
  int chartLowerPart = chartHeight /4 -1; // the lower part is 1/4 of the chart adjusted for 1px down
  int startX = 30;        // Starting X position for the chart  // was 40
  int startY = headerSpace+ chartUpperPart;  // Starting Y position for the chart (bottom of the chart)  // was 190
  int xPos = startX;

  drawVerticalScale(startX - 5, startY - chartUpperPart, chartUpperPart, 1500, true, 4);
  drawVerticalScale(startX - 5, startY, chartLowerPart, 400, false, 2);

  // Loop through the data points and draw the bars
  int scaleWidth = 0;
  // display starting 7am to 23  - 15 hours - 225 *2 columns = 450
  int startHour = 7;  // Scale starts at 5 AM
  int endHour = 22;   // Scale ends at 11 PM
  int dataEntriesPerHour = 15;
  int barWith = 2;
  // Iterate over the data entries, there are 15 data entries per hour
  for (int i = startHour * dataEntriesPerHour; i < endHour * dataEntriesPerHour;
       i++) {
    scaleWidth += barWith;
    // 1200 - 150px ; 400 - 50px  -> y scale of 0.125
    float yScale = 0.125;  // Scale for the data values

    PowerData data = dataManager.getPowerDataForIndex(i, day);

    // Now, you can use `data` as you wish
    int einspeisungHeight = data.einspeisung * yScale;
    int eigenverbrauchHeight = data.eigenverbrauch * yScale;
    int fremdverbrauchHeight = data.fremdverbrauch * yScale;

    // X position for this bar, scaled to fit within chartWidth
    // int xPos = startX + (chartWidth / 314) * (i - 16);
    xPos += barWith;
    // Draw einspeisung (blue bar going down from the startY)
    tft.fillRect(xPos, startY, barWith, einspeisungHeight, TFT_BLUE);

    // Draw eigenverbrauch (green bar going up from the startY)
    tft.fillRect(xPos, startY - eigenverbrauchHeight, barWith, eigenverbrauchHeight,
                 TFT_GREEN);

    // Draw fremdverbrauch stacked on eigenverbrauch (red bar on top of the
    // green)
    tft.fillRect(xPos, startY - eigenverbrauchHeight - fremdverbrauchHeight, barWith,
                 fremdverbrauchHeight, TFT_RED);
  }
  drawHorizontalHourScale(startX, startY, startHour, endHour, scaleWidth);
}

void drawHorizontalHourScale(int x, int y, int startHour, int endHour, int width) {
  int tickLength = 5;
  int labelInterval = 4;  // Place labels every ? hours starting at ? AM
  int numHours = endHour - startHour;  // Total number of hours to display

  tft.setTextSize(1);
  tft.setTextFont(1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  for (int i = 0; i <= numHours; i++) {
    int currentHour = startHour + i;
    // Calculate tick position
    int tickPosition = x + (i * (width / numHours));

    // Draw vertical tick
    tft.drawLine(tickPosition, y, tickPosition, y + tickLength, TFT_WHITE);

    // Check if it's time to place a label (every 6 hours starting at 8 AM)
    if (currentHour % labelInterval ==
        0) {  // 8 AM 
      String label = String(currentHour) + "h";
      int labelWidth = tft.textWidth(label);
      int labelX =
          tickPosition - (labelWidth / 2);  // Center the label below the tick
      int labelY = y + tickLength + 2;      // Place label below the tick

      // Draw the label
      tft.setCursor(labelX, labelY);
      tft.print(label);
    }
  }
}

void drawVerticalScale(int x, int y, int height, int maxValue, bool scaleUp,
                       int numTicks) {
  int tickLength = 8;
  tft.setTextSize(1);
  tft.setTextFont(1);

  for (int i = 0; i < numTicks; i++) {
    int labelValue = (maxValue / (numTicks - 1)) * i;

    // Calculate tick position based on whether the scale goes up or down
    int tickPosition = scaleUp ? y + height - (i * (height / (numTicks - 1)))
                               : y + (i * (height / (numTicks - 1)));

    // Draw vertical ticks to the left side
    tft.drawLine(x, tickPosition, x - tickLength, tickPosition, TFT_WHITE);

    // Adjust label positioning to be above the tick mark
    String label = String(labelValue)+"W";
    int labelWidth = tft.textWidth(label);
    int labelX = x - tickLength - labelWidth +
                 12;  // Shift label to the left of the tick mark
    int labelY =
        tickPosition - 9;  // Adjust Y position to place label above the tick

    // Set cursor for labels above the ticks and adjust positioning as needed.
    tft.setCursor(labelX, labelY);
    tft.setTextColor(TFT_WHITE);
    tft.print(label);
  }
}

void drawLabelsAndValues(PowerData &data, DailyDataManager &dataManager) {
  int gesamtverbrauch = data.eigenverbrauch + data.fremdverbrauch;
  int produziert = data.einspeisung + data.eigenverbrauch;
  int eigenverbrauch = data.eigenverbrauch;

  int startXLeft = 20;
  int startY = 40;
  int areaWidth = 440;  // Define the total width available for each label and value

  // Set text color and size
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(1);
  tft.setTextFont(4);
  // Helper function to print labels left-aligned and values right-aligned
  auto printLabelAndValue = [&](const String &label, float value, float dailyTotal, int yPos) {
    // Calculate the width of the value strings
    String valueStr = String(value, 0) + " W   ";
    tft.setTextColor(TFT_WHITE, TFT_BLACK, true);

    String dailyTotalStr = String(dailyTotal / 1000.0, 2) + " kWh";
    int valueWidth = tft.textWidth(valueStr);
    int dailyTotalWidth = tft.textWidth(dailyTotalStr);

    // Print the label
    tft.setCursor(startXLeft, yPos);
    tft.print(label);

    // Print the value, right-aligned
    tft.setCursor(startXLeft + areaWidth - valueWidth - dailyTotalWidth - 10, yPos);
    tft.print(valueStr);

    // Print the daily total, right-aligned
    tft.setCursor(startXLeft + areaWidth - dailyTotalWidth, yPos);
    tft.print(dailyTotalStr);
  };

  // Helper function to print the header line
  auto printHeaderLine = [&]() {
    // Set text color and size

    int valueWidth = tft.textWidth("jetzt ");
    int dailyTotalWidth = tft.textWidth("heute    ");

    tft.setTextColor(TFT_WHITE, TFT_BLACK, true);
    tft.setTextSize(1);
    tft.setTextFont(4);
    // Print the header labels
    tft.setCursor(startXLeft, startY);
    tft.print("");
    tft.setCursor(startXLeft + areaWidth - 2 * valueWidth - dailyTotalWidth - 10, startY);
    tft.print("jetzt ");
    tft.setCursor(startXLeft + areaWidth - dailyTotalWidth, startY);
    tft.print("heute    ");

    // Draw a horizontal line below the header
    tft.drawLine(startXLeft, startY + 25, startXLeft + areaWidth, startY + 25, TFT_WHITE);
  };

  // Print the header line
  printHeaderLine();

  PowerDataFloat dailyTotals = dataManager.getDailyTotals(0);
  int dailyTotalProduziert = dailyTotals.einspeisung + dailyTotals.eigenverbrauch;
  int dailyTotalEigenverbrauch = dailyTotals.eigenverbrauch;
  int dailyTotalGesamtverbrauch = dailyTotals.eigenverbrauch + dailyTotals.fremdverbrauch;

  // Printing labels and values
  printLabelAndValue("Produziert:      ", produziert, dailyTotalProduziert , startY+40);
  printLabelAndValue("Eigenverbrauch:  ", eigenverbrauch, dailyTotalEigenverbrauch , startY + 70);
  printLabelAndValue("Gesamtverbrauch: ", gesamtverbrauch, dailyTotalGesamtverbrauch , startY + 100);

 
}

void drawLeftBarChartWithScale(int value) {
  int width = 80;
  int x = 50;   // start bar
  int y = 220;  // strart bar
  // write the lable above the chart
  tft.setCursor(x, y - 25);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(1);
  tft.setTextFont(4);
  tft.print("Einsp.");
  tft.drawLine(x + width - 1, y - 25, x + width, y + 25,
               TFT_WHITE);  // draw the line right of Einsp.

  // drawing the chart
  int maxValue = 400;
  int barHeight = 30;
  int fillWidth = map(value, 0, maxValue, 0, width);
  tft.fillRect(x + (width - fillWidth), y, fillWidth, barHeight, TFT_BLUE);
  tft.drawRect(x, y, width, barHeight, TFT_WHITE);  // Draw border
  drawScale(x, y + barHeight + 5, width, maxValue, true,
            3);  // Draw scale below the bar
}

void drawRightBarChartWithScale(int Eigenverbrauch, int Fremdverbrauch) {
  int maxValue = 1500;  // Adjust if your maximum value differs
  int barHeight = 30;
  int width = 300;
  int x = 129;   // startbar  barStartX + barWidthLeft-1
  int y = 220;  // starbar

  tft.setCursor(x + 10, y - 25);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(1);
  tft.setTextFont(4);
  tft.print("Verbrauch");

  int eigenWidth = map(Eigenverbrauch, 0, maxValue, 0, width);
  int fremdWidth = map(Fremdverbrauch, 0, maxValue, 0, width);

  // Draw Eigenverbrauch
  tft.fillRect(x, y, eigenWidth, barHeight, TFT_GREEN);

  // Draw Fremdverbrauch next to Eigenverbrauch
  tft.fillRect(x + eigenWidth, y, fremdWidth, barHeight, TFT_RED);

  // Draw border around the entire bar
  tft.drawRect(x, y, width, barHeight, TFT_WHITE);

  drawScale(x, y + barHeight + 5, width, maxValue, false, 7);  // Draw scale below the bar
}

void drawScale(int x, int y, int width, int maxValue, bool isLeftBar,
               int numTicks) {
  int tickLength = 5;
  tft.setTextSize(1);
  tft.setTextFont(1);
  for (int i = 0; i < numTicks; i++) {
    int labelValue = (maxValue / (numTicks - 1)) * i;
    std::string numStr = std::to_string(labelValue);
    int labelLength = numStr.length();

    int tickPosition = isLeftBar ? x + width - (i * (width / (numTicks - 1)))
                                 : x + (i * (width / (numTicks - 1)));

    //
    tft.drawLine(tickPosition, y, tickPosition, y + tickLength, TFT_WHITE);

    tft.setCursor(tickPosition + 1 - (3 * labelLength), y + tickLength + 2);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(1);
    tft.print(labelValue);
  }
}
