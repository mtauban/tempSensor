/**
    tempSensor
    main.cpp

    Purpose: simple firmware for esp8266 with REST API for controling a relay and
    monitoring temperature

    @author Mathieu Tauban
    @version 0.0.1 1/09/2017
*/
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>

#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>

#include <DallasTemperature.h>
#include <ArduinoJson.h>

#include <user_default.h>


#define ONE_WIRE_BUS D1
#define CLIENT_DATA_JSON_SIZE 500
#define UD_RELAY_PIN 16
#define UD_LED_PIN LED_BUILTIN
#define UD_SECURITY_MS 300000
#define UD_RELAY_OFF HIGH
#define UD_RELAY_ON  LOW
#define UD_STATE_OFF 0
#define UD_STATE_ON 1


// following line to allow frontend debugging with permissive CORS
// more data on CORS : https://www.w3.org/TR/cors/
// Philosophy : Any specific headers or HTTP verbs requested by client calls via
// Access-Control-Request-Method or Access-Control-Request-Headers will be
// automatically added to the corresponding response headers.
// In other words, this middleware ALLOWS ALL THE THINGS.
// --> description from https://github.com/caike/permissive-cors
// #define DEBUG_CORS (1)

unsigned int relaystate = 0;
unsigned long lasttime = 0;

char temperatureString[6];

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature DS18B20(&oneWire);


AsyncWebServer server(80);

AsyncEventSource events("/events");


/**
    Returns the temperature as measured by the DS18B20 temperature sensor
*/
float getTemperature() {
    float temp;
    do {
        DS18B20.requestTemperatures();
        temp = DS18B20.getTempCByIndex(0);
        delay(1);
    } while (temp == 85.0 || temp == (-127.0));
    return temp;
}


/**
    Turns the relay ON
    Returns the state of the relay
*/
int turnRelayOn() {
    digitalWrite(UD_RELAY_PIN, UD_RELAY_ON);
    relaystate = UD_STATE_ON;
    return relaystate;
}
/**
    Turns the relay OFF
    Returns the state of the relay
*/
int turnRelayOff() {
    digitalWrite(UD_RELAY_PIN, UD_RELAY_OFF);
    relaystate = UD_STATE_OFF;
    return relaystate;
}

/**
    Returns the state of the relay
*/
int getRelayState() {
    return relaystate;
}


/**
    Returns a formated json string with temp, dlm and status
*/
String formatedJSONResponse() {
    StaticJsonBuffer<CLIENT_DATA_JSON_SIZE> jsonBuffer;
    JsonObject& root = jsonBuffer.createObject();

    dtostrf(getTemperature(), 2, 2, temperatureString);

    root["temp"] = temperatureString;
    unsigned long dlm = millis() - lasttime;
    root["dlm"] = dlm;
    root["status"] = relaystate;
    String ret;
    root.printTo(ret);
    return ret;
}


// WARNING : Intended for debugging of frontend only
// returns a very permissive CORS response to AJAX request
#ifdef DEBUG_CORS
void handleCORS(AsyncWebServerRequest * request,  AsyncWebServerResponse * response) {
    // ALLOW ALL THE HEADERS !!!!!!
    for (int i = 0; i <  request->headers(); i++) {
        AsyncWebHeader * h = request->getHeader(i);
        if (h->name() == "Access-Control-Request-Method") {
            response->addHeader("Access-Control-Allow-Method", h->value());
        }
        if (h->name() == "Access-Control-Request-Headers") {
            response->addHeader("Access-Control-Allow-Headers", h->value());
        }
    }
    response->addHeader("Access-Control-Allow-Origin", "*");
}
#endif





void setup() {

    pinMode(UD_RELAY_PIN, OUTPUT);
    digitalWrite(UD_RELAY_PIN, UD_RELAY_OFF);

    DS18B20.begin();


    lasttime = millis();

    WiFi.hostname(UD_HOSTNAME);
    WiFi.mode(WIFI_STA);

    WiFi.begin(UD_WIFI_SSID, UD_WIFI_PASS);
    if (WiFi.waitForConnectResult() != WL_CONNECTED) {
        // STA Failed
        WiFi.disconnect(false);
        delay(1000);
        WiFi.begin(UD_WIFI_SSID, UD_WIFI_PASS);
    }

    //Send OTA events to the browser
    ArduinoOTA.onStart([]() {
        events.send("Update Start", "ota");
    });
    ArduinoOTA.onEnd([]() {
        events.send("Update End", "ota");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        char p[32];
        sprintf(p, "Progress: %u%%\n", (progress/(total/100)));
        events.send(p, "ota");
    });
    ArduinoOTA.onError(
        [](ota_error_t error) {
            if(error == OTA_AUTH_ERROR) events.send("Auth Failed", "ota");
            else if(error == OTA_BEGIN_ERROR) events.send("Begin Failed", "ota");
            else if(error == OTA_CONNECT_ERROR) events.send("Connect Failed", "ota");
            else if(error == OTA_RECEIVE_ERROR) events.send("Recieve Failed", "ota");
            else if(error == OTA_END_ERROR) events.send("End Failed", "ota");
        }
    );
    ArduinoOTA.setHostname(UD_HOSTNAME);
    ArduinoOTA.begin();

    MDNS.addService("http", "tcp", 80);

    events.onConnect(
        [](AsyncEventSourceClient *client) {
            client->send("hello!",NULL,millis(),1000);
        }
    );


    server.addHandler(&events);


    // get request
    // returns free heap
    // never been used
    server.on("/heap", HTTP_GET,[](AsyncWebServerRequest *request) {
        request->send(200, "text/plain", String(ESP.getFreeHeap()));
    });


    // get request
    // sets relay on
    // returns the json status
    server.on("/state/on", HTTP_GET, [](AsyncWebServerRequest * request) {
        lasttime = millis();
        turnRelayOn();
        request->send(200, "application/json", formatedJSONResponse());
    });

    // get request
    // sets relay off
    // returns the json status
    server.on("/state/off", HTTP_GET, [](AsyncWebServerRequest *request) {
        lasttime = millis();
        turnRelayOff();
        request->send(200, "application/json", formatedJSONResponse());
    });

    // returns the json status
    server.on("/state", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", formatedJSONResponse());
    });

    // post request
    // used for setting status theoretically
    // returns the json status
    server.on("/state", HTTP_POST,
        [](AsyncWebServerRequest * request) {
            AsyncWebServerResponse * response =  request->beginResponse(200, "application/json", formatedJSONResponse());
            #ifdef DEBUG_CORS
            handleCORS(request, response);
            #endif
            request->send(response);
        },
        [](AsyncWebServerRequest * request, String filename, size_t index, uint8_t * data, size_t len, bool final) {
        },
        [](AsyncWebServerRequest * request, uint8_t * data, size_t len, size_t index, size_t total) {

        DynamicJsonBuffer jsonBuffer;
        JsonObject & root = jsonBuffer.parseObject(data);

        // Test if parsing succeeds.
        if (!root.success()) {
            // Parsing failed, aborting
            return;
        }
        if (root.containsKey("status")) {
            int status = root["status"];
            lasttime = millis();
            if (status==1) turnRelayOn();
            else turnRelayOff();
        }
    });

    server.onNotFound(
    [](AsyncWebServerRequest *request) {
        request->send(404);
    });

    server.begin();
}




void loop() {
    // server related events are handled directly by events handler ...

    // Over the Air firmware
    ArduinoOTA.handle();

    // Security check
    unsigned long dlm = millis();
    if ((relaystate == 1) && (dlm - lasttime > UD_SECURITY_MS)) {
        digitalWrite(UD_RELAY_PIN, HIGH);
        relaystate = 0;
        lasttime = dlm;
    }

}
