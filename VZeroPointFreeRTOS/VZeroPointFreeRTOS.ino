#include <WiFi.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>
const char* ssid     = "Bapt La Casse";
const char* password = "Bien essayé";
const char* api = "http://X.X.X.X";
WiFiServer server(1023);

#include <Adafruit_NeoPixel.h>
#ifdef __AVR__
#include <avr/power.h> // Required for 16 MHz Adafruit Trinket
#endif

// Which pin on the Arduino is connected to the NeoPixels?
// On a Trinket or Gemma we suggest changing this to 1:
#define LED_PIN     12

// How many NeoPixels are attached to the Arduino?
#define LED_COUNT  60

// NeoPixel brightness, 0 (min) to 255 (max)
#define BRIGHTNESS 150 // Set BRIGHTNESS (max = 255)

// Declare our NeoPixel strip object:
Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_RGB + NEO_KHZ800);

void TaskUpdateLight( void *pvParameters );
void TaskListenToServer( void *pvParameters );
TaskHandle_t Task_Handle1;
TaskHandle_t Task_Handle2;

int previousSoundRead = 0;

enum Type { NONE, LUMINOSITY, SOUND, PRESENCE};

enum Operator { EQUAL, DIFF, GREATERTHAN, LESSERTHAN, GREATEROREQUAL, LESSEROREQUAL};

class Factor {
  public:
    Type type;
    float value;
    Factor(Type type, float value) {
      this->type = type;
      this->value = value;
      init();
    }
};

class Condition {
  public:
    Type type;
    Operator conditionOperator;
    int value;

    Condition(Type type, Operator conditionOperator, int value) {
      this->type = type;
      this->value = value;
      this->conditionOperator = conditionOperator;
      init();
    }
};

class State {
  public:
    String name;
    int color;
    int color2;
    Factor factors[3] = {
      Factor(NONE, 0.0f),
      Factor(NONE, 0.0f),
      Factor(NONE, 0.0f)
    };
    Condition conditions[3] = {
      Condition(NONE, EQUAL, 0.0f),
      Condition(NONE, EQUAL, 0.0f),
      Condition(NONE, EQUAL, 0.0f)
    };

    State(String name, int color, int color2, Factor factors[], Condition conditions[]) {
      this->name = name;
      this->color = color;
      this->color2 = color2;


      for (int i = 0; i < 3; i++) {
        this->factors[i] = factors[i];
        this->conditions[i] = conditions[i];
      }
      init();
    }
};


Factor factorsOn[3] = {
  Factor(PRESENCE, 1.0f),
  Factor(NONE, 0.0f),
  Factor(NONE, 0.0f)
};
Factor factorsOff[3] = {
  Factor(SOUND, 1.0f),
  Factor(PRESENCE, 1.0f),
  Factor(NONE, 0.0f)
};
Condition conditionsOn[3] = {
  Condition(LUMINOSITY, GREATERTHAN, 600.0f),
  Condition(NONE, EQUAL, 0.0f),
  Condition(NONE, EQUAL, 0.0f)
};
Factor noFactors[3] = {
  Factor(NONE, 0.0f),
  Factor(NONE, 0.0f),
  Factor(NONE, 0.0f)
};
Condition noConditions[3] = {
  Condition(NONE, EQUAL, 0.0f),
  Condition(NONE, EQUAL, 0.0f),
  Condition(NONE, EQUAL, 0.0f)
};

State emptyState = State("NULL", 0, 0, noFactors, noConditions);

State states[3] = {
  State("On", 3937310, 3289610, factorsOn, conditionsOn),
  State("Off", 65795, 657935, factorsOff, noConditions),
  emptyState
};

int color = 0xFFFFFF;
int red = 0;
int green = 0;
int blue = 0;


void increaseColorBy(int newColor, float percentage) {
  int newRed = (newColor & 0xFF0000) >> 16;
  int newGreen = (newColor & 0x00FF00) >> 8;
  int newBlue = (newColor & 0x0000FF);

  red = (1 - percentage) * red + percentage * newRed;
  green = (1 - percentage) * green + percentage * newGreen;
  blue = (1 - percentage) * blue + percentage * newBlue;
}

StaticJsonDocument<40000> JSONDocument;
StaticJsonDocument<40000> storedDatas;
JsonArray luminosities;
JsonArray sounds;
JsonArray presences;

void setupStoredDatasDocument() {
  storedDatas.clear();
  luminosities = storedDatas.createNestedArray("luminosity");
  sounds = storedDatas.createNestedArray("sound");
  presences = storedDatas.createNestedArray("presence");
}

void setup() {
  // These lines are specifically to support the Adafruit Trinket 5V 16 MHz.
  // Any other board, you can remove this part (but no harm leaving it):
#if defined(__AVR_ATtiny85__) && (F_CPU == 16000000)
  clock_prescale_set(clock_div_1);
#endif
  // END of Trinket-specific code.
  Serial.begin(115200);
  strip.begin();           // INITIALIZE NeoPixel strip object (REQUIRED)
  strip.show();            // Turn OFF all pixels ASAP
  strip.setBrightness(BRIGHTNESS);


  for (int i = 0; i < strip.numPixels(); i++) { // For each pixel in strip.
    strip.setPixelColor(i, strip.Color(10, 10, 10)); // Set white
  }

  xTaskCreate( TaskUpdateLight,  "Update lights",  2048, NULL, 2, &Task_Handle1);
  xTaskCreate( TaskListenToServer,  "Listen to server",  2048, NULL, 4, &Task_Handle2);
  
  strip.show(); // Update strip with new contents

  WiFi.begin("Bapt La Casse", "Baptiste");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.println("Trying to connect to WiFi");
    delay(500);
  }

  Serial.print("Connected to WiFi ! IP : ");
  Serial.println(WiFi.localIP());

  server.begin();


}
void loop() {
}
void TaskUpdateLight( void *pvParameters __attribute__((unused))) {

  for (;;) {
    for (int i = 0; i < strip.numPixels(); i++) { // For each pixel in strip.
      strip.setPixelColor(i, strip.Color(green, red, blue)); // Set white
    }

    strip.show(); // Update strip with new contents
    vTaskDelay( 100 );
    int luminosity = analogRead(A0);
    int soundRead = analogRead(A2);
    bool noise = abs(previousSoundRead - soundRead) > 100; //Sensor read differs by at least 100 units
    previousSoundRead = soundRead;
    bool presence = analogRead(A4) > 2000;

    //For each possible states
    for (int i = 0; i < sizeof(states); i++) {

      //Testing the conditions of this state
      bool conditionMet = true;
      for (int j = 0; j < 3; j++) {
        //Get the value and operator to use for this condition
        int valueToCompare = 0;
        switch (states[i].conditions[j].type) {
          case LUMINOSITY:
            valueToCompare = luminosity;
            break;
          case SOUND:
            valueToCompare = (int) noise;
            break;
          case PRESENCE:
            valueToCompare = (int) presence;
            break;
        }
        switch (states[i].conditions[j].conditionOperator) {
          case EQUAL:
            conditionMet = conditionMet && valueToCompare == states[i].conditions[j].value;
            break;
          case DIFF:
            conditionMet = conditionMet && valueToCompare != states[i].conditions[j].value;
            break;
          case GREATEROREQUAL:
            conditionMet = conditionMet && valueToCompare >= states[i].conditions[j].value;
            break;
          case LESSEROREQUAL:
            conditionMet = conditionMet && valueToCompare <= states[i].conditions[j].value;
            break;
          case GREATERTHAN:
            conditionMet = conditionMet && valueToCompare > states[i].conditions[j].value;
            break;
          case LESSERTHAN:
            conditionMet = conditionMet && valueToCompare < states[i].conditions[j].value;
            break;
        }

        //If condition isn't met
        if (!conditionMet) break;
      }

      //If ALL conditions are met, we shall use that state
      if (conditionMet) {
        float colorToUse = 0.0f;
        //For each possible factors
        for (int k = 0; k < 3; k++) {
          switch (states[i].factors[k].type) {
            case LUMINOSITY:
              colorToUse += luminosity * states[i].factors[k].value;
              break;
            case SOUND:
              if (noise)
                colorToUse += states[i].factors[k].value;
              break;
            case PRESENCE:
              if (presence) {
                colorToUse += states[i].factors[k].value;
              }
              break;
          }
        }

        // Apply the color
        // Note : added a 0.2 factor to smooth the transition

        Serial.print("State : ");
        Serial.print(states[i].name);
        Serial.print("   Percentage : ");
        Serial.print(colorToUse);
        Serial.print("  - red : ");
        Serial.print(red);
        Serial.print("   green : ");
        Serial.print(green);
        Serial.print("   blue : ");
        Serial.print(blue);
        Serial.print("  - Luminosity : ");
        Serial.print(luminosity);
        Serial.print("  Sound : ");
        Serial.print(noise);
        Serial.print("  Presence : ");
        Serial.println(presence);
        increaseColorBy(states[i].color, 0.2 * (1 - colorToUse));
        increaseColorBy(states[i].color2, 0.2 * (colorToUse));

        luminosities.add(luminosity);
        sounds.add((int) noise);
        presences.add((int) presence);

        i = 100; // skip the other states
      }
    }

  }
}

void TaskListenToServer( void *pvParameters __attribute__((unused)))
{
  for (;;) {
    vTaskDelay( 200);
    WiFiClient client = server.available();   // listen for incoming clients
    //client = server.available();
    if (client) { // if you get a client,
      Serial.println("New Client.");           // print a message out the serial port

      Serial.println("Recieving : ");

      String data = "";
      String JSONInput = "";
      bool jsonStarted = false;

      while (client.connected()) {
        if (client.available()) {             // if there's bytes to read from the client,
          char ch = client.read();             // read a byte, then
          Serial.write(ch);
          data += ch;

          if (ch == '{' && !jsonStarted) jsonStarted = true;
          if (jsonStarted) JSONInput += ch;
        }
        else {
          if (JSONInput.length() > 0) {
            DeserializationError error = deserializeJson(JSONDocument, JSONInput); //Parse message
            if (error) {   //Check for errors in parsing
              Serial.println("Parsing failed");
              Serial.println(error.c_str());
              client.println("HTTP/1.1 500");
            } else {
              const char* action = JSONDocument["action"];
              Serial.println("Parsing success : ");
              Serial.println(action);


              client.println("HTTP/1.1 200 OK");
              client.println("");
              if (String(action) == "getData") {
                serializeJson(storedDatas, client);
                setupStoredDatasDocument();
              }
              if (String(action) == "setAmbiance") {
                changeAmbiance();
              }
              client.stop();
            }

          }
        }
      }
    }
  }
}

void changeAmbiance() {
  JsonArray newStates = JSONDocument["states"].as<JsonArray>();
  states[0] = emptyState;
  states[1] = emptyState;
  states[2] = emptyState;

  int stateI = 0;
  for (JsonObject newState : newStates) {
    Factor newFactors[3] = {Factor(NONE, 0.0f), Factor(NONE, 0.0f), Factor(NONE, 0.0f)};
    Condition newConditions[3] = {Condition(NONE, EQUAL, 0.0f), Condition(NONE, EQUAL, 0.0f), Condition(NONE, EQUAL, 0.0f)};

    int factorI = 0;
    for (JsonObject newFactor : newState["factors"].as<JsonArray>()) {
      newFactors[factorI] = Factor(typeToEnum(newFactor["type"]), newFactor["value"]);
      factorI++;
    }
    int conditionI = 0;
    for (JsonObject newCondition : newState["conditions"].as<JsonArray>()) {
      newConditions[conditionI] = Condition(typeToEnum(newCondition["type"]), operatorToEnum(newCondition["operator"]), newCondition["value"]);
      conditionI++;
    }

    states[stateI] = State(newState["name"], newState["colors"][0], newState["colors"][1], newFactors, newConditions);
    stateI++;
  }
}

Type typeToEnum(String type) {
  if (type == "luminosity") return LUMINOSITY;
  if (type == "sound") return SOUND;
  if (type == "presence") return PRESENCE;
  return NONE;
}

Operator operatorToEnum(String type) {
  if (type == "==") return EQUAL;
  if (type == "!=") return DIFF;
  if (type == ">") return GREATERTHAN;
  if (type == "<") return LESSERTHAN;
  if (type == ">=") return GREATEROREQUAL;
  if (type == "<=") return LESSEROREQUAL;
  return EQUAL;
}
