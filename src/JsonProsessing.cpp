#include <boost/asio.hpp>

#include "JsonProsessing.h"
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include <locale>
#include <codecvt>

using namespace rapidjson;

Document constructJson(const std::vector<message_ptr> &incomingMessages, bool onlyNotProcessed) {
	Document document;
	document.SetObject();
	auto& allocator = document.GetAllocator();
	Value json_val;

	Value arrJson(kArrayType);
	for (auto const& message : incomingMessages) {
		if (message->isMessageInProcessing() && onlyNotProcessed)
			continue;

		Value arrRow(kObjectType);

		std::string messageUuidString = message->messageUuidString;
		json_val.SetString(messageUuidString.c_str(), messageUuidString.length(), allocator);
		arrRow.AddMember("messageUuidString", json_val, document.GetAllocator());

		std::string messageBody = message->messageBody;
		json_val.SetString(messageBody.c_str(), messageBody.length(), allocator);
		arrRow.AddMember("messageBody", json_val, document.GetAllocator());

		std::string clientSocketUuidString = message->getClientSocketUuidStringRef();
		json_val.SetString(clientSocketUuidString.c_str(), clientSocketUuidString.length(), allocator);
		arrRow.AddMember("clientSocketUuidString", json_val, document.GetAllocator());
		
		arrRow.AddMember("portNumber", Value().SetInt(message->portNumber), document.GetAllocator());
		
		std::string remoteIP = message->remoteIP;
		json_val.SetString(remoteIP.c_str(), remoteIP.length(), allocator);
		arrRow.AddMember("remoteIP", json_val, document.GetAllocator());

		arrJson.PushBack(arrRow, allocator);
	}
	document.AddMember("incomingMessages", arrJson, allocator);

	return document;
}

Document constructJson(const std::vector<clientConnection_ptr> &clientsConnections) {
	Document document;
	document.SetObject();
	auto& allocator = document.GetAllocator();
	Value json_val;

	Value arrJson(kArrayType);
	for (auto const& clientConnection : clientsConnections) {
		Value arrRow(kObjectType);

		std::string clientSocketUuidString = clientConnection->getClientSocketUuidString();
		json_val.SetString(clientSocketUuidString.c_str(), clientSocketUuidString.length(), allocator);
		arrRow.AddMember("clientSocketUuidString", json_val, document.GetAllocator());

		std::string lastActivityTime = clientConnection->getLastActivityTimeInStringFormat();
		json_val.SetString(lastActivityTime.c_str(), lastActivityTime.length(), allocator);
		arrRow.AddMember("lastActivityTime", json_val, document.GetAllocator());

		arrRow.AddMember("portNumber", Value().SetInt(clientConnection->getPortNumber()), document.GetAllocator());
		arrRow.AddMember("accepted", Value().SetBool(clientConnection->getConnectionAccepted()), document.GetAllocator());

		arrJson.PushBack(arrRow, allocator);
	}
	document.AddMember("clientsConnections", arrJson, allocator);
	
	return document;
}

Document constructJson(const std::vector<logRecord_ptr> &logHistory, int recordsNumber, bool onlyNew) {
	Document document;
	document.SetObject();
	auto& allocator = document.GetAllocator();
	Value json_val;

	Value arrJson(kArrayType);
	int recordsCount = 0;
	for (auto it = logHistory.rbegin(); it != logHistory.rend(); ++it) {
		Value arrRow(kObjectType);

		if (onlyNew && (*it)->isLogRecordProcessingCompleted())
			continue;

		std::string typeString = (*it)->getTypeInStringFormat();
		json_val.SetString(typeString.c_str(), typeString.length(), allocator);
		arrRow.AddMember("type", json_val, document.GetAllocator());

		std::string logRecordUuidString = (*it)->logRecordUuidString;
		json_val.SetString(logRecordUuidString.c_str(), logRecordUuidString.length(), allocator);
		arrRow.AddMember("logRecordUuidString", json_val, document.GetAllocator());

		std::string timePoint = (*it)->getTimePointInStringFormat();
		json_val.SetString(timePoint.c_str(), timePoint.length(), allocator);
		arrRow.AddMember("time", json_val, document.GetAllocator());

		std::string logBody = (*it)->logBody;
		json_val.SetString(logBody.c_str(), logBody.length(), allocator);
		arrRow.AddMember("body", json_val, document.GetAllocator());

		arrJson.PushBack(arrRow, allocator);

		recordsCount++;
		if (recordsCount == recordsNumber)
			break;
	}
	document.AddMember("logHistory", arrJson, allocator);

	return document;
}

std::string errorDescriptionToJsonString(const std::string & errorDescriptionString)
{
	const bool successfully = errorDescriptionString.empty();
	
	Document document;
	document.SetObject();
	auto& allocator = document.GetAllocator();
	Value json_val;

	document.AddMember("successfully", Value().SetBool(successfully), allocator);
	if (!successfully) {
		json_val.SetString(errorDescriptionString.c_str(), errorDescriptionString.length(), allocator);
		document.AddMember("errorDescription", json_val, allocator);
	}

	return jsonToString(document);
}

bool jsonArrayToPortsSettingsMap(const rapidjson::Document &serverParametersJson, 
	std::map<std::pair<int, std::string>, portSettings_ptr> &portsSettingsMap) {
	bool successfully = true;
	for (const auto& portSettingJson : serverParametersJson["ports"].GetArray()) {
		if (isJsonFieldValid(portSettingJson, "portType", StringJson)
			&& isJsonFieldValid(portSettingJson, "portNumber", IntJson)
			&& isJsonFieldValid(portSettingJson, "isLinkToRemoteServer", BoolJson)
			&& isJsonFieldValid(portSettingJson, "remoteIP", StringJson)
			&& isJsonFieldValid(portSettingJson, "delayReadingFromSocket", IntJson)
			&& isJsonFieldValid(portSettingJson, "delayMessageSending", IntJson)
			&& isJsonFieldValid(portSettingJson, "allowedTimeNoActivity", IntJson)) {

			const std::string portTypeString = portSettingJson["portType"].GetString();
			const bool isLinkToRemoteServer = portSettingJson["isLinkToRemoteServer"].GetBool();
			const std::string remoteIP = portSettingJson["remoteIP"].GetString();
			const int portNumber = portSettingJson["portNumber"].GetInt();
			const int delayReadingFromSocket = portSettingJson["delayReadingFromSocket"].GetInt();
			const int delayMessageSending = portSettingJson["delayMessageSending"].GetInt();
			const int allowedTimeNoActivity = portSettingJson["allowedTimeNoActivity"].GetInt();

			portSettings_ptr portSetting_(
				new PortSettings(portNumber, isLinkToRemoteServer, remoteIP, portTypeString, delayReadingFromSocket, delayMessageSending, allowedTimeNoActivity)
			);
			portsSettingsMap.insert(std::make_pair(std::make_pair(portNumber, remoteIP), portSetting_));
		}
		else
		{
			successfully = false;
			break;
		}
	}
	
	return successfully;
}

bool jsonArrayToSetOfUuidAckMessages(const rapidjson::Document &docJson, std::set<std::string> &uuidSet) {
	for (const auto& uuidJson : docJson["ackMessagesUuid"].GetArray()) {
		std::string messageUuidString = uuidJson["messageUuidString"].GetString();
		uuidSet.insert(messageUuidString);
	}
	
	return true;
}

std::string jsonToString(const rapidjson::Document &jsonDocument) {
	StringBuffer buffer;
	Writer<StringBuffer> writer(buffer);
	jsonDocument.Accept(writer);
	std::string jsonString = buffer.GetString();

	return jsonString;
}

Document stringToJson(const std::string &message) {
	Document document = convertStringToJsonDocument(message);

	return document;
}

Document convertStringToJsonDocument(const std::string &message) {
	Document document;
	document.Parse(message.c_str());

	return document;
}