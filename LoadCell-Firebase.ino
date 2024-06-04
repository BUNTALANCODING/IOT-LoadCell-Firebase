#include <HX711_ADC.h>
#include <WiFi.h>
#include <FirebaseESP8266.h>
#include <EEPROM.h>

// Pins
const int HX711_dout = D2; // mcu > HX711 dout pin
const int HX711_sck = D1;  // mcu > HX711 sck pin

// HX711 constructor
HX711_ADC LoadCell(HX711_dout, HX711_sck);

// WiFi credentials
const char* ssid = "PISANGIN";
const char* password = "12345678";

// Firebase project credentials
#define FIREBASE_HOST "tugasakhirapp-c5669-default-rtdb.asia-southeast1.firebasedatabase.app"
#define FIREBASE_AUTH "BDe9Za0dXZaoPrSkRXbAGmYFPrgsFSEVuG7swxkp"

// Firebase objects
FirebaseData firebaseData;
FirebaseConfig config;
FirebaseAuth auth;

// EEPROM address for calibration value
const int calVal_eepromAdress = 0;

void setup() {
  Serial.begin(115200);
  delay(10);
  
  // Connect to WiFi
  Serial.println("Connecting to WiFi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  
  // Initialize Firebase
  config.host = FIREBASE_HOST;
  config.signer.tokens.legacy_token = FIREBASE_AUTH;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  // Initialize EEPROM
  EEPROM.begin(512);
  
  // Initialize LoadCell
  LoadCell.begin();
  unsigned long stabilizingtime = 2000;
  boolean _tare = true;
  LoadCell.start(stabilizingtime, _tare);
  if (LoadCell.getTareTimeoutFlag()) {
    Serial.println("Tare timeout, check wiring and pin configuration");
  } else {
    float calValue = 1.0; // Default calibration value
    EEPROM.get(calVal_eepromAdress, calValue);
    Serial.print("Loaded calibration value from EEPROM: ");
    Serial.println(calValue);
    LoadCell.setCalFactor(calValue); // Set calibration value
  }
}

void loop() {
  static boolean newDataReady = false;
  const int serialPrintInterval = 0; // increase value to slow down serial print activity
  const float threshold = 10.0; // Threshold value to determine if there's a load or not
  
  // Check for new data/start next conversion
  if (LoadCell.update()) newDataReady = true;

  // Get smoothed value from the dataset
  if (newDataReady) {
    if (millis() > serialPrintInterval) {
      float weight = LoadCell.getData();
      Serial.print("Load_cell output val: ");
      Serial.println(weight);
      
      if (weight < threshold) {
        weight = 0; // Set weight to 0 if it's below the threshold
      }
      
      newDataReady = false;
      sendToFirebase(weight); // Send data to Firebase
    }
  }

  // Receive command from serial terminal
  if (Serial.available() > 0) {
    char inByte = Serial.read();
    if (inByte == 't') LoadCell.tareNoDelay(); // Tare
    else if (inByte == 'r') calibrate(); // Calibrate
    else if (inByte == 'c') changeSavedCalFactor(); // Edit calibration value manually
  }

  // Check if last tare operation is complete
  if (LoadCell.getTareStatus()) {
    Serial.println("Tare complete");
  }
}

void sendToFirebase(float data) {
  if (data >= 0) { // Memeriksa apakah nilai data tidak negatif
    if (Firebase.setFloat(firebaseData, "/weight", data)) {
      Serial.println("Data sent to Firebase successfully!");
    } else {
      Serial.println("Failed to send data to Firebase.");
      Serial.println("REASON: " + firebaseData.errorReason());
    }
  } else {
    Serial.println("Nilai data negatif, tidak dikirim ke Firebase.");
  }
}

void calibrate() {
  Serial.println("***");
  Serial.println("Start calibration:");
  Serial.println("Place the load cell on a level stable surface.");
  Serial.println("Remove any load applied to the load cell.");
  Serial.println("Send 't' from serial monitor to set the tare offset.");

  boolean _resume = false;
  while (_resume == false) {
    LoadCell.update();
    if (Serial.available() > 0) {
      if (Serial.available() > 0) {
        char inByte = Serial.read();
        if (inByte == 't') LoadCell.tareNoDelay();
      }
    }
    if (LoadCell.getTareStatus() == true) {
      Serial.println("Tare complete");
      _resume = true;
    }
  }

  Serial.println("Now, place your known mass on the loadcell.");
  Serial.println("Then send the weight of this mass (i.e. 100.0) from serial monitor.");

  float known_mass = 0;
  _resume = false;
  while (_resume == false) {
    LoadCell.update();
    if (Serial.available() > 0) {
      known_mass = Serial.parseFloat();
      if (known_mass != 0) {
        Serial.print("Known mass is: ");
        Serial.println(known_mass);
        _resume = true;
      }
    }
  }

  LoadCell.refreshDataSet(); // Refresh the dataset to be sure that the known mass is measured correctly
  float newCalibrationValue = LoadCell.getNewCalibration(known_mass); // Get the new calibration value

  Serial.print("New calibration value has been set to: ");
  Serial.print(newCalibrationValue);
  Serial.println(", use this as calibration value (calFactor) in your project sketch.");

  // Save the new calibration value to EEPROM
  EEPROM.put(calVal_eepromAdress, newCalibrationValue);
#if defined(ESP8266) || defined(ESP32)
  EEPROM.commit();
#endif
  Serial.println("Calibration value saved to EEPROM.");

  Serial.println("End calibration");
  Serial.println("***");
  Serial.println("To re-calibrate, send 'r' from serial monitor.");
  Serial.println("For manual edit of the calibration value, send 'c' from serial monitor.");
  Serial.println("***");
}

void changeSavedCalFactor() {
  float oldCalibrationValue = LoadCell.getCalFactor();
  boolean _resume = false;
  Serial.println("***");
  Serial.print("Current value is: ");
  Serial.println(oldCalibrationValue);
  Serial.println("Now, send the new value from serial monitor, i.e. 696.0");
  float newCalibrationValue;
  while (_resume == false) {
    if (Serial.available() > 0) {
      newCalibrationValue = Serial.parseFloat();
      if (newCalibrationValue != 0) {
        Serial.print("New calibration value is: ");
        Serial.println(newCalibrationValue);
        LoadCell.setCalFactor(newCalibrationValue);
        
        // Save the new calibration value to EEPROM
        EEPROM.put(calVal_eepromAdress, newCalibrationValue);
#if defined(ESP8266) || defined(ESP32)
        EEPROM.commit();
#endif
        
        _resume = true;
      }
    }
  }
  Serial.println("End change calibration value");
  Serial.println("***");
}
