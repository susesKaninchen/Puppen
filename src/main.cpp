#include <Arduino.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <NewPing.h>
#include <WiFi.h>
#include "LittleFS.h"
#include <Ramp.h>
#include <ArduinoOTA.h>
#define AVOID_LAG_MIN 1000
#define AVOID_LAG_MAX 3000
bool AP = false;

// Pinbelegungen für die Motoren
const int motorPin1 = 16;
const int motorPin2 = 17;
const int motorEnable1 = 18;
const int motorPin3 = 19;
const int motorPin4 = 21;
const int motorEnable2 = 22;

int minSpeed = 60;
int maxSpeed = 255;
// Ramp-Objekte für die Motoren, mit +/- int
rampInt motorRampL;
rampInt motorRampR;
int lastSpeed = 150;

// Zeiten für zufällige Aktionen
long minMSChange = 10000;
long maxMSChange = 20000;

// Zeiten für Bewegungsabläufe
long lastDrive = 0, durationDrive = 0, lastPause = 0, durationPause = 0;
long minMSDrive = 50000, maxMSDrive = 100000, minMSPause = 50000, maxMSPause = 100000;

long duration;
long lastChange = 0, durationChange = 0; // Hinzugefügt

// Pinbelegungen für die Ultraschallsensoren
#define TRIGGER_PIN_FRONT_LEFT 23
#define ECHO_PIN_FRONT_LEFT 25
#define TRIGGER_PIN_FRONT_RIGHT 26
#define ECHO_PIN_FRONT_RIGHT 27
#define TRIGGER_PIN_SIDE_LEFT 32
#define ECHO_PIN_SIDE_LEFT 33
#define TRIGGER_PIN_SIDE_RIGHT 14
#define ECHO_PIN_SIDE_RIGHT 13

#define MAX_DISTANCE 200
NewPing sonarFrontLeft(TRIGGER_PIN_FRONT_LEFT, ECHO_PIN_FRONT_LEFT, MAX_DISTANCE);
NewPing sonarFrontRight(TRIGGER_PIN_FRONT_RIGHT, ECHO_PIN_FRONT_RIGHT, MAX_DISTANCE);
NewPing sonarSideLeft(TRIGGER_PIN_SIDE_LEFT, ECHO_PIN_SIDE_LEFT, MAX_DISTANCE);
NewPing sonarSideRight(TRIGGER_PIN_SIDE_RIGHT, ECHO_PIN_SIDE_RIGHT, MAX_DISTANCE);

long distanceFrontLeft, distanceFrontRight, distanceSideLeft, distanceSideRight;
bool manualMode = false;
bool drive = true;

// Variablen für das Ausweichmanöver
bool isAvoiding = false;
unsigned long avoidanceEndTime = 0;
unsigned long avoidanceDuration = 5000; // Standardmäßig 5 Sekunden
int avoidanceDirection = 0; // 1 für links, 2 für rechts, 0 für rückwärts

AsyncWebServer server(80);


void setup() {
    // Starte ausgabe auf der seriellen Konsole
    Serial.begin(115200);
    // Warte 10 sekunden

    // Initialisiere die Motoren
    pinMode(motorPin1, OUTPUT); pinMode(motorPin2, OUTPUT); pinMode(motorEnable1, OUTPUT);
    pinMode(motorPin3, OUTPUT); pinMode(motorPin4, OUTPUT); pinMode(motorEnable2, OUTPUT);
    ledcSetup(0, 10000, 8); ledcSetup(1, 10000, 8); 
    ledcAttachPin(motorEnable1, 0); ledcAttachPin(motorEnable2, 1);
    Serial.println("Motoren initialisiert");
    // Option wür WLAN und Access Point
    if (AP) {
        WiFi.softAP("Robot1", "1234x6789");
        Serial.println("Access Point gestartet");
    } else {
        WiFi.begin("Seewald", "....");
        Serial.println("WLAN Verbindung wird hergestellt");
        while (WiFi.status() != WL_CONNECTED) {
            delay(1000);
            Serial.println("WLAN Verbindung wird hergestellt");
        }
        Serial.println("WLAN Verbindung hergestellt");
    }

    if (!LittleFS.begin(true)) {
        Serial.println("SPIFFS Fehler");
        return;
    }
    Serial.println("SPIFFS initialisiert");
    
    // Webserver Routen
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(LittleFS, "/control.html", "text/html");
    });

    // Serve the control page
    server.on("/control", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(LittleFS, "/control.html", "text/html");
    });

    server.on("/control", HTTP_POST, [](AsyncWebServerRequest *request) {
        Serial.println("POST /control received");
        Serial.println(request->params());
        if (request->hasParam("action", true)) {
            String action = request->getParam("action", true)->value();
            if (action == "start") {
                drive = true;
                manualMode = false;
                lastDrive = millis(); // 'long' entfernt
                lastChange = 0;
                lastPause = 0;
            } else if (action == "stop") {
                drive = false;
            } else if (action == "faster") {
                lastSpeed = motorRampL.getTarget();
                if (motorRampL.getTarget() < maxSpeed) motorRampL.go(motorRampL.getTarget() + 10, 100);
                if (motorRampR.getTarget() < maxSpeed) motorRampR.go(motorRampR.getTarget() + 10, 100);
            } else if (action == "slower") {
                lastSpeed = motorRampL.getTarget();
                if (motorRampL.getTarget() > minSpeed) motorRampL.go(motorRampL.getTarget() - 10, 100);
                if (motorRampR.getTarget() > minSpeed) motorRampR.go(motorRampR.getTarget() - 10, 100);
            } else if (action == "drive") {
                drive = true;
                manualMode = true;
                lastDrive = 0; // 'long' entfernt
                lastChange = 0;
                lastPause = 0;
                if (request->hasParam("speedR", true)) {
                    motorRampR.go(request->getParam("speedR", true)->value().toInt(), 100);
                }
                if (request->hasParam("speedL", true)) {
                    motorRampL.go(request->getParam("speedL", true)->value().toInt(), 100);
                }
            }
            request->send(200, "text/plain", "Action executed");
        } else {
            request->send(400, "text/plain", "Invalid parameters");
        }
    });

    server.on("/settings", HTTP_GET, [](AsyncWebServerRequest *request) {
        String json = "{";
        json += "\"minDrive\":" + String(minMSDrive) + ",";
        json += "\"maxDrive\":" + String(maxMSDrive) + ",";
        json += "\"minPause\":" + String(minMSPause) + ",";
        json += "\"maxPause\":" + String(maxMSPause) + ",";
        json += "\"minChange\":" + String(minMSChange) + ",";
        json += "\"maxChange\":" + String(maxMSChange);
        json += "}";
        request->send(200, "application/json", json);
    });

    server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        String json = "{";
        json += "\"minSpeed\":" + String(minSpeed) + ",";
        json += "\"maxSpeed\":" + String(maxSpeed) + ",";
        json += "\"speedR\":" + String(motorRampL.update()) + ",";
        json += "\"speedL\":" + String(motorRampR.update()) + ",";
        json += "\"lastSpeed\":" + String(lastSpeed) + ",";
        json += "\"lastDrive\":" + String(lastDrive) + ",";
        json += "\"durationDrive\":" + String(durationDrive) + ",";
        json += "\"lastChange\":" + String(lastChange) + ",";
        json += "\"durationChange\":" + String(durationChange) + ",";
        json += "\"lastPause\":" + String(lastPause) + ",";
        json += "\"durationPause\":" + String(durationPause) + ",";
        json += "\"minMSDrive\":" + String(minMSDrive) + ",";
        json += "\"maxMSDrive\":" + String(maxMSDrive) + ",";
        json += "\"minMSPause\":" + String(minMSPause) + ",";
        json += "\"maxMSPause\":" + String(maxMSPause) + ",";
        json += "\"minMSChange\":" + String(minMSChange) + ",";
        json += "\"maxMSChange\":" + String(maxMSChange) + ",";
        json += "\"duration\":" + String(duration) + ",";
        json += "\"millis\":" + String(millis()) + ",";
        json += "\"distanceFrontLeft\":" + String(distanceFrontLeft) + ",";
        json += "\"distanceFrontRight\":" + String(distanceFrontRight) + ",";
        json += "\"distanceSideLeft\":" + String(distanceSideLeft) + ",";
        json += "\"distanceSideRight\":" + String(distanceSideRight) + ",";
        json += "\"manualMode\":" + String(manualMode) + ",";
        json += "\"drive\":" + String(drive);
        json += "}";
        request->send(200, "application/json", json);
    });

    //Status menschen lesbar (PAuse, Fahren ausweichen,links rechts)
    server.on("/info", HTTP_GET, [](AsyncWebServerRequest *request) {
        // Der Roboter ..... und fährt mit einer Geschwindigkeit von ....., dabei muss er in .... Sekunden anhalten	, er weicht nach .... aus
        String status = "Der Roboter ";
        if (isAvoiding) {
            status += "weicht einem Hindernis aus und fährt ";
            if (avoidanceDirection == 1) {
                status += "nach rechts";
            } else if (avoidanceDirection == 2) {
                status += "nach links";
            } else {
                status += "rückwärts";
            }
            // mit einer geschwindigkeit von
            status += " mit einer Geschwindigkeit von (L)" + String(motorRampL.update()) + " und (R)" + String(motorRampR.update());
        } else if (drive) {
            status += "fährt mit einer Geschwindigkeit von (L)" + String(motorRampL.update()) + " und (R)" + String(motorRampR.update());
            if (motorRampL.update() == 0 && motorRampR.update() == 0) {
                status += ", dabei fährt er in " + String((lastPause - millis()) / 1000) + " Sekunden weiter";
            } else {
                status += ", dabei muss er in " + String((lastDrive - millis()) / 1000) + " Sekunden anhalten";
            }
        } else {
            status += "steht still";
        } 
        request->send(200, "text/plain", status);
    });

    server.on("/settings", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (request->hasParam("minDrive", true)) {
            minMSDrive = request->getParam("minDrive", true)->value().toInt();
        }
        if (request->hasParam("maxDrive", true)) {
            maxMSDrive = request->getParam("maxDrive", true)->value().toInt();
        }
        if (request->hasParam("minPause", true)) {
            minMSPause = request->getParam("minPause", true)->value().toInt();
        }
        if (request->hasParam("maxPause", true)) {
            maxMSPause = request->getParam("maxPause", true)->value().toInt();
        }
        if (request->hasParam("minChange", true)) {
            minMSChange = request->getParam("minChange", true)->value().toInt();
        }
        if (request->hasParam("maxChange", true)) {
            maxMSChange = request->getParam("maxChange", true)->value().toInt();
        }
        request->send(200, "text/plain", "Settings updated");
    });

    durationPause = random(minMSPause, maxMSPause);
    durationDrive = random(minMSDrive, maxMSDrive);
    server.serveStatic("/", LittleFS, "/");
    // OTA initialisieren
    ArduinoOTA
    .onStart([]() {
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH)
        type = "sketch";
      else // U_SPIFFS
        type = "filesystem";

      // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
      Serial.println("Start updating " + type);
    })
    .onEnd([]() {
      Serial.println("\nEnd");
      ESP.restart();
    })
    .onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    })
    .onError([](ota_error_t error) {
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });

  ArduinoOTA.begin();
    Serial.println("OTA initialisiert");
    server.begin();
    Serial.println("Webserver initialisiert");
    
}

void motorControl(int motor, int speed) {
    bool direction = speed >= 0;
    Serial.print("Motor ");
    Serial.print(motor);
    Serial.print(" Speed ");
    Serial.print(speed);
    Serial.print(" Direction ");
    Serial.println(direction);
    if (motor == 1) {
        digitalWrite(motorPin1, direction);
        digitalWrite(motorPin2, !direction);
        ledcWrite(0, abs(speed));
    } else if (motor == 2) {
        digitalWrite(motorPin3, direction);
        digitalWrite(motorPin4, !direction);
        ledcWrite(1, abs(speed));
    }
}

void updateDistance() {
    distanceFrontLeft = sonarFrontLeft.ping_cm();
    distanceFrontRight = sonarFrontRight.ping_cm();
    distanceSideLeft = sonarSideLeft.ping_cm();
    distanceSideRight = sonarSideRight.ping_cm();

    distanceFrontLeft = distanceFrontLeft == 0 ? MAX_DISTANCE : distanceFrontLeft;
    distanceFrontRight = distanceFrontRight == 0 ? MAX_DISTANCE : distanceFrontRight;
    distanceSideLeft = distanceSideLeft == 0 ? MAX_DISTANCE : distanceSideLeft;
    distanceSideRight = distanceSideRight == 0 ? MAX_DISTANCE : distanceSideRight;
    Serial.print("Front Left: ");
    Serial.print(distanceFrontLeft);
    Serial.print(" Front Right: ");
    Serial.print(distanceFrontRight);
    Serial.print(" Side Left: ");
    Serial.print(distanceSideLeft);
    Serial.print(" Side Right: ");
    Serial.println(distanceSideRight);
}

bool isObstacleDetected() {
    return distanceFrontLeft < 40 || distanceFrontRight < 40 || distanceSideLeft < 40 || distanceSideRight < 40;
}

void obstacleAvoidance() {
    if (avoidanceDirection == 1) {
        // Update ramp only, if the target is different
        int newTarget = abs(motorRampL.getTarget())*-1;
        if (motorRampL.getTarget() != newTarget) {
            motorRampL.go(newTarget, 1000);
        }
        if(motorRampR.getTarget() != abs(motorRampR.getTarget())) {
            motorRampR.go(abs(motorRampR.getTarget()), 1000);
        }
        // Hindernis links erkannt, drehe nach rechts
        // linker Motor rückwärts
        // rechter Motor vorwärts
    } else if (avoidanceDirection == 2) {
        // Update ramp only, if the target is different
        int newTarget = abs(motorRampR.getTarget())*-1;
        if (motorRampR.getTarget() != newTarget) {
            motorRampR.go(newTarget, 1000);
        }
        if(motorRampL.getTarget() != abs(motorRampL.getTarget())) {
            motorRampL.go(abs(motorRampL.getTarget()), 1000);
        }
        // Hindernis rechts erkannt, drehe nach links
        // linker Motor vorwärts
        // rechter Motor rückwärts
    } else {
        // Update ramp only, if the target is different
        int newTarget = abs(motorRampL.getTarget())*-1;
        if (motorRampL.getTarget() != newTarget) {
            motorRampL.go(newTarget, 1000);
        }
        newTarget = abs(motorRampR.getTarget())*-1;
        if (motorRampR.getTarget() != newTarget) {
            motorRampR.go(newTarget, 1000);
        }
        // Hindernis vorne erkannt, fahre rückwärts
        // linker Motor rückwärts
        // rechter Motor rückwärts
    }
}

void loop() {
    ArduinoOTA.handle();
    updateDistance();
    if (drive) {
        if (!manualMode) {
            unsigned long currentMillis = millis();
            // Hinderniss immer aktuell ausweichen
            if (isObstacleDetected()) {
                // Hindernis erkannt, Ausweichmodus starten
                isAvoiding = true;
                avoidanceDuration = random(AVOID_LAG_MIN, AVOID_LAG_MAX); // Zwischen 5 und 10 Sekunden
                avoidanceEndTime = currentMillis + avoidanceDuration;

                // Bestimme die Ausweichrichtung
                if (distanceFrontLeft < 40 && distanceFrontRight < 40) {
                    avoidanceDirection = 0; // Hindernis vorne, zurücksetzen
                }
                else if (distanceFrontLeft < 40 || distanceSideLeft < 40) {
                    avoidanceDirection = 1; // Hindernis links, drehe nach rechts
                } else if (distanceFrontRight < 40 || distanceSideRight < 40) {
                    avoidanceDirection = 2; // Hindernis rechts, drehe nach links
                }
                obstacleAvoidance();
                motorControl(1, motorRampR.update());
                motorControl(2, motorRampL.update());
                return;
            }
            // Ausweichen nachlaufen lassen
            if (isAvoiding) {
                if (currentMillis >= avoidanceEndTime) {
                    // Ausweichzeit abgelaufen
                    isAvoiding = false;
                } else {
                    // Weiterhin ausweichen
                    obstacleAvoidance();
                    motorControl(1, motorRampR.update());
                    motorControl(2, motorRampL.update());
                    // Rest der Schleife überspringen
                    return;
                }
            }

            // Update auf vorwärts, wenn target ist noch rückwärts
            if (motorRampL.getTarget() < 0) {
                motorRampL.go(abs(motorRampL.getTarget()), 1000);
            }
            if (motorRampR.getTarget() < 0) {
                motorRampR.go(abs(motorRampR.getTarget()), 1000);
            }
            // Kein Hindernis und nicht im Ausweichmodus, normales Fahren
            motorControl(1, motorRampR.update());  // beide Motoren vorwärts
            motorControl(2, motorRampL.update());

            // Optional: Zufällige Anpassungen und Pausen einfügen
            if (currentMillis > lastPause) {
                if (motorRampL.getTarget() == 0 && motorRampR.getTarget() == 0) {
                    durationDrive = random(minMSDrive, maxMSDrive);
                    lastDrive = currentMillis + durationDrive;
                    motorRampL.go(lastSpeed, 1000); motorRampR.go(lastSpeed, 1000);
                } else {
                    lastSpeed = motorRampL.getTarget();
                }
                if (currentMillis > lastDrive) {
                    durationPause = random(minMSPause, maxMSPause);
                    lastPause = currentMillis + durationPause;
                    motorRampL.go(0, 1000); motorRampR.go(0, 1000);
                }
            }

        } else {
            // Manueller Modus
            // Kein Hindernis und nicht im Ausweichmodus, normales Fahren
            motorControl(1, motorRampR.update());  // beide Motoren vorwärts
            motorControl(2, motorRampL.update());
        }
    } else {
        // Anhalten
        motorControl(1, 0);
        motorControl(2, 0);
    }
    delay(100);
}
