#include <WiFiUdp.h>
#include <NTPClient.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <elapsedMillis.h>
//#include <ArduinoJson.h>
#include <stdio.h>
#include <time.h> tr


// credenciales wifi
char ssid[22] = "Fibertel PIERAGOSTINI";
char passwd[16] = "01430882078";

// base de datos de Mlab y Servicio de clima de OWM
const String MLAB_HOST = "https://api.mlab.com/api/1/databases/roof-irrigation-control/collections/measurements"; 
const String MLAB_AUTH = "AfORlm1l3lAs-KpD0Mecp6THxTncFx41";
const String FORECAST_API_CALL = "http://api.openweathermap.org/data/2.5/forecast?id=524901&APPID=353995861eb7811526342a9b49f8e6e2&cnt=8";

HTTPClient HTTPclient;                                                      // abro conexión para consultas

WiFiUDP udp;                                                                // seteo el reloj para la hora actual
NTPClient NTPclient(udp, "0.south-america.pool.ntp.org", -10800);           // GMT -3 = -(3600 * 3);
elapsedMillis timer;                                                        // seteo el timer general
elapsedMillis timerB;                                                       // seteo el timer para las bombas o válvulas

// variables / constantes de humedad y riego
const byte MOISTURE_SENSORS[] = {2, 4};                                                 // sensores de humedad
const byte MOISTURE_SENSORS_QUANTITY = sizeof(MOISTURE_SENSORS) / sizeof(byte);         // cantidad de sensores de humedad
const byte MIN_MOISTURE = 50;                                                           //definida en porcentaje de humedad
const byte MAX_MOISTURE = 80;                                                           //definida en porcentaje de humedad
const byte SUMMER_WATER_TIME = 19;
const byte WINTER_WATER_TIME = 10;
#define ART (-3)

long maxCicles = 43200;                                                     //48; controla la cantidad de ciclos máximos hasta reniciar. Como chequeo cada 30 minutos, tengo 48 ciclos en un día.
long timeCicle = (1000 * 60 * 60 * 24) / maxCicles;                         // el tiempo de duración de un cliclo: 1 día / maxCicles
byte cicles = 1;
long timerPump = timeCicle * maxCicles;
long timerStopPump = 600000;                                                // voy a esperar 10 minutos antes de volver a sensar y ver si necesito prender nuevamente la bomba
const byte maxWater = 5;                                                    // máximo de veces que voy a regar con la bomba
byte flagWater[MOISTURE_SENSORS_QUANTITY];

// variables / constantes de bombas - relays
const byte PUMPS[] = {2, 1};                                                            // pines de las bombas o válvulas
const byte PUMPS_QUANTITY = sizeof(PUMPS) / sizeof(byte);                               // cantidad de bombas de agua

// variables globales usadas en funciones que se reutilizan en todo el código
int currentDate[6];                                                                     // en este array guardo la fecha actual
byte allSensorsMoisture[MOISTURE_SENSORS_QUANTITY];                                     // estado de todos los sensores
bool pumpState[PUMPS_QUANTITY];                                                         // estado de las bombas
byte moistureSensor[MOISTURE_SENSORS_QUANTITY];                                         // humedad de los sensores


void setup() {
  Serial.begin(38400);
  delay(1000);
  Serial.println("Inicializando el programa...");
}


void loop() {
  // cada bomba debe estar máximo 5 minutos prendida, cortar 10 minutos y volver a sensar hasta lograr el valor de humedad deseado y apagar la bomba
  printInConsole("timer", String(timer), "");
  printInConsole("timerB", String(timerB), "");
  printInConsole("timerPump", String(timerPump), "");
  printInConsole("timeCicle", String(timeCicle), "");

  if (timerB >= timerPump) {
    connectWifi(); // conecto al wifi

    for (byte i = 0; i < MOISTURE_SENSORS_QUANTITY; i ++) {
      /* falta control para una sola bomba  */
      printInConsole("Estado de la bomba " + i, String(pumpState[i]), "");

      if (pumpState[i]) {
        turnPumpOff(i);
      } else {
        moistureSensor[i] = getMappedMoistureSensor(i);                                   // mido la humedad del sensor
        printInConsole("Humedad del sensor" + i, String(moistureSensor[i]), "");

        if (moistureSensor[i] <= MIN_MOISTURE) {
          printInConsole("Cantidad de riegos del sensor " + i, String(flagWater[i]), "");

          if (flagWater[i] > maxWater) {
            timerPump = timeCicle * maxCicles;
            printInConsole("Actualización del timerPump", String(timerPump), "");

          } else {
            turnPumpOn(i);
            timerPump += timerB;
            flagWater[i] ++;
            printInConsole("Actualización del timerPump", String(timerPump), "");
          }

        } else if (moistureSensor[i] > MIN_MOISTURE) {
          timerPump = timeCicle * maxCicles;
          printInConsole("Actualización del timerPump", String(timerPump), "");
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
    connectWifi();                                                                        // conecto al wifi

    getCurrentDate();                                                                     // traigo la fecha actual
    String strCurrentDate = getStrCurrentDate();
    getNeedWater();                                                                       // necesitan agua los sensores?
    bool rainsToday = getForecast();                                                      // veo si va a llover (1 = llueve; 0 = no llueve)

    // es el primer ciclo? >> en el primer ciclo riego
    printInConsole("Cantidad de ciclos", String(cicles), "");
    printInConsole("Llueve hoy?", String(rainsToday), "");

    // puse < a 10 para probar... acá tiene que ir == 1
    if (cicles < 10) {
      printInConsole("Necesitan agua los sensores?", String(allSensorsMoisture[0]), "");

      if (allSensorsMoisture[0]) {
        if (!rainsToday) {
          if (PUMPS_QUANTITY == 1) {                                                       // hay una sola bomba, la prendo si ningún sensor se opone
            bool turnOn = true;
            for (byte i = 1; i < MOISTURE_SENSORS_QUANTITY + 1; i ++) {
              printInConsole("Humedad sensor " + i, String(allSensorsMoisture[i + 1]), "");

              if (allSensorsMoisture[i + 1] == 2) turnOn = false;
            }
            if (turnOn) turnPumpOn(0);

          } else { // hay más de una bomba y un sensor de humedad por bomba
            for (byte i = 0; i < MOISTURE_SENSORS_QUANTITY; i ++) {
              if (allSensorsMoisture[i + 1] == 1) turnPumpOn(i);                            // prendo la bomba solo si está en 1 = necesita agua
            }
          }
        }
      }

      timer = 0; // reseteo el timer

    } else if (cicles == maxCicles) {
      timer = fixTime(currentDate[4]);                                                      // le envío los minutos y ajusto el timer con los minutos de desajuste
      //cicles = fixCicles(currentDate[1]);                                                 // le envío el mes y ajusto los ciclos en función de la estacionalidad (verano / invierno)
    } else {
      timer = 0;                                                                            // reseteo el timer
    }

    /* transmito los datos:
       - fecha y hora
       - humedad de cada sensor
       - llueve hoy?
    */

    /*String payload = getJsonPayload(strCurrentDate, cicles, rainsToday);
    printInConsole("payLoad", String(payload), "");
    */
    if (bool transmit = transmitData(strCurrentDate, cicles, rainsToday)) {
      printInConsole("Firebase API response", String(transmit), "");
    }

    WiFi.disconnect();                                                                      // cierro la conexión
    cicles ++;                                                                              // aumento la cantidad de ciclos
  }
}

// transmito los datos
bool transmitData(String strCurrentDate, byte ciclos, bool rainsToday) {
  
  // armo el json
  const size_t capacity = 11*JSON_OBJECT_SIZE(1) + 3*JSON_OBJECT_SIZE(2) + JSON_OBJECT_SIZE(5) + 350;
  DynamicJsonBuffer jsonBuffer(capacity);
  JsonObject& root = jsonBuffer.createObject();

  JsonObject& fields = root.createNestedObject("fields");

  // fecha
  JsonObject& date = fields.createNestedObject("date");
  date["StringValue"] = strCurrentDate;
  // número de ciclo
  JsonObject& cicleNumber = fields.createNestedObject("cicleNumber");
  cicleNumber["IntegerValue"] = ciclos;
  // llueve hoy?
  JsonObject& rains = fields.createNestedObject("rainsToday");
  rains["BooleanValue"] = rainsToday;

  // bombas
  JsonObject& fields_pumps = fields.createNestedObject("pumps");
  JsonObject& fields_pumps_mapValue = fields_pumps.createNestedObject("mapValue");  
  JsonObject& fields_pumps_mapValue_fields = fields_pumps_mapValue.createNestedObject("fields");
  for (byte i = 0; i < PUMPS_QUANTITY; i ++) {
    JsonObject& fields_pumps_mapValue_fields_i = fields_pumps_mapValue_fields.createNestedObject(String(i + 1));
    fields_pumps_mapValue_fields["integerValue"] = allSensorsMoisture[i + 1];
  }
  
  // sensores
  JsonObject& fields_sensors = fields.createNestedObject("sensors");
  JsonObject& fields_sensors_mapValue = fields_sensors.createNestedObject("mapValue");  
  JsonObject& fields_sensors_mapValue_fields = fields_sensors_mapValue.createNestedObject("fields");
  for (byte i = 0; i < PUMPS_QUANTITY; i ++) {
    JsonObject& fields_sensors_mapValue_fields_i = fields_sensors_mapValue_fields.createNestedObject(String(i));
    fields_sensors_mapValue_fields["integerValue"] = pumpState[i];
  }
  
  // guardar datos en mlab
  HTTPclient.begin(MLAB_HOST + "apiKey?" + MLAB_AUTH);                                                      // open connection to post data
  /// _--- - --___ SEGUIR ACA
  

  //String&Print payload;
  String payload;
  root.printTo(payload);
  printInConsole("Json paylod to transmit", payload, "");
  
  if (Firebase.failed()) {
    Serial.println(Firebase.error());
    printInConsole("Firebase failed", Firebase.error(), "");
  } else {
    printInConsole("Firebase success! fucking yeah!", String(Firebase.success()), "");  
  }
  
  return Firebase.success();
}


void getNeedWater() {
  int moistureSensor[MOISTURE_SENSORS_QUANTITY];

  /* uso las banderas regar con 3 estados:
     0 = no necesita agua (tiene suficiente)
     1 = necesita agua (tiene menos del mínimo)
     2 = tiene exceso de agua (no regarlo)
  */

  /* voy a usar toWater[0] para indicar si es necesario regar o directamente no.
     en el resto del array pongo la situación de cada sensor
  */

  allSensorsMoisture[0] = 0;

  for (byte i = 1; i < MOISTURE_SENSORS_QUANTITY + 1; i ++) {
    // chequeo humedad de los sensores
    moistureSensor[i] = getMappedMoistureSensor(i);
    printInConsole("Humedad del sensor" + i, String(moistureSensor[i]), "ACA!");

    if (moistureSensor[i] <= MIN_MOISTURE) {
      allSensorsMoisture[i] = 1;                                                            // necesita agua
      allSensorsMoisture[0] = 1;                                                            // hay un sensor que necesita agua...
      printInConsole(">> Sensor", String(i), ">> Necesita agua");

    } else if (moistureSensor[i] < MAX_MOISTURE) {
      allSensorsMoisture[i] = 0;                                                            // no necesita agua
    } else {
      allSensorsMoisture[i] = 2;                                                            // tiene exceso de agua
    }

    if (allSensorsMoisture[i] == 1) printInConsole(">> Sensor ", String(i), ">> Necesita riego");
    else if (allSensorsMoisture[i] == 2) printInConsole(">> Sensor ", String(i), ">> No necesita riego > tiene exceso");
    else if (allSensorsMoisture[i] == 0) printInConsole(">> Sensor ", String(i), ">> No necesita riego > está OK");
  }
}


// devuelve el porcentaje de humedad ya mapeado
byte getMappedMoistureSensor(byte i) {
  //descomentar: int moistureSensor =  map(analogRead(MOISTURE_SENSORS[i]), 0, 1023, 0, 100);
  /* para prueba */
  int moistureSensor = 81;
  if (i == 1) moistureSensor = 20;

  return moistureSensor;
}


// prendo la bomba que me indiquen
void turnPumpOn(byte pump) {
  // activo relay... ver cómo
  /*
     Estados de la bomba:
     - 0 = apagada
     - 1 = prendida
  */
  pumpState[pump] = true;
  timerPump = 300000;                                                                       // 5 minutos de bomba encendida
  printInConsole("Estado de la bomba" + pump, String(pumpState[pump]), "");
}

// apago la bomba que me indiquen
void turnPumpOff(byte pump) {
  // ver como desactivo el relay
  pumpState[pump] = false;
  timerPump = timer + timerStopPump;

  printInConsole("Estado de la bomba" + pump, String(pumpState[pump]), "");
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


// busco la fecha actual y la guardo en un array
void getCurrentDate() {
  NTPclient.begin();
  NTPclient.update();

  time_t dateNTPclient = NTPclient.getEpochTime();
  struct tm *dateUTCClient = gmtime(&dateNTPclient);

  NTPclient.end();

  currentDate[0] = dateUTCClient->tm_year + 1900;                                           // año;
  currentDate[1] = dateUTCClient->tm_mon + 1;                                               // mes
  currentDate[2] = dateUTCClient->tm_mday;                                                  // día del mes
  currentDate[3] = dateUTCClient->tm_hour;                                                  // hora
  currentDate[4] = dateUTCClient->tm_min;                                                   // minutos
  currentDate[5] = dateUTCClient->tm_sec;                                                   // segundos
}

// tomo la fecha generada por getCurrentDate y la devuelvo como string
String getStrCurrentDate() {
  String strCurrentDate;

  for (byte i = 0; i < 6; i ++) {
    strCurrentDate += currentDate[i];
    if (i < 2) strCurrentDate += "-";
    else if (i == 2) strCurrentDate += " ";
    else if (i < 5) strCurrentDate += ":";
  }

  return strCurrentDate;
}


// devuelvo el valor inicial de timer para ajustarse a los defasajes de ms que pudo haber en el día
long fixTime(byte currentMinutes) {
  unsigned long fixedTimer;
  if ((currentMinutes * 60 * 1000) >= timeCicle) currentMinutes = - timeCicle;
  fixedTimer = currentMinutes;
  printInConsole("Ajusto el timer", String(fixedTimer), "");

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

  HTTPclient.begin(FORECAST_API_CALL);                                                      // open connection to ask the forecast
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

void printInConsole(String key, String value, String comment) {
  Serial.print(key);
  Serial.print(": ");
  Serial.print(value);
  Serial.print(". ");
  Serial.println(comment);
}
