#pragma once
#include "rapidjson/document.h"
#include "SynchServer.h"
#include <set>

enum JsonTypesEnum
{
	StringJson,
	IntJson,
	BoolJson,
	ArrayJson
};

rapidjson::Document stringToJson(const std::string &messageString);
std::string jsonToString(const rapidjson::Document &messageJson);
rapidjson::Document constructJson(const std::vector<message_ptr> &incomingMessages, bool onlyNotProcessed=true);
rapidjson::Document constructJson(const std::vector<clientConnection_ptr> &clientsConnections);
rapidjson::Document constructJson(const std::vector<logRecord_ptr> &logsHistory, int recordsNumber, bool onlyNew);
std::string errorDescriptionToJsonString(const std::string &errorDescriptionString);
bool jsonArrayToPortsSettingsMap(const rapidjson::Document &serverParametersJson, 
	std::map<int, portSettings_ptr> &portsSettingsMap);
bool jsonArrayToSetOfUuidAckMessages(const rapidjson::Document &docJson, std::set<std::string> &messagesUuidSet);
rapidjson::Document convertStringToJsonDocument(const std::string &message);

template <typename T>
bool isJsonFieldValid(const T &objJson, const char* fieldName, JsonTypesEnum jsonType)
{
	if (!objJson.HasMember(fieldName))
		return false;

	switch (jsonType)
	{
	case StringJson:
		return objJson[fieldName].IsString();
	case IntJson:
		return objJson[fieldName].IsInt();
	case BoolJson:
		return objJson[fieldName].IsBool();
	case ArrayJson:
		return objJson[fieldName].IsArray();
	default:
		return true;
	}
}