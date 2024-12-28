#include <boost/asio.hpp>

#include "SynchServer.h"
#include "JsonProsessing.h"
#include <algorithm>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/date_time/posix_time/posix_time_io.hpp>
#include "boost/format.hpp"
#include <boost/locale.hpp>
#include <iostream>
#include <fstream>
#include <thread>

//---------------------------------------------------------------------------//
// SynchServer methods
//---------------------------------------------------------------------------//

void SynchClientServer::setServerParametersImpl(Biterp::CallContext& ctx) {	
	const std::string parametersString = ctx.stringParamUtf8();
	
	rapidjson::Document serverParametersJson = stringToJson(parametersString);

	std::unique_lock<std::mutex> lk(classVariables_mutex);
	bool successfully = true;
	if (serverParametersJson.IsObject()
		&& isJsonFieldValid(serverParametersJson, "ip", StringJson)
		&& isJsonFieldValid(serverParametersJson, "loggingRequired", BoolJson)
		&& isJsonFieldValid(serverParametersJson, "logFileName", StringJson)
		&& isJsonFieldValid(serverParametersJson, "memoryCleaningFrequency", IntJson)
		&& isJsonFieldValid(serverParametersJson, "serverTerminationSignal", StringJson)
		&& isJsonFieldValid(serverParametersJson, "ports", ArrayJson)) {
	
		std::string ipString = serverParametersJson["ip"].GetString();
		if (ipString.empty()) {
			throw Biterp::Error("ip cannot be empty!");
			return;
		}
		ip = boost::asio::ip::address_v4::from_string(ipString);
		loggingRequired = serverParametersJson["loggingRequired"].GetBool();
		logFileName = serverParametersJson["logFileName"].GetString();
		loggingToFile = ! logFileName.empty();
		memoryCleaningFrequency = serverParametersJson["memoryCleaningFrequency"].GetInt();
		serverTerminationSignal = serverParametersJson["serverTerminationSignal"].GetString();

		serverNeedToTerminate.store(false, std::memory_order_seq_cst);
		allThreadsNeedToTerminate.store(false, std::memory_order_seq_cst);

		successfully = jsonArrayToPortsSettingsMap(serverParametersJson, portsSettings);

		state = READY;
	}
	else successfully = false;

	if (! successfully)
		throw Biterp::Error("invalid json parameters");

}

void SynchClientServer::listenImpl(Biterp::CallContext& ctx)
{
	if (getLoggingRequired())
		addLogInFile("DEBUG: start listenImpl()");
	
	std::string errorDescription;
	
	if (state == RUNNING)
		throw Biterp::Error("server is already running");

	// if at least one port (internal) is already open, then the server cannot be started
	
	for (auto const& portSettingPair : portsSettings) {
		portSettings_ptr setting_ptr = portSettingPair.second;
		if (! setting_ptr->isLinkToRemoteServer && isPortAlreadyOpened(setting_ptr->portNumber, false)) {
			std::string portOpenError = std::to_string(setting_ptr->portNumber) + " port is already opened; ";
			errorDescription.append(portOpenError);
		}		
	}
	
	if (errorDescription.empty()) {
		std::unique_lock<std::mutex> lk(connections_mutex);
		// creatå accept connections threads. One for each port (internal)
		for (auto const& portSettingPair : portsSettings) {
			portSettings_ptr setting_ptr = portSettingPair.second;
			if (!setting_ptr->isLinkToRemoteServer) {		
				threads.push_back(std::thread(&SynchClientServer::accept_connections_thread, this, setting_ptr));
			}
		}
		threads.push_back(std::thread(&SynchClientServer::handle_connections_thread, this));
		threads.push_back(std::thread(&SynchClientServer::clean_thread, this));
		threads.push_back(std::thread(&SynchClientServer::reconnectToRemoteServers_thread, this));
		terminate_thread.push_back(std::thread(&SynchClientServer::checkForTerminate_thread, this));
	
		state = RUNNING;
	}

	// establish connections to remote servers

	for (auto const& portSettingPair : portsSettings) {
		portSettings_ptr setting_ptr = portSettingPair.second;
		if (setting_ptr->isLinkToRemoteServer) {
			clientConnection_ptr connectionToRemoteServer = getCreateClientConnectionToRemoteServer(setting_ptr);
			if (!connectionToRemoteServer) {
				std::string portOpenError = std::to_string(setting_ptr->portNumber) + " can not establish the connection to remote server";
				if (loggingRequired)
					addLog(portOpenError);
			}
		}
	}

	std::string errorDescriptionJsonString = errorDescriptionToJsonString(errorDescription);

	ctx.setStringResult(u16Converter.from_bytes(errorDescriptionJsonString));
}

void SynchClientServer::getMessagesFromClientsWhenArriveImpl(Biterp::CallContext& ctx)
{
	tVariant* outdata = ctx.skipParam();
	ctx.setEmptyResult(outdata);
	int checkFrequency = ctx.intParam();
	int timeout = ctx.intParam();
	
	const boost::posix_time::ptime lastPulseTime = boost::posix_time::second_clock::local_time();

	bool hasNewMessages = false;
	std::string incomingMessagesJsonString;
    while (true)
    {
		std::this_thread::sleep_for(std::chrono::milliseconds(checkFrequency));
        std::unique_lock<std::mutex> lk(incomingMessages_mutex);
        if (!allIncomingMessagesTakenInProcessing()) {
            // receive all new (not takenInProcessing) incoming messages from clients
            rapidjson::Document messagesJson = constructJson(incomingMessages);
            std::string messagesJsonString = jsonToString(messagesJson);
            for (auto const& message_ptr : incomingMessages) {
                message_ptr->takeMesssageInProsessing();
            }

            incomingMessagesJsonString = messagesJsonString;
			hasNewMessages = true;
            break;
        }

		const boost::posix_time::ptime now = boost::posix_time::second_clock::local_time();
		const long long secondPassed = (now - lastPulseTime).total_milliseconds();
        if (secondPassed > timeout)
            break;
    }

	if (serverNeedToTerminate.load(std::memory_order_seq_cst)) {
		// waiting for this instance of the server to process the terminate message
		std::this_thread::sleep_for(std::chrono::milliseconds(2000));
		ctx.setBoolResult(false);
	}
	
	std::string incomingMessagesJsonStringLocale = boost::locale::conv::utf_to_utf<char>(incomingMessagesJsonString.c_str());

	ctx.setStringResult(u16Converter.from_bytes(incomingMessagesJsonStringLocale), outdata);
	ctx.setBoolResult(true);
}

void SynchClientServer::getMessagesFromClientsImpl(Biterp::CallContext& ctx)
{
	tVariant* outdata = ctx.skipParam();
	ctx.setEmptyResult(outdata);
	
	std::unique_lock<std::mutex> lk(incomingMessages_mutex);

	std::string incomingMessagesJsonString;	
	if (incomingMessages.empty()) {
		incomingMessagesJsonString = "";
	}
	else {
		// receive all new (not takenInProcessing) incoming messages from clients
		rapidjson::Document messagesJson = constructJson(incomingMessages);
		std::string messagesJsonString = jsonToString(messagesJson);
		for (auto const& message_ptr : incomingMessages) {
			message_ptr->takeMesssageInProsessing();
		}
		incomingMessagesJsonString = messagesJsonString;
	}

	std::string incomingMessagesJsonStringLocale = boost::locale::conv::utf_to_utf<char>(incomingMessagesJsonString.c_str());

	ctx.setStringResult(u16Converter.from_bytes(incomingMessagesJsonStringLocale), outdata);
	ctx.setBoolResult(true);
}

void SynchClientServer::ackMessagesReceiptImpl(Biterp::CallContext& ctx)
{
	const std::string messagesUuidJsonString = ctx.stringParamUtf8();
	
	bool successfully = true;
	
	rapidjson::Document messagesUuidJson = stringToJson(messagesUuidJsonString);
	if (messagesUuidJson.IsObject()
		&& isJsonFieldValid(messagesUuidJson, "ackMessagesUuid", ArrayJson)) {
		std::set<std::string> messagesUuidSet;
		successfully = jsonArrayToSetOfUuidAckMessages(messagesUuidJson, messagesUuidSet);
		
		if (successfully) {
			const int setSize = static_cast<int>(messagesUuidSet.size());

			std::unique_lock<std::mutex> lk(incomingMessages_mutex);
			int messageCount = 0;
			for (auto const& message_ptr : incomingMessages) {
				const bool isMessageUuidInSet = messagesUuidSet.find(message_ptr->messageUuidString) != messagesUuidSet.end();
				if (isMessageUuidInSet) {
					message_ptr->completeProsessingMesssage();
					messageCount++;
				}
				if (messageCount == setSize)
					break;
			}
		}
	}
	else successfully = false;

	if (! successfully)
		throw Biterp::Error("invalid json parameters");
}

void SynchClientServer::sendMessageToClientImpl(Biterp::CallContext& ctx)
{
	const std::string outgoingMessagesJsonString = ctx.stringParamUtf8();

	bool successfully = true;
	
	rapidjson::Document messageJson = stringToJson(outgoingMessagesJsonString);
	if (messageJson.IsObject()
		&& isJsonFieldValid(messageJson, "messageUuidString", StringJson)
		&& isJsonFieldValid(messageJson, "messageBody", StringJson)
		&& isJsonFieldValid(messageJson, "portNumber", IntJson)
		&& isJsonFieldValid(messageJson, "clientSocketUuidString", StringJson)) {

		const std::string messageUuidString = messageJson["messageUuidString"].GetString();
		const std::string messageBody = messageJson["messageBody"].GetString();
		const int portNumber = messageJson["portNumber"].GetInt();
		const std::string clientSocketUuidString = messageJson["clientSocketUuidString"].GetString();
		const std::string remoteIP;

		message_ptr newMessage_(
			new Message(OUTGOING,
				messageUuidString,
				messageBody,
				portNumber,
				remoteIP,
				clientSocketUuidString)
		);

		std::unique_lock<std::mutex> lk(outgoingMessages_mutex);
		outgoingMessages.push_back(newMessage_);
	}
	else successfully = false;

	if (! successfully)
		throw Biterp::Error("invalid json parameters");
}

void SynchClientServer::sendMessageToRemoteServerImpl(Biterp::CallContext& ctx)
{
	const std::string outgoingMessagesJsonString = ctx.stringParamUtf8();
	
	bool successfully = true;

	rapidjson::Document messageJson = stringToJson(outgoingMessagesJsonString);
	if (messageJson.IsObject()
		&& isJsonFieldValid(messageJson, "messageUuidString", StringJson)
		&& isJsonFieldValid(messageJson, "messageBody", StringJson)
		&& isJsonFieldValid(messageJson, "portNumber", IntJson)
		&& isJsonFieldValid(messageJson, "remoteIP", StringJson)) {
		
		const std::string messageUuidString = messageJson["messageUuidString"].GetString();
		const std::string messageBody = messageJson["messageBody"].GetString();
		const std::string remoteIP = messageJson["remoteIP"].GetString();
		const int portNumber = messageJson["portNumber"].GetInt();
		
		portSettings_ptr portSettings = getPortSettings(portNumber, remoteIP);
		if (! portSettings) {
			std::string errorMessage = std::to_string(portNumber) + " can not find any portSettings by port number";
			if (loggingRequired)
				addLog(errorMessage);
			throw Biterp::Error(errorMessage);
		}

		clientConnection_ptr connectionToRemoteServer = getCreateClientConnectionToRemoteServer(portSettings);
		if (! connectionToRemoteServer) {
			std::string errorMessage = std::to_string(portNumber) + " can not establish the connection to remote server. Message has been ignored";
			if (loggingRequired)
				addLog(errorMessage);
			throw Biterp::Error(errorMessage);
		}
				
		std::string clientSocketUuidString = connectionToRemoteServer->getClientSocketUuidString();
		message_ptr newMessage_(
			new Message(OUTGOING,
				messageUuidString,
				messageBody,
				portNumber,
				remoteIP,
				clientSocketUuidString)
		);
		std::unique_lock<std::mutex> lk(outgoingMessages_mutex);
		outgoingMessages.push_back(newMessage_);
	}
	else 
		successfully = false;

	if (! successfully)
		throw Biterp::Error("invalid json parameters");
}

void SynchClientServer::getClientsStateImpl(Biterp::CallContext& ctx)
{
	std::string clientsConnectionsJsonString;

	std::unique_lock<std::mutex> lk(connections_mutex);
	rapidjson::Document clientsConnectionsJson = constructJson(clientsConnections);
	clientsConnectionsJsonString = jsonToString(clientsConnectionsJson);

	ctx.setStringResult(u16Converter.from_bytes(clientsConnectionsJsonString));
}

void SynchClientServer::stopServerImpl(Biterp::CallContext& ctx)
{	
	stopServer_();
}

void SynchClientServer::stopServer_()
{
	if (getLoggingRequired())
		addLogInFile("DEBUG: start stopServer_()");
	
	if (state != RUNNING)
		return;

	state = STOPED;

	allThreadsNeedToTerminate.store(true, std::memory_order_seq_cst);

	std::this_thread::sleep_for(std::chrono::milliseconds(1000));

	// establish temporary connections on all server ports to exit loops in the accept_connections_thread thread, 
	// since the acceptor.accept () method is blocking
	for (auto const& portSettingPair : portsSettings) {
		portSettings_ptr setting_ptr = portSettingPair.second;
		isPortAlreadyOpened(setting_ptr->getPortNumber(), false);
	}
	for (auto& th : threads) th.join();
	for (auto& th : terminate_thread) th.join();

	clientsConnections.clear();
	incomingMessages.clear();
	outgoingMessages.clear();

	if (loggingRequired)
		addLog("server has been stoped");
}

void SynchClientServer::sendTerminationSignalToRunningInstanceOfServerImpl(Biterp::CallContext& ctx)
{	
	for (auto const& portSettingPair : portsSettings) {
		portSettings_ptr setting_ptr = portSettingPair.second;
		
		if (!setting_ptr->isLinkToRemoteServer) {
			const bool isOpened = isPortAlreadyOpened(setting_ptr->portNumber, true);
		}
	}

	// waiting for another instance of the server to process the message
	std::this_thread::sleep_for(std::chrono::milliseconds(2000));
}

void SynchClientServer::terminateServer()
{
	if (getLoggingRequired())
		addLogInFile("DEBUG: start terminateServer()");
	
	std::unique_lock<std::mutex> lk(stopServer_mutex);
	
	stopServer_();

	std::this_thread::sleep_for(std::chrono::milliseconds(3000));

	service_io.post([this]() {
		work_io.reset(); // let io_service run out of work
	});
	service_io.stop();
	

	if (getLoggingRequired())
		addLogInFile("DEBUG: done terminateServer()");
}

void SynchClientServer::addLog(const std::string &recordBody)
{
	addRecordInLogsHistory(recordBody);
	addLogInFile(recordBody);
}

void SynchClientServer::addRecordInLogsHistory(const std::string &recordBody)
{
	logRecord_ptr newLogRecord_(
		new LogRecord(recordBody)
	);
	std::unique_lock<std::mutex> lk(logHistory_mutex);
	logHistory.push_back(newLogRecord_);
}

void SynchClientServer::addLogInFile(const std::string & record)
{
	if (loggingToFile) {
		std::unique_lock<std::mutex> lk(logHistory_mutex);

		std::ofstream out(logFileName, std::ios_base::app);
		out << timeToISOString(boost::posix_time::microsec_clock::local_time()) << ' ';
		out << record << '\n';
		out.close();
	}
}

void SynchClientServer::addLogInFile(const char* & record)
{
	if (loggingToFile) {
		std::unique_lock<std::mutex> lk(logHistory_mutex);

		std::ofstream out(logFileName, std::ios_base::app);
		out << timeToISOString(boost::posix_time::microsec_clock::local_time()) << ' ';
		out << record << '\n';
		out.close();
	}
}

void SynchClientServer::getLastRecordsFromLogHistoryImpl(Biterp::CallContext& ctx)
{
	int recordsNumber = ctx.intParam();
	bool onlyNew = ctx.boolParam();
	
	std::string logsJsonString;
	
	std::unique_lock<std::mutex> lk(logHistory_mutex);
	if (! logHistory.empty()) {
		rapidjson::Document logsJson = constructJson(logHistory, recordsNumber, onlyNew);
		logsJsonString = jsonToString(logsJson);

		prosessLastRecordsFromLogHistory(logHistory, recordsNumber, onlyNew);
	}

	std::string logsJsonStringLocale = boost::locale::conv::utf_to_utf<char>(logsJsonString.c_str());

	ctx.setStringResult(u16Converter.from_bytes(logsJsonStringLocale));
}

bool SynchClientServer::getLoggingRequired()
{
	std::unique_lock<std::mutex> lk(classVariables_mutex);
	return loggingRequired;
}

void SynchClientServer::setLoggingRequired(bool _loggingRequired)
{
	std::unique_lock<std::mutex> lk(classVariables_mutex);
	loggingRequired = _loggingRequired;
}

void SynchClientServer::accept_connections_thread(const portSettings_ptr & portSettings) {
	if (loggingRequired)
		addLog("run accept_connections_thread");

	boost::asio::ip::tcp::acceptor acceptor(service_io, 
		boost::asio::ip::tcp::endpoint(ip, portSettings->getPortNumber()));

	while (true) {
		if (isThreadInterrupted())
			break;
		
		synchServer_Wptr weak_ptr_this(shared_from_this());
		clientConnection_ptr newConnection_(
			new ClientConnection(boost::ref(service_io), portSettings, weak_ptr_this)
		);
		
		boost::system::error_code err;
		acceptor.accept(newConnection_->getSocket(), err);
		if (err) 
			return;
		newConnection_->acceptConnection();

		std::unique_lock<std::mutex> lk_connections(connections_mutex);
		clientsConnections.push_back(newConnection_);
	}

	if (loggingRequired)
		addLog("finish accept_connections_thread");
}

void SynchClientServer::handle_connections_thread() {
	if (loggingRequired)
		addLog("run handle_connections_thread");
	
	while (true) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
		std::unique_lock<std::mutex> lk_connections(connections_mutex);
		if (isThreadInterrupted()) 
			break;
		
		for (auto const& clientConnection_ptr : clientsConnections) {
			readMessageFromConnection(clientConnection_ptr);
			sendMessagesToConnection(clientConnection_ptr);
		}
	}

	if (loggingRequired)
		addLog("finish handle_connections_thread");
}

void SynchClientServer::clean_thread()
{
	while (true) {
		std::this_thread::sleep_for(std::chrono::milliseconds(memoryCleaningFrequency));
		if (getLoggingRequired())
			addLogInFile("DEBUG: start clean_thread()");

		std::unique_lock<std::mutex> lk_incomingMessages(incomingMessages_mutex,	std::defer_lock);
		std::unique_lock<std::mutex> lk_outgoingMessages(outgoingMessages_mutex,	std::defer_lock);
		std::unique_lock<std::mutex> lk_connections(connections_mutex,				std::defer_lock);
		std::lock(lk_incomingMessages, lk_outgoingMessages, lk_connections);
		if (isThreadInterrupted())
			break;

		// close connections that timed out
		clientsConnections.erase(
			std::remove_if(
				clientsConnections.begin(),
				clientsConnections.end(),
				boost::bind(&ClientConnection::isTimedOut, _1)
			),
			clientsConnections.end()
		);
		lk_connections.unlock();

		incomingMessages.erase(
			std::remove_if(
				incomingMessages.begin(),
				incomingMessages.end(),
				boost::bind(&Message::isMessageProcessingCompleted, _1)
			),
			incomingMessages.end()
		);
		lk_incomingMessages.unlock();
	
		outgoingMessages.erase(
			std::remove_if(
				outgoingMessages.begin(),
				outgoingMessages.end(),
				boost::bind(&Message::isMessageProcessingCompleted, _1)
			),
			outgoingMessages.end()
		);
		lk_outgoingMessages.unlock();

		if (getLoggingRequired())
			addLogInFile("DEBUG: done clean_thread()");
	}
}

void SynchClientServer::checkForTerminate_thread()
{
	while (true) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1000));
		if (isThreadInterrupted() || state == STOPED)
			break;
		
		std::unique_lock<std::mutex> lk(classVariables_mutex);
		if (serverNeedToTerminate.load(std::memory_order_seq_cst)) {
			terminateServer();
		}
	}
}

void SynchClientServer::reconnectToRemoteServers_thread()
{
	while (true) {
		std::this_thread::sleep_for(std::chrono::milliseconds(10000));
		if (isThreadInterrupted())
			break;

		for (auto const& portSettingPair : portsSettings) {
			portSettings_ptr setting_ptr = portSettingPair.second;
			if (setting_ptr->isLinkToRemoteServer) {
				clientConnection_ptr connectionToRemoteServer = getCreateClientConnectionToRemoteServer(setting_ptr);
			}
		}
	}
}

bool SynchClientServer::allIncomingMessagesTakenInProcessing()
{
	return std::find_if(
		incomingMessages.begin(), 
		incomingMessages.end(), 
		! boost::bind(&Message::isMessageInProcessing, _1)
	) == incomingMessages.end();
}

void SynchClientServer::readMessageFromConnection(const clientConnection_ptr & clientConnection)
{
	bool haveNewData = clientConnection->readDataFromSocket();
	if (haveNewData) {
		std::string bufferString = clientConnection->getBufferString();
		if (isServerTerminationSignal(bufferString)) {
			std::unique_lock<std::mutex> lk(classVariables_mutex);
			serverNeedToTerminate.store(true, std::memory_order_seq_cst);
		}

		message_ptr newMessage_(
			new Message(INCOMMING,
				generateNewUuidString(),
				bufferString,
				clientConnection->getPortNumber(),
				clientConnection->getRemoteIP(),
				clientConnection->getClientSocketUuidString())
		);
		std::unique_lock<std::mutex> lk(incomingMessages_mutex);
		incomingMessages.push_back(newMessage_);
	}
}

void SynchClientServer::sendMessagesToConnection(const clientConnection_ptr & clientConnection)
{
	std::unique_lock<std::mutex> lk(outgoingMessages_mutex);
	
	const std::string & clientSocketUuidString = clientConnection->getClientSocketUuidString();
	for (auto const& message_ptr : outgoingMessages) {
		const std::string & m_clientSocketUuidString = message_ptr->getClientSocketUuidStringRef();
		bool isMessageForThisConnection = clientSocketUuidString == m_clientSocketUuidString;

		if (isMessageForThisConnection
			&& !message_ptr->isMessageProcessingCompleted()) {		
			message_ptr->takeMesssageInProsessing();
			clientConnection->sendMessage(message_ptr);
			message_ptr->completeProsessingMesssage();
		}
	}
}

bool SynchClientServer::isPortAlreadyOpened(int portNumber, bool needToTerminate)
{
	boost::asio::ip::tcp::endpoint ep(ip, portNumber);
	boost::asio::ip::tcp::socket socket(service_io);
	boost::system::error_code err;
	socket.connect(ep, err);
	if (err)
		return false;
	else {
		if (needToTerminate) {
			socket.write_some(boost::asio::buffer(serverTerminationSignal), err);
		}
		socket.close();
		return true;
	}
}

void SynchClientServer::prosessLastRecordsFromLogHistory(const std::vector<logRecord_ptr> &logHistory, int recordsNumber, bool onlyNew)
{
	int recordsCount = 0;
	for (auto it = logHistory.rbegin(); it != logHistory.rend(); ++it) {
		if (onlyNew && (*it)->isLogRecordProcessingCompleted())
			continue;

		(*it)->completeProsessingLogRecord();

		recordsCount++;
		if (recordsCount == recordsNumber)
			break;
	}
}

const clientConnection_ptr SynchClientServer::getCreateClientConnectionToRemoteServer(const portSettings_ptr & portSettings)
{
	if (getLoggingRequired())
		addLogInFile("DEBUG: start getCreateClientConnectionToRemoteServer()");
	
	if (portSettings->getRemoteIP().empty()) {
		return std::shared_ptr<ClientConnection>(nullptr);
	}

	clientConnection_ptr existingConnection = getExistingClientConnectionToRemoteServer(portSettings);
	if (existingConnection) {
		return existingConnection;
	}

	// can not find existing connection. Create new one and try to establish connection

	synchServer_Wptr weak_ptr_this(shared_from_this());
	clientConnection_ptr newConnection_(
		new ClientConnection(boost::ref(service_io), portSettings, weak_ptr_this)
	);

	bool successfully = newConnection_->reconnectSocketRemoteServer();
	if (successfully) {
		newConnection_->acceptConnection();

		std::unique_lock<std::mutex> lk_connections(connections_mutex);
		clientsConnections.push_back(newConnection_);
		
		if (getLoggingRequired())
			addLogInFile("DEBUG: done getCreateClientConnectionToRemoteServer()");

		return newConnection_;		
	}
	else {
		if (getLoggingRequired())
			addLogInFile("DEBUG: done getCreateClientConnectionToRemoteServer()");

		return std::shared_ptr<ClientConnection>(nullptr);
	}
}

const clientConnection_ptr SynchClientServer::getExistingClientConnectionToRemoteServer(const portSettings_ptr& portSettings)
{
	std::unique_lock<std::mutex> lk_connections(connections_mutex);

	int portNumber = portSettings->getPortNumber();
	std::string remoteIP = portSettings->getRemoteIP();
	for (auto it = clientsConnections.rbegin(); it != clientsConnections.rend(); ++it) {
		if ((*it)->getPortNumber() == portNumber && (*it)->getRemoteIP() == remoteIP) {
			return *it;
		}
	}

	return std::shared_ptr<ClientConnection>(nullptr);
}

const portSettings_ptr SynchClientServer::getPortSettings(int portNumber, std::string remoteIP)
{
	std::map<std::pair<int, std::string>, portSettings_ptr>::iterator pos = portsSettings.find(std::make_pair(portNumber, remoteIP));
	if (pos == portsSettings.end()) {
		return std::shared_ptr<PortSettings>(nullptr);
	}
	else {
		return pos->second;
	}
}

bool SynchClientServer::isServerTerminationSignal(const std::string & record)
{
	return record == serverTerminationSignal;
}

bool SynchClientServer::isThreadInterrupted() {
	
	bool interrupted = allThreadsNeedToTerminate.load(std::memory_order_seq_cst);

	return interrupted;
}

//---------------------------------------------------------------------------//
// ClientConnection methods
//---------------------------------------------------------------------------//

ClientConnection::ClientConnection(boost::asio::io_service &service_io, 
	const portSettings_ptr & portSettings, const synchServer_Wptr &server)
	: socket_(service_io), server_(server){
	clientSocketUuidString_ = generateNewUuidString();
	portNumber_ = portSettings->portNumber;
	isLinkToRemoteServer_ = portSettings->isLinkToRemoteServer;
	remoteIP_ = portSettings->remoteIP;
	delayReadingFromSocket_ = portSettings->delayReadingFromSocket;
	delayMessageSending_ = portSettings->delayMessageSending;
	allowedTimeNoActivity_ = portSettings->allowedTimeNoActivity;
	connectionAccepted_ = false;

	refreshLastActivityTime();

	printConstructionInfo();
}

ClientConnection::~ClientConnection()
{
	closeSocket();
}

bool ClientConnection::readDataFromSocket()
{
	bool haveNewData = false;
	if (socket_.available()) {
		
		if (synchServer_Sptr server_shp = server_.lock()) {
			if (server_shp->getLoggingRequired())
				server_shp->addLogInFile("DEBUG: start reading from socket");
		}		
		
		haveNewData = true;
		int numberReadAttempts = 0;
		std::size_t bytes_readable = 0;
		while (!bytes_readable)
		{
			// waiting for all the data to be loaded into the buffer.
			// Needed when receiving data without termination character
			std::this_thread::sleep_for(std::chrono::milliseconds(delayReadingFromSocket_));
			numberReadAttempts++;

			// Issue command to socket to get number of bytes readable.
			boost::asio::socket_base::bytes_readable num_of_bytes_readable(true);
			socket_.io_control(num_of_bytes_readable);

			if (synchServer_Sptr server_shp = server_.lock()) {
				if (server_shp->getLoggingRequired())
					server_shp->addLogInFile("DEBUG: reading from socket (step 1)");
			}

			// Get the value from the command.
			bytes_readable = num_of_bytes_readable.get();
			
			if (synchServer_Sptr server_shp = server_.lock()) {
				if (server_shp->getLoggingRequired())
					server_shp->addLogInFile("DEBUG: reading from socket (step 2)");
			}

			// sometimes, with available socket, the amount of data to be read is 0. 
			// If no data has been received within 10 attempts - break the loop
			if (numberReadAttempts > 10)
				break;
		}
		buffer_.resize(bytes_readable);
		socket_.receive(boost::asio::buffer(buffer_, bytes_readable));

		refreshLastActivityTime();

		if (synchServer_Sptr server_shp = server_.lock()) {
			if (server_shp->getLoggingRequired()) {
				std::string bufferString(buffer_.begin(), buffer_.end());
				std::string record = (boost::format("read from socket: port %d, uuid connection: %s, data: %s")
					% portNumber_
					% clientSocketUuidString_.c_str()
					% bufferString.c_str()
					).str();
				server_shp->addLog(record);
			}
		}
	}

	return haveNewData;
}

void ClientConnection::sendMessage(const message_ptr &message)
{
	if (delayMessageSending_ > 0)
		std::this_thread::sleep_for(std::chrono::milliseconds(delayMessageSending_));
	
	if (synchServer_Sptr server_shp = server_.lock()) {
		if (server_shp->getLoggingRequired())
			server_shp->addLogInFile("DEBUG: start writing to socket");
	}

	boost::system::error_code err;
	socket_.write_some(boost::asio::buffer(message->messageBody), err);

	if (err && isLinkToRemoteServer_) {
		// connection to is not alive. Try to reconnect and (if connected) send message again
		bool connectedToRemoteServer = reconnectSocketRemoteServer();
		if (connectedToRemoteServer) {
			socket_.write_some(boost::asio::buffer(message->messageBody), err);
		}
	}

	if (! err)
		refreshLastActivityTime();

	if (synchServer_Sptr server_shp = server_.lock()) {
		if (server_shp->getLoggingRequired()) {
			std::string record;
			if (err)
			{
				record = (boost::format("error: can not write in socket. It has been closed: port %d, uuid connection: %s, data: %s")
					% portNumber_
					% clientSocketUuidString_.c_str()
					% message->messageBody.c_str()
					).str();
			}
			else
			{
				record = (boost::format("write in socket: port %d, uuid connection: %s, data: %s")
					% portNumber_
					% clientSocketUuidString_.c_str()
					% message->messageBody.c_str()
					).str();		
			}
			server_shp->addLog(record);
		}
	}
}

void ClientConnection::closeSocket()
{
	if (synchServer_Sptr server_shp = server_.lock()) {
		if (server_shp->getLoggingRequired())
			server_shp->addLogInFile("DEBUG: start closing socket");
	}

	boost::system::error_code err;
	socket_.close(err);
	
	if (synchServer_Sptr server_shp = server_.lock()) {
		if (server_shp->getLoggingRequired()) {
			std::string record = (boost::format("close socket: port %d, uuid connection %s")
				% portNumber_
				% clientSocketUuidString_.c_str()
				).str();
			server_shp->addLog(record);
		}
	}
}

bool ClientConnection::reconnectSocketRemoteServer()
{
	if (synchServer_Sptr server_shp = server_.lock()) {
		if (server_shp->getLoggingRequired())
			server_shp->addLogInFile("DEBUG: start reconnectSocketRemoteServer()");
	}

	boost::system::error_code err;
	
	socket_.close(err);
	
	boost::asio::ip::tcp::endpoint ep(
		boost::asio::ip::address_v4::from_string(remoteIP_), portNumber_
	);
	socket_.connect(ep, err);

	if (err)
		return false;
	else {
		if (synchServer_Sptr server_shp = server_.lock()) {
			if (server_shp->getLoggingRequired()) {
				server_shp->addLog(std::to_string(portNumber_) + " socket has been connected (reconnected)");
			}
		}
		refreshLastActivityTime();
		
		return true;
	}
}

bool ClientConnection::isTimedOut()
{
	const boost::posix_time::ptime now = boost::posix_time::second_clock::local_time();
	const long long ms = (now - lastActivityTime_).total_milliseconds();
	return ms > allowedTimeNoActivity_;
}

std::string ClientConnection::getLastActivityTimeInStringFormat()
{
	return timeToISOString(lastActivityTime_);
}

void ClientConnection::acceptConnection()
{
	connectionAccepted_ = true;
	refreshLastActivityTime();

	if (synchServer_Sptr server_shp = server_.lock()) {
		if (server_shp->getLoggingRequired()) {
			std::string record = (boost::format("connection accepted: port %d, uuid connection %s")
				% portNumber_
				% clientSocketUuidString_.c_str()
				).str();
			server_shp->addLog(record);
		}
	}
}

std::string ClientConnection::getBufferString()
{
	std::string bufferString(buffer_.begin(), buffer_.end());
	return bufferString;
}

void ClientConnection::refreshLastActivityTime()
{
	lastActivityTime_ = boost::posix_time::second_clock::local_time();
}

void ClientConnection::printConstructionInfo()
{
	if (synchServer_Sptr server_shp = server_.lock()) {
		if (server_shp->getLoggingRequired()) {
			if (isLinkToRemoteServer_) {
				std::string record = (boost::format("new connection (to remote server): port %d, ip %s, uuid connection %s")
					% portNumber_
					% remoteIP_
					% clientSocketUuidString_.c_str()
					).str();
				server_shp->addLog(record);
			}
			else {
				std::string record = (boost::format("new connection (waiting fot accept): port %d, uuid connection %s")
					% portNumber_
					% clientSocketUuidString_.c_str()
					).str();
				server_shp->addLog(record);
			}
		}
	}
}


//---------------------------------------------------------------------------//
// PortSettings methods
//---------------------------------------------------------------------------//

PortSettings::PortSettings(int _portNumber, bool _isLinkToRemoteServer, std::string _remoteIP, std::string _portTypeString,
	int _delayReadingFromSocket, int _delayMessageSending, int _allowedTimeNoActivity)
	: portNumber(_portNumber), isLinkToRemoteServer(_isLinkToRemoteServer), remoteIP(_remoteIP),
	delayReadingFromSocket(_delayReadingFromSocket), delayMessageSending(_delayMessageSending), allowedTimeNoActivity(_allowedTimeNoActivity){

	setPortTypeFromString(_portTypeString);
}

void PortSettings::setPortTypeFromString(std::string portTypeString)
{
	if (portTypeString == "TCP") portType = TCP;
	else if (portTypeString == "UDP") portType = UDP;
	else if (portTypeString == "COM") portType = COM;
	else portType = TCP;
}


//---------------------------------------------------------------------------//
// Message methods
//---------------------------------------------------------------------------//

Message::Message(MessageDirectionEnum _direction, std::string _messageUuidString,
	const std::string &_messageBody, int _portNumber, std::string _remoteIP, std::string _clientSocketUuidString)
	: direction(_direction), messageUuidString(_messageUuidString), messageBody(_messageBody), 
	portNumber(_portNumber), remoteIP(_remoteIP), clientSocketUuidString(_clientSocketUuidString){
	
	takenInProcessing_ = false;
	processingCompleted_ = false;
}


//---------------------------------------------------------------------------//
// LogRecord methods
//---------------------------------------------------------------------------//

LogRecord::LogRecord(const std::string &logBody_, LogTypeEnum type_)
	:logBody(logBody_), type(type_)
{
	timePoint = boost::posix_time::second_clock::local_time();
	logRecordUuidString = generateNewUuidString();
	processingCompleted_ = false;
}

std::string LogRecord::getTypeInStringFormat()
{
	switch (type) {
	case LOG_INFO:
		return "INFO";
	case LOG_ERROR:
		return "ERROR";
	default:
		return "INFO";
	}
}

std::string LogRecord::getTimePointInStringFormat()
{
	return timeToISOString(timePoint);
}


//---------------------------------------------------------------------------//
// Other methods
//---------------------------------------------------------------------------//

std::string generateNewUuidString()
{
	return boost::uuids::to_string(boost::uuids::random_generator()());
}

std::string timeToISOString(boost::posix_time::ptime time)
{
	std::string str_time = to_iso_extended_string(time);
	return str_time;
}