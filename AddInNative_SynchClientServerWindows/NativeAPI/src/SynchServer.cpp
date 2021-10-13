
#include "SynchServer.h"
#include "JsonProsessing.h"
#include <algorithm>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/date_time/posix_time/posix_time_io.hpp>
#include "boost/format.hpp"
#include <iostream>
#include <fstream>

//---------------------------------------------------------------------------//
// SynchServer methods
//---------------------------------------------------------------------------//

SynchClientServer::~SynchClientServer()
{
	terminateServer();
}

bool SynchClientServer::setServerParameters(const std::string &parametersString) {
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
		if (ipString.empty()) 
			return false;
		ip = boost::asio::ip::address_v4::from_string(ipString);
		loggingRequired = serverParametersJson["loggingRequired"].GetBool();
		logFileName = serverParametersJson["logFileName"].GetString();
		loggingToFile = ! logFileName.empty();
		memoryCleaningFrequency = serverParametersJson["memoryCleaningFrequency"].GetInt();
		serverTerminationSignal = serverParametersJson["serverTerminationSignal"].GetString();

		successfully = jsonArrayToPortsSettingsMap(serverParametersJson, portsSettings);

		state = READY;
	}
	else successfully = false;

	return successfully;
}

std::string SynchClientServer::listen()
{
	if (state == RUNNING)
		return false;

	// if at least one port (internal) is already open, then the server cannot be started
	std::string errorDescription;
	for (auto const& portSettingPair : portsSettings) {
		portSettings_ptr setting_ptr = portSettingPair.second;
		if (! setting_ptr->isLinkToRemoteServer && isPortAlreadyOpened(setting_ptr->portNumber, false)) {
			std::string portOpenError = std::to_string(setting_ptr->portNumber) + " port is already opened; ";
			errorDescription.append(portOpenError);
		}		
	}
	
	if (errorDescription.empty()) {
		std::unique_lock<std::mutex> lk(connections_mutex);
		// creatÂ accept connections threads. One for each port (internal)
		for (auto const& portSettingPair : portsSettings) {
			portSettings_ptr setting_ptr = portSettingPair.second;
			if (!setting_ptr->isLinkToRemoteServer) {		
				threads.create_thread(boost::bind(&SynchClientServer::accept_connections_thread, this, setting_ptr));
			}
		}
		threads.create_thread(boost::bind(&SynchClientServer::handle_connections_thread, this));
		threads.create_thread(boost::bind(&SynchClientServer::clean_thread, this));
		terminate_thread.create_thread(boost::bind(&SynchClientServer::checkForTerminate_thread, this));
	
		state = RUNNING;
		serverNeedToTerminate = false;
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

	return errorDescriptionJsonString;
}

bool SynchClientServer::getMessagesFromClientsWhenArrive(std::string &incomingMessagesJsonString, int checkFrequency, int timeout)
{
	boost::posix_time::ptime lastPulseTime = boost::posix_time::second_clock::local_time();

    while (true)
    {
        boost::this_thread::sleep(boost::posix_time::millisec(checkFrequency));
        std::unique_lock<std::mutex> lk(incomingMessages_mutex);
        if (!allIncomingMessagesTakenInProcessing()) {
            // receive all new (not takenInProcessing) incoming messages from clients
            rapidjson::Document messagesJson = constructJson(incomingMessages);
            std::string messagesJsonString = jsonToString(messagesJson);
            for (auto const& message_ptr : incomingMessages) {
                message_ptr->takeMesssageInProsessing();
            }

            incomingMessagesJsonString = messagesJsonString;
            break;
        }

        boost::posix_time::ptime now = boost::posix_time::second_clock::local_time();
        long long secondPassed = (now - lastPulseTime).total_milliseconds();
        if (secondPassed > timeout)
            break;
    }

	if (serverNeedToTerminate) {
		// waiting for this instance of the server to process the terminate message
		boost::this_thread::sleep(boost::posix_time::millisec(2000));
		return false;
	}

	return true;
}

bool SynchClientServer::getMessagesFromClients(std::string & incomingMessagesJsonString)
{
	std::unique_lock<std::mutex> lk(incomingMessages_mutex);
	
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

	return true;
}

bool SynchClientServer::ackMessagesReceipt(const std::string &messagesUuidJsonString)
{
	bool successfully = true;
	
	rapidjson::Document messagesUuidJson = stringToJson(messagesUuidJsonString);
	if (messagesUuidJson.IsObject()
		&& isJsonFieldValid(messagesUuidJson, "ackMessagesUuid", ArrayJson)) {
		std::set<std::string> messagesUuidSet;
		successfully = jsonArrayToSetOfUuidAckMessages(messagesUuidJson, messagesUuidSet);
		
		if (successfully) {
			int setSize = static_cast<int>(messagesUuidSet.size());

			std::unique_lock<std::mutex> lk(incomingMessages_mutex);
			int messageCount = 0;
			for (auto const& message_ptr : incomingMessages) {
				bool isMessageUuidInSet = messagesUuidSet.find(message_ptr->messageUuidString) != messagesUuidSet.end();
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

	return successfully;
}

bool SynchClientServer::sendMessageToClient(const std::string &outgoingMessagesJsonString)
{
	bool successfully = true;
	
	rapidjson::Document messageJson = stringToJson(outgoingMessagesJsonString);
	if (messageJson.IsObject()
		&& isJsonFieldValid(messageJson, "messageUuidString", StringJson)
		&& isJsonFieldValid(messageJson, "messageBody", StringJson)
		&& isJsonFieldValid(messageJson, "portNumber", IntJson)
		&& isJsonFieldValid(messageJson, "clientSocketUuidString", StringJson)) {

		std::string messageUuidString = messageJson["messageUuidString"].GetString();
		std::string messageBody = messageJson["messageBody"].GetString();
		int portNumber = messageJson["portNumber"].GetInt();
		std::string clientSocketUuidString = messageJson["clientSocketUuidString"].GetString();

		message_ptr newMessage_(
			new Message(OUTGOING,
				messageUuidString,
				messageBody,
				portNumber,
				clientSocketUuidString)
		);

		std::unique_lock<std::mutex> lk(outgoingMessages_mutex);
		outgoingMessages.push_back(newMessage_);
	}
	else successfully = false;

	return successfully;
}

bool SynchClientServer::sendMessageToRemoteServer(const std::string &outgoingMessagesJsonString)
{
	rapidjson::Document messageJson = stringToJson(outgoingMessagesJsonString);
	if (messageJson.IsObject()
		&& isJsonFieldValid(messageJson, "messageUuidString", StringJson)
		&& isJsonFieldValid(messageJson, "messageBody", StringJson)
		&& isJsonFieldValid(messageJson, "portNumber", IntJson)) {
		
		std::string messageUuidString = messageJson["messageUuidString"].GetString();
		std::string messageBody = messageJson["messageBody"].GetString();
		int portNumber = messageJson["portNumber"].GetInt();
		
		portSettings_ptr portSettings = getPortSettings(portNumber);
		if (! portSettings) {
			if (loggingRequired)
				addLog(std::to_string(portNumber) + " can not find any portSettings by port number");
			return false;
		}

		clientConnection_ptr connectionToRemoteServer = getCreateClientConnectionToRemoteServer(portSettings);
		if (! connectionToRemoteServer) {
			if (loggingRequired)
				addLog(std::to_string(portNumber) + " can not establish the connection to remote server. Message has been ignored");
			return true;
		}
				
		std::string clientSocketUuidString = connectionToRemoteServer->getClientSocketUuidString();
		message_ptr newMessage_(
			new Message(OUTGOING,
				messageUuidString,
				messageBody,
				portNumber,
				clientSocketUuidString)
		);
		std::unique_lock<std::mutex> lk(outgoingMessages_mutex);
		outgoingMessages.push_back(newMessage_);
	}
	else 
		return false;

	return true;
}

std::string SynchClientServer::getClientsState()
{
	std::string clientsConnectionsJsonString;

	std::unique_lock<std::mutex> lk(connections_mutex);
	rapidjson::Document clientsConnectionsJson = constructJson(clientsConnections);
	clientsConnectionsJsonString = jsonToString(clientsConnectionsJson);

	return clientsConnectionsJsonString;
}

bool SynchClientServer::stopServer()
{	
	if (state != RUNNING)
		return true;
	
	state = STOPED;

	terminate_thread.interrupt_all();

	threads.interrupt_all();	
	// establish temporary connections on all server ports to exit loops in the accept_connections_thread thread, 
	// since the acceptor.accept () method is blocking
	for (auto const& portSettingPair : portsSettings) {
		portSettings_ptr setting_ptr = portSettingPair.second;
		isPortAlreadyOpened(setting_ptr->getPortNumber(), false);
	}
	threads.join_all();

	clientsConnections.clear();
	incomingMessages.clear();
	outgoingMessages.clear();

	if (loggingRequired)
		addLog("server has been stoped");

	return true;
}

bool SynchClientServer::sendTerminationSignalToRunningInstanceOfServer()
{	
	for (auto const& portSettingPair : portsSettings) {
		portSettings_ptr setting_ptr = portSettingPair.second;
		
		if (!setting_ptr->isLinkToRemoteServer) {
			bool isOpened = isPortAlreadyOpened(setting_ptr->portNumber, true);
		}
	}

	// waiting for another instance of the server to process the message
	boost::this_thread::sleep(boost::posix_time::millisec(2000));

	return true;
}

void SynchClientServer::terminateServer()
{
	std::unique_lock<std::mutex> lk(stopServer_mutex);

	service_io.post([this]() {
		work_io.reset(); // let io_service run out of work
	});
	service_io.stop();
	stopServer();
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

std::string SynchClientServer::getLastRecordsFromLogHistory(int recordsNumber, bool onlyNew)
{
	std::string logsJsonString;
	
	std::unique_lock<std::mutex> lk(logHistory_mutex);
	if (logHistory.empty())
		return "";
	
	rapidjson::Document logsJson = constructJson(logHistory, recordsNumber, onlyNew);
	logsJsonString = jsonToString(logsJson);

	prosessLastRecordsFromLogHistory(logHistory, recordsNumber, onlyNew);

	return logsJsonString;
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
		
		clientConnection_ptr newConnection_(
			new ClientConnection(boost::ref(service_io), portSettings, weak_from_this())
		);
		
		boost::system::error_code err;
		acceptor.accept(newConnection_->getSocket(), err);
		if (err) 
			return;
		newConnection_->acceptConnection();

		std::unique_lock<std::mutex> lk_connections(connections_mutex);
		clientsConnections.push_back(newConnection_);
	}
}

void SynchClientServer::handle_connections_thread() {
	if (loggingRequired)
		addLog("run handle_connections_thread");
	
	while (true) {
		boost::this_thread::sleep(boost::posix_time::milliseconds(1));
		std::unique_lock<std::mutex> lk_connections(connections_mutex);
		if (isThreadInterrupted()) 
			break;
		
		for (auto const& clientConnection_ptr : clientsConnections) {
			readMessageFromConnection(clientConnection_ptr);
			sendMessagesToConnection(clientConnection_ptr);
		}
	}
}

void SynchClientServer::clean_thread()
{
	while (true) {
		boost::this_thread::sleep(boost::posix_time::millisec(memoryCleaningFrequency));
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
				boost::bind(&Message::isMessageProcessing—ompleted, _1)
			),
			incomingMessages.end()
		);
		lk_incomingMessages.unlock();
	
		outgoingMessages.erase(
			std::remove_if(
				outgoingMessages.begin(),
				outgoingMessages.end(),
				boost::bind(&Message::isMessageProcessing—ompleted, _1)
			),
			outgoingMessages.end()
		);
		lk_outgoingMessages.unlock();
	}
}

void SynchClientServer::checkForTerminate_thread()
{
	while (true) {
		boost::this_thread::sleep(boost::posix_time::millisec(1000));
		if (isThreadInterrupted() || state == STOPED)
			break;
		
		std::unique_lock<std::mutex> lk(classVariables_mutex);
		if (serverNeedToTerminate) {
			terminateServer();
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
			serverNeedToTerminate = true;
		}

		message_ptr newMessage_(
			new Message(INCOMMING,
				generateNewUuidString(),
				bufferString,
				clientConnection->getPortNumber(),
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
	bool sendWithDelay = false;
	for (auto const& message_ptr : outgoingMessages) {
		const std::string & m_clientSocketUuidString = message_ptr->getClientSocketUuidStringRef();
		bool isMessageForThisConnection = clientSocketUuidString == m_clientSocketUuidString;

		if (isMessageForThisConnection
			&& !message_ptr->isMessageProcessing—ompleted()) {		
			message_ptr->takeMesssageInProsessing();
			clientConnection->sendMessage(message_ptr, sendWithDelay);
			message_ptr->completeProsessingMesssage();
			
			// delay only for the second and following messages
			if (sendWithDelay) 
				sendWithDelay = true;
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
		if (onlyNew && (*it)->isLogRecordProcessing—ompleted())
			continue;

		(*it)->completeProsessingLogRecord();

		recordsCount++;
		if (recordsCount == recordsNumber)
			break;
	}
}

const clientConnection_ptr SynchClientServer::getCreateClientConnectionToRemoteServer(const portSettings_ptr & portSettings)
{
	std::unique_lock<std::mutex> lk_connections(connections_mutex);
	
	int portNumber = portSettings->getPortNumber();
	for (auto it = clientsConnections.rbegin(); it != clientsConnections.rend(); ++it) {
		if ((*it)->getPortNumber() == portNumber) {
			return *it;
		}
	}
	
	// can not find existing connection. Create new one and try to establish connection

	if (portSettings->getRemoteIP().empty()) {
		return std::shared_ptr<ClientConnection>(nullptr);
	}

	clientConnection_ptr newConnection_(
		new ClientConnection(boost::ref(service_io), portSettings, weak_from_this())
	);

	bool successfully = newConnection_->reconnectSocketRemoteServer();
	if (successfully) {
		newConnection_->acceptConnection();	
		clientsConnections.push_back(newConnection_);
		
		return newConnection_;		
	}
	else {
		return std::shared_ptr<ClientConnection>(nullptr);
	}
}

const portSettings_ptr SynchClientServer::getPortSettings(int portNumber)
{
	std::map<int, portSettings_ptr>::iterator pos = portsSettings.find(portNumber);
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
				server_shp->addLogInFile("start reading from socket");
		}		
		
		haveNewData = true;		
		refreshLastActivityTime();
		std::size_t bytes_readable = 0;
		while (!bytes_readable)
		{
			// waiting for all the data to be loaded into the buffer.
			// Needed when receiving data without termination character
			boost::this_thread::sleep(boost::posix_time::millisec(delayReadingFromSocket_));

			// Issue command to socket to get number of bytes readable.
			boost::asio::socket_base::bytes_readable num_of_bytes_readable(true);
			socket_.io_control(num_of_bytes_readable);
			// Get the value from the command.
			bytes_readable = num_of_bytes_readable.get();
		}
		buffer_.resize(bytes_readable);
		socket_.receive(boost::asio::buffer(buffer_, bytes_readable));

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

void ClientConnection::sendMessage(const message_ptr &message, bool withDelay)
{
	if (withDelay)
		boost::this_thread::sleep(boost::posix_time::millisec(delayMessageSending_));
	
	if (synchServer_Sptr server_shp = server_.lock()) {
		if (server_shp->getLoggingRequired())
			server_shp->addLogInFile("start writing to socket");
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
			server_shp->addLogInFile("start closing socket");
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
	boost::posix_time::ptime now = boost::posix_time::second_clock::local_time();
	long long ms = (now - lastActivityTime_).total_milliseconds();
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
	const std::string &_messageBody, int _portNumber, std::string _clientSocketUuidString)
	: direction(_direction), messageUuidString(_messageUuidString), messageBody(_messageBody), 
	portNumber(_portNumber), clientSocketUuidString(_clientSocketUuidString){
	
	takenInProcessing_ = false;
	processing—ompleted_ = false;
}


//---------------------------------------------------------------------------//
// LogRecord methods
//---------------------------------------------------------------------------//

LogRecord::LogRecord(const std::string &logBody_, LogTypeEnum type_)
	:logBody(logBody_), type(type_)
{
	timePoint = boost::posix_time::second_clock::local_time();
	logRecordUuidString = generateNewUuidString();
	processing—ompleted_ = false;
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

bool isThreadInterrupted() {
	bool interrupted = false;
	try
	{
		boost::this_thread::interruption_point();
		// Sleep and check for interrupt.
		//boost::this_thread::sleep(boost::posix_time::milliseconds(1));
	}
	catch (boost::thread_interrupted&)
	{
		interrupted = true;
	}

	return interrupted;
}

std::string generateNewUuidString()
{
	return boost::uuids::to_string(boost::uuids::random_generator()());
}

std::string timeToISOString(boost::posix_time::ptime time)
{
	std::string str_time = to_iso_extended_string(time);
	return str_time;
}