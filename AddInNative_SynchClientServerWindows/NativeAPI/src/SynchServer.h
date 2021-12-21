#pragma once
#include <map>
#include <boost/uuid/uuid.hpp>
#include <boost/asio.hpp>
#include <boost/thread.hpp>
#include <vector>
#include <types.h>

struct PortSettings;
struct Message;
struct LogRecord;
class ClientConnection;
class SynchClientServer;

typedef std::weak_ptr<SynchClientServer> synchServer_Wptr;
typedef std::shared_ptr<SynchClientServer> synchServer_Sptr;
typedef std::shared_ptr<ClientConnection> clientConnection_ptr;
typedef std::shared_ptr<PortSettings> portSettings_ptr;
typedef std::shared_ptr<Message> message_ptr;
typedef std::shared_ptr<LogRecord> logRecord_ptr;

enum PortTypeEnum
{
	TCP,
	UDP,
	COM
};

enum MessageDirectionEnum
{
	INCOMMING,
	OUTGOING
};

enum ServerStateEnum
{
	READY,
	STOPED,
	RUNNING
};

enum LogTypeEnum
{
	LOG_INFO,
	LOG_ERROR
};


class SynchClientServer : public std::enable_shared_from_this<SynchClientServer> {
public:
	// Disable copy	semantics
	SynchClientServer(const SynchClientServer &) = delete;
	SynchClientServer & operator = (const SynchClientServer &) = delete;
	// Disable move semantics
	SynchClientServer(const SynchClientServer &&) = delete;
	SynchClientServer & operator = (const SynchClientServer &&) = delete;

	~SynchClientServer();
	SynchClientServer() {};

	bool setServerParameters(const std::string &parametersString);
	std::string listen();
	bool getMessagesFromClientsWhenArrive(std::string &incomingMessagesJsonString, int checkFrequency, int timeout);
	bool getMessagesFromClients(std::string &incomingMessagesJsonString);
	bool ackMessagesReceipt(const std::string &messagesUuidJsonString);
	bool sendMessageToClient(const std::string &outgoingMessagesJsonString);
	bool sendMessageToRemoteServer(const std::string &outgoingMessagesJsonString);
	std::string getClientsState();
	ServerStateEnum getServerState() { return state; }
	std::string getLastRecordsFromLogHistory(int recordsNumber, bool onlyNew=true);
	bool stopServer();
	bool sendTerminationSignalToRunningInstanceOfServer();

	bool getLoggingRequired();
	void setLoggingRequired(bool _loggingRequired);	
	void addLog(const std::string &record);
	void addRecordInLogsHistory(const std::string &record);
	void addLogInFile(const std::string &record);
	void addLogInFile(const char* & record);
private:
	void accept_connections_thread(const portSettings_ptr & portSettings);
	void handle_connections_thread();
	void clean_thread();
	void checkForTerminate_thread();
	void reconnectToRemoteServers_thread();
	bool allIncomingMessagesTakenInProcessing();
	void readMessageFromConnection(const clientConnection_ptr &clientConnection);
	void sendMessagesToConnection(const clientConnection_ptr &clientConnection);
	bool isPortAlreadyOpened(int portNumber, bool needToTerminate);
	void prosessLastRecordsFromLogHistory(const std::vector<logRecord_ptr> &logHistory, int recordsNumber, bool onlyNew);
	const clientConnection_ptr getCreateClientConnectionToRemoteServer(const portSettings_ptr & portSettings);
	const portSettings_ptr getPortSettings(int portNumber);
	bool isServerTerminationSignal(const std::string &record);
	void terminateServer();
private:
	boost::asio::io_service service_io;
	boost::optional<boost::asio::io_service::work> work_io{ service_io };
	boost::asio::ip::address_v4 ip;
	std::map<int, portSettings_ptr> portsSettings;
	std::vector<clientConnection_ptr> clientsConnections;
	std::vector<message_ptr> incomingMessages;
	std::vector<message_ptr> outgoingMessages;
	boost::thread_group threads;
	boost::thread_group terminate_thread;
	std::mutex connections_mutex;
	std::mutex incomingMessages_mutex;
	std::mutex outgoingMessages_mutex;
	std::mutex logHistory_mutex;
	std::mutex classVariables_mutex;
	std::mutex stopServer_mutex;

	ServerStateEnum state;
	bool loggingRequired;
	bool loggingToFile;
	std::string logFileName;
	std::vector<logRecord_ptr> logHistory;
	int memoryCleaningFrequency;
	std::string serverTerminationSignal;
	bool serverNeedToTerminate;
};

class ClientConnection {
public:
	ClientConnection(boost::asio::io_service &service_io, const portSettings_ptr & portSettings,
		const synchServer_Wptr &server);
	~ClientConnection();

	void acceptConnection();
	bool readDataFromSocket();
	void sendMessage(const message_ptr &message, bool withDelay);
	bool isTimedOut();
	void closeSocket();
	bool reconnectSocketRemoteServer();

	boost::asio::ip::tcp::socket& getSocket() { return socket_; }
	std::string getLastActivityTimeInStringFormat();
	const std::string & getClientSocketUuidString() { return clientSocketUuidString_; }
	int getPortNumber() { return portNumber_; }
	bool isLinkToRemoteServer() { return isLinkToRemoteServer_; }
	bool getConnectionAccepted() { return connectionAccepted_; }
	std::string getBufferString();
private:
	void refreshLastActivityTime();
	void printConstructionInfo();
private:
	synchServer_Wptr server_;
	boost::asio::ip::tcp::socket socket_;
	std::vector<char> buffer_;
	std::string clientSocketUuidString_;
	bool connectionAccepted_;
	boost::posix_time::ptime lastActivityTime_;
	int portNumber_;
	bool isLinkToRemoteServer_;
	std::string remoteIP_;
	int delayReadingFromSocket_;
	int delayMessageSending_;
	int allowedTimeNoActivity_;
};

struct PortSettings
{
	PortSettings(int portNumber, bool isLinkToRemoteServer, std::string remoteIP, std::string portTypeString, 
		int delayReadingFromSocket, int delayMessageSending, int allowedTimeNoActivity);
	
	std::string getRemoteIP() { return remoteIP; }
	int getPortNumber() { return portNumber; }

	PortTypeEnum portType;
	int portNumber;	
	bool isLinkToRemoteServer;
	std::string remoteIP;
	int delayReadingFromSocket;
	int delayMessageSending;
	int allowedTimeNoActivity;
private:
	void setPortTypeFromString(std::string portTypeString);
};

struct Message
{
	Message(MessageDirectionEnum direction, std::string messageUuidString, 
		const std::string &messageBody, int portNumber, std::string clientSocketUuidString);

	bool isMessageInProcessing()		{ return takenInProcessing_; }
	bool isMessageProcessing—ompleted() { return processing—ompleted_; }
	void takeMesssageInProsessing()		{ takenInProcessing_ = true; }
	void completeProsessingMesssage()	{ processing—ompleted_ = true; }
	const std::string & getClientSocketUuidStringRef() { return clientSocketUuidString; }

	MessageDirectionEnum direction;
	std::string messageUuidString;
	std::string messageBody;	
	int portNumber;
	std::string clientSocketUuidString;
private:
	bool takenInProcessing_;
	bool processing—ompleted_;
};

struct LogRecord
{
	LogRecord(const std::string &logBody_, LogTypeEnum type_=LOG_INFO);

	LogTypeEnum type;
	std::string logBody;
	boost::posix_time::ptime timePoint;
	std::string logRecordUuidString;

	bool isLogRecordProcessing—ompleted()	{ return processing—ompleted_; }
	void completeProsessingLogRecord()		{ processing—ompleted_ = true; }
	std::string getTypeInStringFormat();
	std::string getTimePointInStringFormat();
private:
	bool processing—ompleted_;
};

std::string generateNewUuidString();
bool isThreadInterrupted();
std::string timeToISOString(boost::posix_time::ptime time);
