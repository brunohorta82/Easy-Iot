#include "Switches.h"
#include "Mqtt.h"
#include "Discovery.h"

std::vector<SwitchT> switchs;
bool requestSaveSwitchs = false;
void removeSwitch(String id, bool persist)
{
  logger(SWITCHES_TAG, "Remove switch " + id);
  unsigned int del = NO_GPIO;
  for (unsigned int i = 0; i < switchs.size(); i++)
  {
    SwitchT swStored = switchs[i];
    if (strcmp(id.c_str(), swStored.id) == 0)
    {
      removeFromDiscovery(&swStored);
      unsubscribeOnMqtt(String(swStored.mqttCommandTopic));
      del = i;
    }
  }
  if (del != NO_GPIO)
  {
    switchs.erase(switchs.begin() + del);
  }
  if (persist)
  {
    requestSaveSwitchs= true;  
  }
}
void initSwitchesMqttAndDiscovery()
{
  for (unsigned int i = 0; i < switchs.size(); i++)
  {
    publishOnMqtt(switchs[i].mqttStateTopic, switchs[i].mqttPayload, switchs[i].mqttRetain);
    addToDiscovery(&switchs[i]);
  }
}
JsonObject updateSwitches(JsonObject doc, bool persist)
{
  String newId = doc.getMember("id").as<String>().equals(NEW_ID) ? String(String(ESP.getChipId()) + normalize(doc.getMember("name").as<String>())) : doc.getMember("id").as<String>();
  if(persist)
  removeSwitch(newId, false);
  SwitchT sw;
  strlcpy(sw.id, String(String(ESP.getChipId()) + normalize(doc.getMember("name").as<String>())).c_str(), sizeof(sw.id));
  strlcpy(sw.name, doc.getMember("name").as<String>().c_str(), sizeof(sw.name));
  strlcpy(sw.family, doc.getMember("family").as<String>().c_str(), sizeof(sw.family));
  sw.primaryGpio = doc["primaryGpio"] | NO_GPIO;
  sw.secondaryGpio = doc["secondaryGpio"] | NO_GPIO;
  sw.timeBetweenStates = doc["timeBetweenStates"] | 60000;
  sw.autoState = (doc["autoStateDelay"] | 0) > 0 && strlen(doc["autoStateValue"] | "") > 0 ;
  sw.autoStateDelay = doc["autoStateDelay"] | 0;
  strlcpy(sw.autoStateValue, doc.getMember("autoStateValue").as<String>().c_str(), sizeof(sw.autoStateValue));
  sw.typeControl = doc["typeControl"] | TYPE_MQTT;
  sw.mode = doc["mode"] | MODE_SWITCH;
  sw.pullup = doc["pullup"] | true;
  sw.mqttRetain = doc["mqttRetain"] | true;
  sw.inverted = doc["inverted"] | false;
  String baseTopic = getBaseTopic() + "/" + String(sw.family) + "/" + String(sw.id);

  doc["mqttCommandTopic"] =  String(baseTopic + "/set");
  doc["mqttStateTopic"] =  String(baseTopic + "/state");
  strlcpy(sw.mqttCommandTopic, String(baseTopic + "/set").c_str(), sizeof(sw.mqttCommandTopic));
  strlcpy(sw.mqttStateTopic, String(baseTopic + "/state").c_str(), sizeof(sw.mqttStateTopic));
  sw.primaryGpioControl = doc["primaryGpioControl"] | NO_GPIO;
  sw.secondaryGpioControl = doc["secondaryGpioControl"] | NO_GPIO;
  if (sw.pullup)
  {
    if (sw.primaryGpio != NO_GPIO)
    {
      pinMode(sw.primaryGpio, sw.primaryGpio == 16 ? INPUT_PULLDOWN_16 :INPUT_PULLUP  );
    }
    if (sw.secondaryGpio != NO_GPIO)
    {
      pinMode(sw.secondaryGpio, sw.secondaryGpio == 16 ? INPUT_PULLDOWN_16 :INPUT_PULLUP  );
    }
  }
  if (String(sw.family).equals(String(FAMILY_COVER)))
  {
    sw.statePoolStart = doc["statePoolStart"] | 2;
    sw.statePoolEnd = doc["statePoolEnd"] | 4;
    sw.positionControlCover = doc["positionControlCover"] | 0;
    sw.lastPercentage = doc["lastPercentage"] | 0;
    strlcpy(sw.mqttPositionCommandTopic, String(baseTopic + "/setposition").c_str(), sizeof(sw.mqttPositionCommandTopic));
    strlcpy(sw.mqttPositionStateTopic, String(baseTopic + "/position").c_str(), sizeof(sw.mqttPositionStateTopic));
    doc["mqttPositionCommandTopic"] = String(baseTopic + "/setposition");
    doc["mqttPositionStateTopic"] =  String(baseTopic + "/position");
  }
  else if (String(sw.family).equals(String(FAMILY_LOCK)))
  {
    sw.statePoolStart = doc["statePoolStart"] | 6;
    sw.statePoolEnd = doc["statePoolEnd"] | 7;
  }
  else
  {
    sw.statePoolStart = doc["statePoolStart"] | 0;
    sw.statePoolEnd = doc["statePoolEnd"] | 1;
  }
  sw.statePoolIdx = doc["statePoolIdx"] | sw.statePoolStart;

  strlcpy(sw.mqttPayload, statesPool[sw.statePoolIdx].c_str(), sizeof(sw.mqttPayload));
  strlcpy(sw.stateControl, statesPool[sw.statePoolIdx].c_str(), sizeof(sw.stateControl));
  sw.lastPrimaryGpioState = doc["lastPrimaryGpioState"] | true;
  sw.lastSecondaryGpioState = doc["lastSecondaryGpioState"] | true;
  sw.lastTimeChange = doc["lastTimeChange"] | 0;
  sw.percentageRequest = doc["percentageRequest"] | INT_MAX;
  sw.onTime = doc["onTime"] | 0;

  switchs.push_back(sw);
  if (persist)
  {
    requestSaveSwitchs = true;
  }
  addToDiscovery(&sw);
  doc["id"] = String(sw.id);
  stateSwitch(&sw,sw.stateControl);
  return doc;
}

void loadStoredSwitchs()
{
  if (SPIFFS.begin())
  {
    File file = SPIFFS.open(SWITCHES_CONFIG_FILENAME, "r+");
    const size_t CAPACITY = JSON_ARRAY_SIZE(switchs.size()) + switchs.size() * JSON_OBJECT_SIZE(33) + 1600;
    DynamicJsonDocument doc(CAPACITY);
    DeserializationError error = deserializeJson(doc, file);
    if (error)
    {
      logger(SWITCHES_TAG, "Default switches loaded.");
    }
    else
    {
      logger(SWITCHES_TAG, "Stored switches loaded.");
    }
    file.close();
    JsonArray ar = doc.as<JsonArray>();
    for (JsonVariant sw : ar)
    {
      updateSwitches(sw, error);
    }
  }
  SPIFFS.end();
}

void saveSwitchs()
{
  if (SPIFFS.begin())
  {
    File file = SPIFFS.open(SWITCHES_CONFIG_FILENAME, "w+");
    if (!file)
    {
      logger(SWITCHES_TAG, "Open Switches config file Error!");
    }
    else
    {
      const size_t CAPACITY = JSON_ARRAY_SIZE(switchs.size()) + switchs.size() * JSON_OBJECT_SIZE(33) + 2000;
      DynamicJsonDocument doc(CAPACITY);
      for (unsigned int i = 0; i < switchs.size(); i++)
      {
        JsonObject sdoc = doc.createNestedObject();
        SwitchT sw = switchs[i];
        sdoc["id"] = sw.id;
        sdoc["name"] = sw.name;
        sdoc["family"] = sw.family;
        sdoc["primaryGpio"] = sw.primaryGpio;
        sdoc["secondaryGpio"] = sw.secondaryGpio;
        sdoc["autoStateValue"] = String(sw.autoStateValue);

        sdoc["autoState"] = sw.autoState;
        sdoc["autoStateDelay"] = sw.autoStateDelay;
        sdoc["typeControl"] = sw.typeControl;
        sdoc["mode"] = sw.mode;
        sdoc["pullup"] = sw.pullup;
        sdoc["mqttRetain"] = sw.mqttRetain;
        sdoc["inverted"] = sw.inverted;
        sdoc["mqttCommandTopic"] = String(sw.mqttCommandTopic);
        sdoc["mqttStateTopic"] = String(sw.mqttStateTopic);
        if (String(sw.family).equals("cover"))
        {
          sdoc["mqttPositionCommandTopic"] = sw.mqttPositionCommandTopic;
          sdoc["mqttPositionStateTopic"] = sw.mqttPositionStateTopic;
          sdoc["percentageRequest"] = sw.percentageRequest;
          sdoc["lastPercentage"] = sw.lastPercentage;
          sdoc["positionControlCover"] = sw.positionControlCover; //COVER PERCENTAGE

          sdoc["secondaryGpioControl"] = sw.secondaryGpioControl;
          
        }
        sdoc["timeBetweenStates"] = sw.timeBetweenStates;
        sdoc["primaryGpioControl"] = sw.primaryGpioControl;

        sdoc["lastPrimaryGpioState"] = sw.lastPrimaryGpioState;
        sdoc["lastSecondaryGpioState"] = sw.lastSecondaryGpioState;

        sdoc["lastTimeChange"] = sw.lastTimeChange;

        sdoc["onTime"] = sw.onTime;
        sdoc["statePoolIdx"] = sw.statePoolIdx;
        sdoc["statePoolStart"] = sw.statePoolStart;
        sdoc["statePoolEnd"] = sw.statePoolEnd;
        sdoc["mqttPayload"] = sw.mqttPayload;
        sdoc["stateControl"] = sw.stateControl;
      }

      if (serializeJson(doc.as<JsonArray>(), file) == 0)
      {
        logger(SWITCHES_TAG, "Failed to write Switches Config into file");
      }
      else
      {
        logger(SWITCHES_TAG, "Switches Config stored.");
      }
    }
    file.close();
  }
  SPIFFS.end();
}

String getSwitchesConfigStatus()
{
  String object = "";
  if (SPIFFS.begin())
  {
    File file = SPIFFS.open(SWITCHES_CONFIG_FILENAME, "r+");

    if (!file)
    {
      return "[]";
    }
    while (file.available())
    {
      object += (char)file.read();
    }
    file.close();
  }
  SPIFFS.end();
  if (object.equals("") || object.equals("null"))
    return "[]";
  return object;
}
int findPoolIdx(String state, unsigned int start, unsigned int end)
{
  for (unsigned int p = start; p <= end; p++)
  {
    if (strcmp(state.c_str(), statesPool[p].c_str()) == 0)
    {
      return p;
      break;
    }
  }
  return start;
}

void mqttSwitchControl(String topic, String payload)
{
  for (unsigned int i = 0; i < switchs.size(); i++)
  {
    if (strcmp(switchs[i].mqttCommandTopic, topic.c_str()) == 0)
    {

      for (unsigned int p = switchs[i].statePoolStart; p <= switchs[i].statePoolEnd; p++)
      {
        if (strcmp(payload.c_str(), statesPool[p].c_str()) == 0)
        {
          switchs[i].statePoolIdx = p;
          stateSwitch(&switchs[i], statesPool[p]);
          return;
        }
      }
      stateSwitch(&switchs[i], payload);
    }
  }
}

void stateSwitch(SwitchT *switchT, String state)
{
  if (String(PAYLOAD_OPEN).equals(state))
  {
    strlcpy(switchT->stateControl, PAYLOAD_OPEN, sizeof(switchT->stateControl));
    strlcpy(switchT->mqttPayload, PAYLOAD_STATE_OPEN, sizeof(switchT->mqttPayload));
    if (switchT->typeControl == TYPE_RELAY)
    {
      pinMode(switchT->primaryGpioControl, OUTPUT);
      pinMode(switchT->secondaryGpioControl, OUTPUT);
      delay(DELAY_COVER_PROTECTION);
      digitalWrite(switchT->primaryGpioControl, switchT->inverted ? HIGH : LOW); //TURN OFF -> STOP
      delay(DELAY_COVER_PROTECTION);
      digitalWrite(switchT->secondaryGpioControl, switchT->inverted ? LOW : HIGH); //TURN ON -> OPEN REQUEST
      delay(DELAY_COVER_PROTECTION);
      digitalWrite(switchT->primaryGpioControl, switchT->inverted ? LOW : HIGH); //TURN ON -> EXECUTE REQUEST
    }
  }
  else if (String(PAYLOAD_STOP).equals(state))
  {
    strlcpy(switchT->stateControl, PAYLOAD_STOP, sizeof(switchT->stateControl));
    strlcpy(switchT->mqttPayload, PAYLOAD_STATE_STOP, sizeof(switchT->mqttPayload));
    if (switchT->typeControl == TYPE_RELAY)
    {
      pinMode(switchT->primaryGpioControl, OUTPUT);
      delay(DELAY_COVER_PROTECTION);
      digitalWrite(switchT->primaryGpioControl, switchT->inverted ? HIGH : LOW); //TURN OFF -> STOP
      delay(DELAY_COVER_PROTECTION);
    }
  }
  else if (String(PAYLOAD_CLOSE).equals(state))
  {
    strlcpy(switchT->stateControl, PAYLOAD_CLOSE, sizeof(switchT->stateControl));
    strlcpy(switchT->mqttPayload, PAYLOAD_STATE_CLOSE, sizeof(switchT->mqttPayload));
    if (switchT->typeControl == TYPE_RELAY)
    {
      pinMode(switchT->primaryGpioControl, OUTPUT);
      pinMode(switchT->secondaryGpioControl, OUTPUT);
      delay(DELAY_COVER_PROTECTION);
      digitalWrite(switchT->primaryGpioControl, switchT->inverted ? HIGH : LOW); //TURN OFF -> STOP
      delay(DELAY_COVER_PROTECTION);
      digitalWrite(switchT->secondaryGpioControl, switchT->inverted ? HIGH : LOW); //TURN OFF -> CLOSE REQUEST
      delay(DELAY_COVER_PROTECTION);
      digitalWrite(switchT->primaryGpioControl, switchT->inverted ? LOW : HIGH); //TURN ON -> EXECUTE REQUEST
    }
  }
  else if (String(PAYLOAD_ON).equals(state))
  {
    strlcpy(switchT->stateControl, PAYLOAD_ON, sizeof(switchT->stateControl));
    strlcpy(switchT->mqttPayload, PAYLOAD_ON, sizeof(switchT->mqttPayload));
    if (switchT->typeControl == TYPE_RELAY)
    {

      pinMode(switchT->primaryGpioControl, OUTPUT);
      digitalWrite(switchT->primaryGpioControl, switchT->inverted ? LOW : HIGH); //TURN ON
    }
  }
  else if (String(PAYLOAD_OFF).equals(state))
  { 
    strlcpy(switchT->stateControl, PAYLOAD_OFF, sizeof(switchT->stateControl));
    strlcpy(switchT->mqttPayload, PAYLOAD_OFF, sizeof(switchT->mqttPayload));
    if (switchT->typeControl == TYPE_RELAY)
    {
      logger(SWITCHES_TAG, switchT->name);
      logger(SWITCHES_TAG, state);
      logger(SWITCHES_TAG, String(switchT->primaryGpioControl));
      pinMode(switchT->primaryGpioControl, OUTPUT);
      digitalWrite(switchT->primaryGpioControl, switchT->inverted ? HIGH : LOW); //TURN OFF
    }
  }
  else if (String(PAYLOAD_LOCK).equals(state))
  {
    strlcpy(switchT->stateControl, PAYLOAD_LOCK, sizeof(switchT->stateControl));
    strlcpy(switchT->mqttPayload, PAYLOAD_STATE_LOCK, sizeof(switchT->mqttPayload));
    if (switchT->typeControl == TYPE_RELAY)
    {
      pinMode(switchT->primaryGpioControl, OUTPUT);
      digitalWrite(switchT->primaryGpioControl, switchT->inverted ? LOW : HIGH); //TURN ON
    }
  }
  else if (String(PAYLOAD_UNLOCK).equals(state))
  {
    strlcpy(switchT->stateControl, PAYLOAD_UNLOCK, sizeof(switchT->stateControl));
    strlcpy(switchT->mqttPayload, PAYLOAD_STATE_UNLOCK, sizeof(switchT->mqttPayload));
    if (switchT->typeControl == TYPE_RELAY)
    {
      pinMode(switchT->primaryGpioControl, OUTPUT);
      digitalWrite(switchT->primaryGpioControl, switchT->inverted ? LOW : HIGH); //TURN ON
    }
  }
  else if (isValidNumber(state))
  {
    switchT->percentageRequest = state.toInt();
    if (switchT->positionControlCover > switchT->percentageRequest)
    {
      switchT->statePoolIdx = 2;
      stateSwitch(switchT, statesPool[switchT->statePoolIdx]);
    }
    else
    {
      switchT->statePoolIdx = 4;
      stateSwitch(switchT, statesPool[switchT->statePoolIdx]);
    }
  }
  publishOnMqtt(switchT->mqttStateTopic, switchT->mqttPayload, switchT->mqttRetain);
  sendToServerEvents("states",String("{\"id\":\"")+String(switchT->id)+String("\",\"state\":\"")+String(switchT->mqttPayload)+String("\"}"));
  switchT->lastTimeChange = millis();
  switchT->statePoolIdx = findPoolIdx(switchT->stateControl,switchT->statePoolStart, switchT->statePoolEnd);
  requestSaveSwitchs= true;
}
void stateSwitchById(String id, String state){
  for (unsigned int i = 0; i < switchs.size(); i++)
  {
    if(strcmp(id.c_str(),switchs[i].id) == 0){
      stateSwitch(&switchs[i],state);
    }
  }
}
void stateSwitchByName(const char* name, String state,unsigned char value){
for (unsigned int i = 0; i < switchs.size(); i++)
  {
    if(strcasecmp(switchs[i].name,name)== 0){
      if(strcmp(FAMILY_COVER,switchs[i].family) == 0){
        stateSwitch(&switchs[i],strcmp(PAYLOAD_ON,state.c_str()) == 0 ? PAYLOAD_OPEN : PAYLOAD_CLOSE);
      }else if(strcmp(FAMILY_LIGHT,switchs[i].family) == 0 || strcmp(FAMILY_SWITCH,switchs[i].family) == 0){
      
        stateSwitch(&switchs[i],state);
      }
      
      
    }
  }
}

bool stateTimeout(SwitchT *sw)
{
  return (sw->autoState && strcmp(sw->autoStateValue, sw->stateControl) != 0 && (sw->lastTimeChange + sw->autoStateDelay) < millis());
}
boolean positionDone(SwitchT *sw, int currentPercentage)
{
  return strcmp( sw->family,FAMILY_COVER) == 0 && ((sw->positionControlCover == 0 || sw->positionControlCover == 100 || sw->percentageRequest == sw->positionControlCover) && currentPercentage > 0 && strcmp(sw->autoStateValue, sw->stateControl) != 0);
}
void loopSwitches()
{
  if(requestSaveSwitchs){
    requestSaveSwitchs = false;
    saveSwitchs();
  }
  for (unsigned int i = 0; i < switchs.size(); i++)
  {

    SwitchT *sw = &switchs[i];

    bool primaryValue = sw->primaryGpio == 99 ? true : digitalRead(sw->primaryGpio);
    bool secondaryValue = sw->secondaryGpio == 99 ? true : digitalRead(sw->secondaryGpio);
    unsigned long currentTime = millis();

    bool primaryGpioEvent = primaryValue != sw->lastPrimaryGpioState;
    bool secondaryGpioEvent = secondaryValue != sw->lastSecondaryGpioState;

    if ((primaryGpioEvent || secondaryGpioEvent) && currentTime - sw->lastTimeChange >= 5)
    {
      sw->lastPrimaryGpioState = primaryValue;
      sw->lastSecondaryGpioState = secondaryValue;
      sw->lastTimeChange = currentTime;

      switch (sw->mode)
      {
      case MODE_SWITCH:
        sw->statePoolIdx = (sw->statePoolIdx + 1 + sw->statePoolStart) % ((sw->statePoolEnd - sw->statePoolStart) + 1);
        stateSwitch(sw, statesPool[sw->statePoolIdx]);
        break;
      case MODE_PUSH:
        if (!primaryValue)
        { //PUSHED
          sw->statePoolIdx = (sw->statePoolIdx + 1 + sw->statePoolStart) % ((sw->statePoolEnd - sw->statePoolStart) + 1);
          stateSwitch(sw, statesPool[sw->statePoolIdx]);
        }
        break;
      case MODE_DUAL_SWITCH:
        if (primaryValue == true && secondaryValue == true)
        {
          sw->statePoolIdx = sw->statePoolIdx == 2 ? 3 : 5;
          stateSwitch(sw, statesPool[sw->statePoolIdx]);
        }
        else if (primaryGpioEvent && !primaryValue)
        {
          sw->statePoolIdx = 2;
          stateSwitch(sw, statesPool[sw->statePoolIdx]);
        }
        else if (secondaryGpioEvent && !secondaryValue)
        {
          sw->statePoolIdx = 4;
          stateSwitch(sw, statesPool[sw->statePoolIdx]);
        }
        break;
      default:
        break;
      }
    }

    int currentPercentage = 0;
    if (digitalRead(sw->primaryGpioControl))
    {
      if (sw->onTime == 0)
      {
        sw->onTime = millis();
      }
      long currentOffset = sw->timeBetweenStates - millis() - sw->onTime;
      int position = max(0l, (long)(sw->timeBetweenStates - currentOffset));
      currentPercentage = (position * 100) / sw->timeBetweenStates;
      if (digitalRead(sw->secondaryGpioControl))
      {
        sw->positionControlCover = max(0, sw->lastPercentage - currentPercentage);
      }
      else
      {
        sw->positionControlCover = min(100, currentPercentage + sw->lastPercentage);
      }
    }
    else
    {
      sw->onTime = 0;
      sw->lastPercentage = sw->positionControlCover;
    }

    if (stateTimeout(sw) || positionDone(sw, currentPercentage))
    {
      
      sw->statePoolIdx = findPoolIdx(sw->autoStateValue, sw->statePoolIdx, sw->statePoolEnd);
      sw->percentageRequest = INT_MAX;
      logger(SWITCHES_TAG,"AUTO STATE MODE set change switch to -> "+String(statesPool[sw->statePoolIdx]));
      stateSwitch(sw, statesPool[sw->statePoolIdx]);
    }
  }
}
