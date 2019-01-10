#include <WiFiUdp.h>
#include <NTPClient.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPclient.h>
#include <elapsedMillis.h>
#include <ArduinoJson.h>
#include <stdio.h>
#include <time.h>


// credenciales wifi
char ssid[22] = "Fibertel PIERAGOSTINI";
char clave[16] = "01430882078";

// base de datos firebase
const String FIREBASE_HOST = "https://firestore.googleapis.com/v1beta1/projects/control-de-riego-9f37b/databases/(default)/documents/sensores";

// abro conexión para consultas
HTTPClient HTTPclient;

// seteo el reloj para la hora actual
WiFiUDP udp;
NTPClient NTPclient(udp, "0.south-america.pool.ntp.org", -10800); // GMT -3 = -(3600 * 3);
byte currentDate;

elapsedMillis timer; // seteo el timer general
elapsedMillis timerB; // seteo el timer para las bombas o válvulas

long maxCicles = 43200; //48; controla la cantidad de ciclos máximos hasta reniciar. Como chequeo cada 30 minutos, tengo 48 ciclos en un día.
long timeCicle = (1000 * 60 * 60 * 24) / maxCicles; // el tiempo de duración de un cliclo: 1 día / maxCicles
byte cicles = 1;
long timerPump = timeCicle * maxCicles;
long timerStopPump = 600000;  // voy a esperar 10 minutos antes de volver a sensar y ver si necesito prender nuevamente la bomba
const byte maxWater = 5; // máximo de veces que voy a regar con la bomba
byte flagWater = 0;

// variables / constantes de humedad y riego
const byte MOISTURE_SENSORS[] = {2, 4}; // sensores de humedad
const byte MOISTURE_SENSORS_QUANTITY = sizeof(MOISTURE_SENSORS) / sizeof(byte); // cantidad de sensores de humedad
const byte MIN_MOISTURE = 50; //definida en porcentaje de humedad
const byte MAX_MOISTURE = 80; //definida en porcentaje de humedad
const byte SUMMER_WATER_TIME = 19;
const byte WINTER_WATER_TIME = 10;
#define ART (-3)

// variables / constantes de bombas - relays
const byte PUMPS[] = {2, 1}; // pines de las bombas o válvulas
const byte PUMPS_QUANTITY = sizeof(PUMPS) / sizeof(byte); // cantidad de bombas de agua
bool pumpState[PUMPS_QUANTITY];


void setup() {
  Serial.begin(38400);
  delay(1000);
  Serial.println("Inicializando el programa...");
}


void loop() {
  // cada bomba debe estar máximo 5 minutos prendida, cortar 10 minutos y volver a sensar hasta lograr el valor de humedad deseado y apagar la bomba  
  Serial.print("timer: ");
  Serial.println(timer);
  Serial.print("timerB: ");
  Serial.println(timerB);
  Serial.print("timerPump: ");
  Serial.println(timerPump);
  Serial.print("timeCicle: ");
  Serial.println(timeCicle);
  
  if (timerB >= timerPump) {
    connectWifi(); // conecto al wifi
    
    byte moistureSensor[MOISTURE_SENSORS_QUANTITY];
        
    for (byte i = 0; i < MOISTURE_SENSORS_QUANTITY; i ++) {
      /* falta control para una sola bomba  */
      Serial.print("Estado de la bomba ");
      Serial.print(i);
      Serial.print(": ");
      Serial.println(pumpState[i]);
      
      if (pumpState[i]) {
        turnPumpOff(i);
      } else {
        moistureSensor[i] = getMappedMoistureSensor(i);  // mido la humedad del sensor
        Serial.print("Humedad del sensor ");
        Serial.print(i);
        Serial.print(": ");
        Serial.println(moistureSensor[i]);
        
        if (moistureSensor[i] <= MIN_MOISTURE) {
          Serial.print("Cantidad de riegos: ");
          Serial.println(flagWater);
          
          if (flagWater > maxWater) {
            timerPump = timeCicle * maxCicles;
            Serial.print("Actúalización del timerPump: ");
            Serial.println(timerPump);
          } else {
            turnPumpOn(i);
            timerPump += timerB;
            flagWater ++;
            Serial.print("Actúalización del timerPump: ");
            Serial.println(timerPump);
          }
          
        } else if (moistureSensor[i] > MIN_MOISTURE) {
          timerPump = timeCicle * maxCicles;
          Serial.print("Actúalización del timerPump: ");
          Serial.println(timerPump);
        }
        
      }
    }
  } else {
    // como medida de seguridad apago todas las bombas
    for (byte i = 0; i < MOISTURE_SENSORS_QUANTITY; i ++) {
      turnPumpOff(i);
    }
  }

  // chequeo cada ciclo de media hora
  if (timer >= timeCicle) {
    connectWifi(); // conecto al wifi

    int *currentDate = getCurrentDate();  // traigo la fecha actual
    String strCurrentDate;
    for (byte i = 0; i < 6; i ++) {
      strCurrentDate += (*currentDate + (i));
      if (i < 2) strCurrentDate += "-";
      else if (i == 2) strCurrentDate += " ";
      else if (i < 5) strCurrentDate += ":";
    }
  
    byte *allSensorsMoisture = getNeedWater();  // necesitan agua los sensores?
    bool rainsToday = getForecast(); // veo si va a llover (1 = llueve; 0 = no llueve)

    // es el primer ciclo? > en el primer ciclo riego
    Serial.print("Cantidad de ciclos: ");
    Serial.println(cicles);
    Serial.print("Llueve hoy?: ");
    Serial.println(rainsToday);

    // puse < a 10 para probar... acá tiene que ir == 1
    if (cicles < 10) {
      Serial.print("Necesitan agua los sensores?: ");
      Serial.println(allSensorsMoisture[0]);
      
      if (allSensorsMoisture[0]) {
        if (!rainsToday) {
          if (PUMPS_QUANTITY == 1) { // hay una sola bomba, la prendo si ningún sensor se opone
            bool turnOn = true;
            for (byte i = 1; i < MOISTURE_SENSORS_QUANTITY + 1; i ++) {
              Serial.print("Humedad sensor (array)");
              Serial.print(i);
              Serial.print(": ");
              Serial.println(allSensorsMoisture[i]);
              Serial.print("Humedad sensor (pointer)");
              Serial.print(i);
              Serial.print(": ");
              Serial.println(*allSensorsMoisture + (i + 1));
              if (*(allSensorsMoisture + (i + 1)) == 2) turnOn = false;
            }
            if (turnOn) *pumpState = *turnPumpOn(0);

          } else { // hay más de una bomba y un sensor de humedad por bomba
            for (byte i = 0; i < MOISTURE_SENSORS_QUANTITY; i ++) {
              if (*(allSensorsMoisture + (i + 1)) == 1) bool *pumpState = turnPumpOn(i); // prendo la bomba solo si está en 1 = necesita agua
            }
          }
        }
      }

      timer = 0; // reseteo el timer

    } else if (cicles == maxCicles) {
      timer = fixTime(*(currentDate + 4)); // le envío los minutos y ajusto el timer con los minutos de desajuste
      cicles = fixCicles(*(currentDate + 1)); // le envío el mes y ajusto los ciclos en función de la estacionalidad (verano / invierno)
    } else {
      timer = 0; // reseteo el timer
    }

    /* transmito los datos:
       - fecha y hora
       - humedad de cada sensor
       - llueve hoy?
    */
    
    String payload = "{\"date\":";
    payload += strCurrentDate;
    payload += ",";
    payload += "{\"cicleNumber\":";
    payload += cicles;
    payload += ",";
    payload += "\"rainsToday\":";
    payload += rainsToday;
    payload += ", \"sensors\": [";
    for (byte i = 0; i < MOISTURE_SENSORS_QUANTITY; i ++) { 
      if (i > 0) payload += ", ";
      payload += "{\"sensorId\":";
      payload += i + 1;
      payload += ", \"moistureSensor\":";
      payload += *(allSensorsMoisture + (i + 1));
      payload += "}";
    }
    payload += "]";
    payload += ", \"pumps\": [";
    for (byte i = 0; i < PUMPS_QUANTITY; i ++) { 
      if (i > 0) payload += ", ";
      payload += "{\"pumpId\":";
      payload += i + 1;
      payload += ", \"pumpState\":";
      payload += *pumpState + i;
      payload += "}";
    }
    payload += "]}";

    int payloadSize = payload.length();
    
    Serial.println(payload);
    
    transmitData(strCurrentDate, payload);

    WiFi.disconnect(); // cierro la conexión
    cicles ++; // aumento la cantidad de ciclos
  }
}

// transmito los datos
bool transmitData(String jsonName, String payload) {
  // guardar datos en firebase
  String endpointName = FIREBASE_HOST + "/documentID=" + jsonName;
  HTTPclient.begin(endpointName);
  HTTPclient.addHeader("Content-Type", "application/json");
  int clientCode = HTTPclient.PUT(payload); // put json into firebase
  
  bool clientSuccess = false;
  
  if (clientCode > 0) {
    if (clientCode == 200) {
      Serial.println("Transmisión de datos correcta");
      bool clientSuccess = true;
    } else {
      Serial.println("Error en la transmisión de datos, código HTTP: " + clientCode);
    }
  }
  HTTPclient.end();
  return clientSuccess;
}


byte *getNeedWater() {
  int moistureSensor[MOISTURE_SENSORS_QUANTITY];
  static byte toWater[MOISTURE_SENSORS_QUANTITY];

  /* uso las banderas regar con 3 estados:
     0 = no necesita agua (tiene suficiente)
     1 = necesita agua (tiene menos del mínimo)
     2 = tiene exceso de agua (no regarlo)
  */

  /* voy a usar toWater[0] para indicar si es necesario regar o directamente no.
     en el resto del array pongo la situación de cada sensor
  */

  toWater[0] = 0;

  for (byte i = 1; i < MOISTURE_SENSORS_QUANTITY + 1; i ++) {
    // chequeo humedad de los sensores
    moistureSensor[i] = getMappedMoistureSensor(i);
    Serial.print("Humedad del sensor ");
    Serial.print(i);
    Serial.print(": ");
    Serial.println(moistureSensor[i]);
    
    if (moistureSensor[i] <= MIN_MOISTURE) {
      toWater[i] = 1; // necesita agua
      toWater[0] = 1; // hay un sensor que necesita agua...
      Serial.print(">> El sensor ");
      Serial.print(i);
      Serial.print(" necesita agua: ");
      Serial.println(toWater[0]);
    } else if (moistureSensor[i] < MAX_MOISTURE) {
      toWater[i] = 0; // no necesita agua
    } else {
      toWater[i] = 2; // tiene exceso de agua
    }
    
    Serial.print("El sensor ");
    Serial.print(i);
    Serial.print(" necesita regar?: ");
    if (toWater[i] == 1) Serial.println(" Si");
    else if (toWater[i] == 2) Serial.println(" No... tiene exceso");
    else if (toWater[i] == 0) Serial.println(" No... está OK");
  }

  return toWater;
}


// devuelve el porcentaje de humedad ya mapeado
byte getMappedMoistureSensor(byte i) {
  //int moistureSensor =  map(analogRead(MOISTURE_SENSORS[i]), 0, 1023, 0, 100);
  /* para prueba */
  int moistureSensor = 81;
  if (i == 1) moistureSensor = 20;

  return moistureSensor;
}


// prendo la bomba que me indiquen
bool *turnPumpOn(byte pump) {
  // activo relay... ver cómo
  /*
     Estados de la bomba:
     - 0 = apagada
     - 1 = prendida
  */
  pumpState[pump] = true;
  timerPump = 300000;  // 5 minutos de bomba encendida

  Serial.print(">> Se prendió una bomba. La bomba ");
  Serial.print(pump);
  Serial.print(", estado: ");
  Serial.println(pumpState[pump]);
  
  return pumpState;
}

// apago la bomba que me indiquen
void turnPumpOff(byte pump) {
  // ver como desactivo el relay
  pumpState[pump] = false;
  timerPump = timer + timerStopPump;

  Serial.print("<< Se apagó una bomba. La bomba ");
  Serial.print(pump);
  Serial.print(", estado: ");
  Serial.println(pumpState[pump]);
}


// conecto WiFi
void connectWifi() {
  Serial.print("Conectando a red " + String(ssid));
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, passwd);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(100);
  }
  udp.begin(2390);
  Serial.println(" ok!");
}


// busco la fecha actual
int *getCurrentDate() {
  NTPclient.begin();
  NTPclient.update();
  //NTPclient.end();

  time_t dateNTPclient = NTPclient.getEpochTime();
    
  struct tm *dateUTCClient = gmtime(&fechaClienteNtp);

  static int currentDate[6];
  currentDate[0] = dateUTCClient->tm_year + 1900; // año;
  currentDate[1] = dateUTCClient->tm_mon + 1; // mes
  currentDate[2] = dateUTCClient->tm_mday; // día del mes
  currentDate[3] = dateUTCClient->tm_hour; // hora
  currentDate[4] = dateUTCClient->tm_min; // minutos
  currentDate[5] = dateUTCClient->tm_sec; // segundos

  return currentDate;
}

// devuelvo el valor inicial de timer para ajustarse a los defasajes de ms que pudo haber en el día
long fixTime(byte currentMinutes) {
  unsigned long fixedTimer;
  if ((currentMinutes * 60 * 1000) >= timeCicle) currentMinutes = - timeCicle;
  fixedTimer = currentMinutes;

  Serial.print(">> Ajusto el timer ");
  Serial.println(fixedTimer);
  
  return fixedTimer;
}

// ...
byte fixCicles(byte currentMonth) {
  byte currentCicle = 0;
  byte hour1;
  byte hour2;

  // si estamos en invierno/otoño > riego a la mañana: 10hs
  if (currentMonth > 3 && currentMonth < 10) {
    /*
        Si es verano, estoy en el ciclo 48 (fin del día) son las 19hs...
        el próximo ciclo 1 debe ser a las 10hs
        entonces me faltan: 15hs > 30 medias horas
    */
    // si estamos en verano/primavera > riego a la tardecita: 19hs
    hour1 = WINTER_WATER_TIME;
    hour2 = SUMMER_WATER_TIME;
  } else {
    /*
        Si es invierno, estoy en el ciclo 48 (fin del día) son las 10hs...
        el próximo ciclo 1 debe ser a las 19hs
        entonces me faltan: 9hs > 18 medias horas
    */

    hour1 = SUMMER_WATER_TIME;
    hour2 = WINTER_WATER_TIME;
  }

  if (hour1 > hour2) currentCicle = (hour1 - hour2) / (24 / maxCicles);
  else if (hour1 < hour2) currentCicle = (24 - hour2 + hour1) / (24 / maxCicles);
  else currentCicle = 0;

  return currentCicle;
}


// consulto el pronóstico
bool getForecast() {
  bool rain = false;

  HTTPclient.begin("http://api.openweathermap.org/data/2.5/forecast?id=524901&APPID=353995861eb7811526342a9b49f8e6e2&cnt=8"); // open connection to ask the forecast
  int clientCode = HTTPclient.GET();
  
  if (clientCode > 0) {
    String jsonRafaelaForecast = HTTPclient.getString();
    jsonRafaelaForecast.replace('\"', '\\"');

    // defino el tamaño del json y creo el objeto
    const size_t capacity = 8 * JSON_ARRAY_SIZE(1) + JSON_ARRAY_SIZE(8) + 8 * JSON_OBJECT_SIZE(0) + 24 * JSON_OBJECT_SIZE(1) + 9 * JSON_OBJECT_SIZE(2) + 9 * JSON_OBJECT_SIZE(4) + JSON_OBJECT_SIZE(5) + 8 * JSON_OBJECT_SIZE(8) + 8 * JSON_OBJECT_SIZE(9);
    DynamicJsonBuffer jsonBuffer(capacity);

    // parseo el json
    JsonObject& forecast = jsonBuffer.parseObject(jsonRafaelaForecast);

    if (forecast.success()) {
      for (byte i = 0; i < 8; i ++) { // chequeo los primeros 8 ciclos que suman 1 día (8 ciclos * 3 horas)
        Serial.print(">> La API del tiempo dice que hoy ");
        Serial.println(forecast["list"][i]["weather"]["main"] <= 500 && forecast["list"][i]["weather"]["main"]);
          
        if (forecast["list"][i]["weather"]["main"] <= 500 && forecast["list"][i]["weather"]["main"] > 600) {
          rain = true;
          break;
        }
      }
    }


  }
  HTTPclient.end();
  
  return rain;
}
