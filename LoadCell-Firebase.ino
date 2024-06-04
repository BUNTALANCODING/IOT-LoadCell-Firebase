#include <HX711_ADC.h>

#include <ESP8266WiFi.h>

#include <FirebaseESP8266.h>

#include <EEPROM.h>

const int HX711_dout = D2;
const int HX711_sck = D1;

HX711_ADC LoadCell(HX711_dout, HX711_sck);

const char * ssid = "PISANGIN";
const char * password = "12345678";

#define FIREBASE_HOST "tugasakhirapp-c5669-default-rtdb.asia-southeast1.firebasedatabase.app"
#define FIREBASE_AUTH "BDe9Za0dXZaoPrSkRXbAGmYFPrgsFSEVuG7swxkp"

FirebaseData firebaseData;
FirebaseConfig config;
FirebaseAuth auth;

const int calVal_eepromAdress = 0;

void setup() {
    Serial.begin(115200);
    delay(10);

    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
    }

    config.host = FIREBASE_HOST;
    config.signer.tokens.legacy_token = FIREBASE_AUTH;
    Firebase.begin( & config, & auth);
    Firebase.reconnectWiFi(true);

    EEPROM.begin(512);

    LoadCell.begin();
    unsigned long stabilizingtime = 2000;
    boolean _tare = true;
    LoadCell.start(stabilizingtime, _tare);
    if (LoadCell.getTareTimeoutFlag()) {
        Serial.println("Tare timeout, check wiring and pin configuration");
    } else {
        float calValue = 1.0;
        EEPROM.get(calVal_eepromAdress, calValue);
        LoadCell.setCalFactor(calValue);
    }
}

void loop() {
    static boolean newDataReady = false;
    const int serialPrintInterval = 0;
    const float threshold = 10.0;

    if (LoadCell.update()) newDataReady = true;

    if (newDataReady) {
        if (millis() > serialPrintInterval) {
            float weight = LoadCell.getData();
            if (weight < threshold) {
                weight = 0;
            }
            newDataReady = false;
            sendToFirebase(weight);
        }
    }

    if (Serial.available() > 0) {
        char inByte = Serial.read();
        if (inByte == 't') LoadCell.tareNoDelay();
        else if (inByte == 'r') calibrate();
        else if (inByte == 'c') changeSavedCalFactor();
    }

    if (LoadCell.getTareStatus()) {
        Serial.println("Tare complete");
    }
}

void sendToFirebase(float data) {
    if (data >= 0) {
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

    float known_mass = 0;
    _resume = false;
    while (_resume == false) {
        LoadCell.update();
        if (Serial.available() > 0) {
            known_mass = Serial.parseFloat();
            if (known_mass != 0) {
                _resume = true;
            }
        }
    }

    LoadCell.refreshDataSet();
    float newCalibrationValue = LoadCell.getNewCalibration(known_mass);

    EEPROM.put(calVal_eepromAdress, newCalibrationValue);
    #if defined(ESP8266) || defined(ESP32)
    EEPROM.commit();
    #endif
}

void changeSavedCalFactor() {
    boolean _resume = false;
    float newCalibrationValue;
    while (_resume == false) {
        if (Serial.available() > 0) {
            newCalibrationValue = Serial.parseFloat();
            if (newCalibrationValue != 0) {
                LoadCell.setCalFactor(newCalibrationValue);

                EEPROM.put(calVal_eepromAdress, newCalibrationValue);
                #if defined(ESP8266) || defined(ESP32)
                EEPROM.commit();
                #endif

                _resume = true;
            }
        }
    }
}
