#include <Arduino.h>

// Bibliotheken für die Weboberfläche
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <NewPing.h>
#include <WiFi.h>

#include "LittleFS.h"

// Pinbelegungen für die Motoren
const int motorPin1 = 16;     // IN1 auf dem L298N
const int motorPin2 = 17;     // IN2 auf dem L298N
const int motorEnable1 = 18;  // ENA auf dem L298N

const int motorPin3 = 19;     // IN3 auf dem L298N
const int motorPin4 = 21;     // IN4 auf dem L298N
const int motorEnable2 = 22;  // ENB auf dem L298N

int minSpeed = 60;
int maxSpeed = 255;

int speedR = 100;
int speedL = 100;

int lastSpeed = 150;

// Times for random actions
long lastDrive = 0;
long durationDrive = 0;
long lastChange = 0;
long durationChange = 0;
long lastPause = 0;
long durationPause = 0;
long minMSDrive = 50000;
long maxMSDrive = 100000;
long minMSPause = 50000;
long maxMSPause = 100000;
long minMSChange = 10000;
long maxMSChange = 20000;

// Pinbelegungen für die Ultraschallsensoren
#define TRIGGER_PIN_FRONT_LEFT 23
#define ECHO_PIN_FRONT_LEFT 25
#define TRIGGER_PIN_FRONT_RIGHT 26
#define ECHO_PIN_FRONT_RIGHT 27
#define TRIGGER_PIN_SIDE_LEFT 32
#define ECHO_PIN_SIDE_LEFT 33
#define TRIGGER_PIN_SIDE_RIGHT 14
#define ECHO_PIN_SIDE_RIGHT 13

// Ultraschallsensor-Objekte
#define MAX_DISTANCE 200  // maximale Distanz für die Sensoren in cm
NewPing sonarFrontLeft(TRIGGER_PIN_FRONT_LEFT, ECHO_PIN_FRONT_LEFT, MAX_DISTANCE);
NewPing sonarFrontRight(TRIGGER_PIN_FRONT_RIGHT, ECHO_PIN_FRONT_RIGHT, MAX_DISTANCE);
NewPing sonarSideLeft(TRIGGER_PIN_SIDE_LEFT, ECHO_PIN_SIDE_LEFT, MAX_DISTANCE);
NewPing sonarSideRight(TRIGGER_PIN_SIDE_RIGHT, ECHO_PIN_SIDE_RIGHT, MAX_DISTANCE);

long duration, distanceFrontLeft, distanceFrontRight, distanceSideLeft, distanceSideRight;

bool manualMode = false;  // Manueller Modus ein/aus

bool drive = true;

// Webserver (später verwenden)
AsyncWebServer server(80);

void setup() {
    durationPause = random(minMSPause, maxMSPause);
    durationDrive = random(minMSDrive, maxMSDrive);
    Serial.begin(115200);  // Startet die serielle Kommunikation

    // Motor-Pins als Output definieren
    pinMode(motorPin1, OUTPUT);
    pinMode(motorPin2, OUTPUT);
    pinMode(motorEnable1, OUTPUT);

    pinMode(motorPin3, OUTPUT);
    pinMode(motorPin4, OUTPUT);
    pinMode(motorEnable2, OUTPUT);

    ledcSetup(0, 10000, 8);  // Kanal 0 für RPWM
    ledcSetup(1, 10000, 8);  // Kanal 1 für LPWM
    ledcAttachPin(motorEnable1, 0);
    ledcAttachPin(motorEnable2, 1);

    // Initialisierung des WLANs und des Webservers
    WiFi.softAP("Robot1", "1234x6789");  // Hier deine Daten eintragen
    // server.begin();

    // SPIFFS starten
    if (!LittleFS.begin(true)) {
        Serial.println("An error has occurred while mounting SPIFFS");
        return;
    }

    // Webserver Routen
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(LittleFS, "/control.html", "text/html");
    });

    // Serve the control page
    server.on("/control", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(LittleFS, "/control.html", "text/html");
    });

    server.on("/control", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (request->hasParam("action", true)) {
            String action = request->getParam("action", true)->value();
            if (action == "start") {
                drive = true;
                manualMode = false;
                long lastDrive = millis();
                long lastChange = 0;
                long lastPause = 0;
            } else if (action == "stop") {
                drive = false;
            } else if (action == "faster") {
                if (speedL < maxSpeed) speedL += 10;
                if (speedR < maxSpeed) speedR += 10;
                lastSpeed = speedL;
            } else if (action == "slower") {
                if (speedL > minSpeed) speedL -= 10;
                if (speedR > minSpeed) speedR -= 10;
                lastSpeed = speedL;
            } else if (action == "drive") {
                drive = true;
                manualMode = true;
                long lastDrive = 0;
                long lastChange = 0;
                long lastPause = 0;
                if (request->hasParam("speedR", true)) {
                    speedR = request->getParam("speedR", true)->value().toInt();
                }
                if (request->hasParam("speedL", true)) {
                    speedL = request->getParam("speedL", true)->value().toInt();
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
    json += "\"speedR\":" + String(speedR) + ",";
    json += "\"speedL\":" + String(speedL) + ",";
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
    // Webserver starten
    server.begin();
}

void motorControl(int motor, int speed, bool direction) {
    if (motor == 1) {
        digitalWrite(motorPin1, direction);
        digitalWrite(motorPin2, !direction);
        ledcWrite(0, speed);
    } else if (motor == 2) {
        digitalWrite(motorPin3, direction);
        digitalWrite(motorPin4, !direction);
        ledcWrite(1, speed);
    }
}

void updateDistance() {
    distanceFrontLeft = sonarFrontLeft.ping_cm(50);
    distanceFrontRight = sonarFrontRight.ping_cm(50);
    distanceSideLeft = sonarSideLeft.ping_cm(50);
    distanceSideRight = sonarSideRight.ping_cm(50);
    if (distanceFrontLeft == 0) {
        distanceFrontLeft = 100;
    }
    if (distanceFrontRight == 0) {
        distanceFrontRight = 100;
    }
    if (distanceSideLeft == 0) {
        distanceSideLeft = 100;
    }
    if (distanceSideRight == 0) {
        distanceSideRight = 100;
    }
}

void loop() {
    // Lese Abstände von den Sensoren
    updateDistance();

    // Ausgabe der Sensorwerte auf die serielle Konsole
    Serial.print("Links: ");
    Serial.print(distanceSideLeft);
    Serial.print("cm, Rechts: ");
    Serial.print(distanceSideRight);
    Serial.println("cm");
    Serial.print("LinksVorne: ");
    Serial.print(distanceFrontLeft);
    Serial.print("cm, RechtsVorne: ");
    Serial.print(distanceFrontRight);
    Serial.println("cm");

    if (drive) {
        // Entscheidungen basierend auf dem Modus
        if (!manualMode) {
            // Automatischer Modus
            if (distanceSideLeft < 40 || distanceSideRight < 40 || distanceFrontLeft < 40 || distanceFrontRight < 40) {
                // Wenn ein Hindernis näher als 20 cm ist, reagiere entsprechend
                if (distanceSideLeft < 40 || distanceFrontLeft < 40 && distanceSideRight < 40 || distanceFrontRight < 40) {
                    // Hindernis links
                    Serial.println("Hindernis links erkannt, drehe nach rechts.");
                    // Drehe nach rechts
                    motorControl(1, speedR, false);  // Motor 1 rückwärts
                    motorControl(2, speedL, false);   // Motor 2 vorwärts
                }
                // Wenn ein Hindernis näher als 20 cm ist, reagiere entsprechend
                if (distanceSideLeft < 40 || distanceFrontLeft < 40) {
                    // Hindernis links
                    Serial.println("Hindernis links erkannt, drehe nach rechts.");
                    // Drehe nach rechts
                    motorControl(1, speedR, true);  // Motor 1 rückwärts
                    motorControl(2, speedL, false);   // Motor 2 vorwärts
                }
                if (distanceSideRight < 40 || distanceFrontRight < 40) {
                    // Hindernis rechts
                    Serial.println("Hindernis rechts erkannt, drehe nach links.");
                    // Drehe nach links
                    motorControl(1, speedR, false);   // Motor 1 vorwärts
                    motorControl(2, speedL, true);  // Motor 2 rückwärts
                }
            } else {
                // Kein Hindernis, fahre geradeaus
                // TODO: Richtungsänderrungen und Pausen
                Serial.println("Kein Hindernis, fahre geradeaus.");
                motorControl(1, speedR, true);  // Beide Motoren vorwärts
                motorControl(2, speedL, true);
            }
            // Random einbauen:
            unsigned long currentMillis = millis();

            // Überprüfen, ob die Pausenzeit vorbei ist und wir wieder fahren sollen
            if (currentMillis > lastPause) {
                if (speedL == 0 && speedR == 0) {  // Wenn wir in der Pause sind
                    // Fahre wieder los
                    durationDrive = random(minMSDrive, maxMSDrive);
                    lastDrive = currentMillis + durationDrive;
                    speedL = lastSpeed;
                    speedR = lastSpeed;
                }

                // Überprüfen, ob die Fahrzeit vorbei ist und wir pausieren sollen
                if (currentMillis > lastDrive) {
                    durationPause = random(minMSPause, maxMSPause);
                    lastPause = currentMillis + durationPause;
                    // Stoppe und speichere den alten Wert ab
                    speedL = 0;
                    speedR = 0;
                }

                // Überprüfen, ob wir eine zufällige Änderung vornehmen sollen
                if (currentMillis > lastChange && speedL != 0 && speedR != 0) {
                    durationChange = random(minMSChange, maxMSChange);
                    lastChange = currentMillis + durationChange;
                    int randomMotor = random(0, 2);           // Wähle zufällig einen Motor (0 für links, 1 für rechts)
                    int randomSpeedChange = random(-50, 51);  // Zufällige Geschwindigkeitsänderung zwischen -50 und 50

                    if (randomMotor == 0) {
                        speedL = constrain(speedL + randomSpeedChange, minSpeed, maxSpeed);
                    } else {
                        speedR = constrain(speedR + randomSpeedChange, minSpeed, maxSpeed);
                    }
                }
            }
        } else {
            // Manueller Modus
            // Hier würde die Logik für manuelle Steuerbefehle stehen, die von der Weboberfläche empfangen werden.
            Serial.println("Manueller Modus aktiv - Steuerung über Webinterface.");
            Serial.println("Kein Hindernis, fahre geradeaus.");
            motorControl(1, speedR, true);  // Beide Motoren vorwärts
            motorControl(2, speedL, true);
        }
    } else {
        motorControl(1, 0, true);  // Beide Motoren aus
        motorControl(2, 0, true);
    }

    delay(100);  // Kurze Verzögerung zur Schleifensteuerung und zur Reduzierung von Sensorrauschen
}
