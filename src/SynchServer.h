#pragma once

#include <addin/biterp/Component.hpp>
#include <map>
#include <boost/uuid/uuid.hpp>
#include <boost/asio.hpp>
#include <boost/thread.hpp>
#include <vector>
#include <thread>
#include <atomic>

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


class SynchClientServer : public Biterp::Component, public std::enable_shared_from_this<SynchClientServer> {
public:
	// Disable copy	semantics
	SynchClientServer(const SynchClientServer &) = delete;
	SynchClientServer & operator = (const SynchClientServer &) = delete;
	// Disable move semantics
	SynchClientServer(const SynchClientServer &&) = delete;
	SynchClientServer & operator = (const SynchClientServer &&) = delete;

	~SynchClientServer() { terminateServer(); };
	SynchClientServer() : Biterp::Component("SynchClientServer"), state(READY), loggingRequired(false), loggingToFile(false), memoryCleaningFrequency(5000),
		serverNeedToTerminate(false), allThreadsNeedToTerminate(false) {};

	inline bool setServerParameters(tVariant* paParams, const long lSizeArray) {
		return wrapCall(this, &SynchClientServer::setServerParametersImpl, paParams, lSizeArray);
	}
	inline bool ackMessagesReceipt(tVariant* paParams, const long lSizeArray) {
		return wrapCall(this, &SynchClientServer::ackMessagesReceiptImpl, paParams, lSizeArray);
	}
	inline bool sendMessageToClient(tVariant* paParams, const long lSizeArray) {
		return wrapCall(this, &SynchClientServer::sendMessageToClientImpl, paParams, lSizeArray);
	}
	inline bool sendMessageToRemoteServer(tVariant* paParams, const long lSizeArray) {
		return wrapCall(this, &SynchClientServer::sendMessageToRemoteServerImpl, paParams, lSizeArray);
	}
	inline bool stopServer(tVariant* paParams, const long lSizeArray) {
		return wrapCall(this, &SynchClientServer::stopServerImpl, paParams, lSizeArray);
	}
	inline bool sendTerminationSignalToRunningInstanceOfServer(tVariant* paParams, const long lSizeArray) {
		return wrapCall(this, &SynchClientServer::sendTerminationSignalToRunningInstanceOfServerImpl, paParams, lSizeArray);
	}

	inline bool listen(tVariant* pvarRetValue, tVariant* paParams, const long lSizeArray) {
		return wrapCall(this, &SynchClientServer::listenImpl, paParams, lSizeArray, pvarRetValue);
	}
	inline bool getLastLogRecords(tVariant* pvarRetValue, tVariant* paParams, const long lSizeArray) {
		return wrapCall(this, &SynchClientServer::getLastRecordsFromLogHistoryImpl, paParams, lSizeArray, pvarRetValue);
	}
	inline bool getMessagesFromClientsWhenArrive(tVariant* pvarRetValue, tVariant* paParams, const long lSizeArray) {
		return wrapCall(this, &SynchClientServer::getMessagesFromClientsWhenArriveImpl, paParams, lSizeArray, pvarRetValue);
	}
	inline bool getMessagesFromClients(tVariant* pvarRetValue, tVariant* paParams, const long lSizeArray) {
		return wrapCall(this, &SynchClientServer::getMessagesFromClientsImpl, paParams, lSizeArray, pvarRetValue);
	}
	inline bool getClientsState(tVariant* pvarRetValue, tVariant* paParams, const long lSizeArray) {
		return wrapCall(this, &SynchClientServer::getClientsStateImpl, paParams, lSizeArray, pvarRetValue);
	}

	inline bool getPropertyLoggingRequired(tVariant* pvarPropVal, const long lPropNum) {
		return wrapLongCall(this, &SynchClientServer::getPropertyLoggingRequiredImpl, lPropNum, nullptr, 0, pvarPropVal);
	}
	inline bool setPropertyLoggingRequired(tVariant* varPropVal, const long lPropNum) {
		return wrapLongCall(this, &SynchClientServer::setPropertyLoggingRequiredImpl, lPropNum, varPropVal, 1);
	}

public:
	bool getLoggingRequired();
	void setLoggingRequired(bool _loggingRequired);

	void addLog(const std::string& record);
	void addRecordInLogsHistory(const std::string& record);
	void addLogInFile(const std::string& record);
	void addLogInFile(const char*& record);

private:
	void setServerParametersImpl(Biterp::CallContext& ctx);
	void ackMessagesReceiptImpl(Biterp::CallContext& ctx);
	void sendMessageToClientImpl(Biterp::CallContext& ctx);
	void sendMessageToRemoteServerImpl(Biterp::CallContext& ctx);
	void stopServerImpl(Biterp::CallContext& ctx);
	void sendTerminationSignalToRunningInstanceOfServerImpl(Biterp::CallContext& ctx);
	
	void listenImpl(Biterp::CallContext& ctx);
	void getLastRecordsFromLogHistoryImpl(Biterp::CallContext& ctx);
	void getMessagesFromClientsWhenArriveImpl(Biterp::CallContext& ctx);
	void getMessagesFromClientsImpl(Biterp::CallContext& ctx);
	void getClientsStateImpl(Biterp::CallContext& ctx);
	
	inline void getPropertyLoggingRequiredImpl(const long propNum, Biterp::CallContext& ctx) { ctx.setBoolResult(getLoggingRequired()); }
	inline void setPropertyLoggingRequiredImpl(const long propNum, Biterp::CallContext& ctx) { setLoggingRequired(ctx.boolParam()); }

private:
	void stopServer_();

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
	const clientConnection_ptr getExistingClientConnectionToRemoteServer(const portSettings_ptr& portSettings);
	const portSettings_ptr getPortSettings(int portNumber, std::string remoteIP);
	bool isServerTerminationSignal(const std::string &record);
	void terminateServer();
	bool isThreadInterrupted();

private:
	boost::asio::io_service service_io;
	boost::optional<boost::asio::io_service::work> work_io{ service_io };
	boost::asio::ip::address_v4 ip;
	std::map<std::pair<int, std::string>, portSettings_ptr> portsSettings;
	std::vector<clientConnection_ptr> clientsConnections;
	std::vector<message_ptr> incomingMessages;
	std::vector<message_ptr> outgoingMessages;
	std::vector<std::thread> threads;
	std::vector<std::thread> terminate_thread;
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
	std::atomic<bool> serverNeedToTerminate;
	std::atomic<bool> allThreadsNeedToTerminate;

private:
	template<typename T, typename Proc>
	bool wrapLongCall(T* obj, Proc proc, const long param, tVariant* paParams, const long lSizeArray,
		tVariant* pvarRetValue = nullptr) {
		bool result = false;
		try {
			skipAddError = false;
			lastError.clear();
			Biterp::CallContext ctx(memManager, paParams, lSizeArray, pvarRetValue);
			(obj->*proc)(param, ctx);
			result = true;
		}
		catch (std::exception& e) {
			string who = typeid(e).name();
			string what = e.what();
			LOGE(who + ": " + what);
			addError(what, who);
		}
		return result;
	}
};

class ClientConnection {
public:
	ClientConnection(boost::asio::io_service &service_io, const portSettings_ptr & portSettings,
		const synchServer_Wptr &server);
	~ClientConnection();

	void acceptConnection();
	bool readDataFromSocket();
	void sendMessage(const message_ptr &message);
	bool isTimedOut();
	void closeSocket();
	bool reconnectSocketRemoteServer();

	inline boost::asio::ip::tcp::socket& getSocket() { return socket_; }
	std::string getLastActivityTimeInStringFormat();
	inline const std::string & getClientSocketUuidString() { return clientSocketUuidString_; }
	inline int getPortNumber() { return portNumber_; }
	inline std::string getRemoteIP() { return remoteIP_; }
	inline bool isLinkToRemoteServer() { return isLinkToRemoteServer_; }
	inline bool getConnectionAccepted() { return connectionAccepted_; }
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
	
	inline std::string getRemoteIP() { return remoteIP; }
	inline int getPortNumber() { return portNumber; }

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
		const std::string &messageBody, int portNumber, std::string remoteIP, std::string clientSocketUuidString);

	inline bool isMessageInProcessing()		{ return takenInProcessing_; }
	inline bool isMessageProcessingCompleted() { return processingCompleted_; }
	inline void takeMesssageInProsessing()		{ takenInProcessing_ = true; }
	inline void completeProsessingMesssage()	{ processingCompleted_ = true; }
	inline const std::string & getClientSocketUuidStringRef() { return clientSocketUuidString; }

	MessageDirectionEnum direction;
	std::string messageUuidString;
	std::string messageBody;	
	int portNumber;
	std::string remoteIP;
	std::string clientSocketUuidString;
private:
	bool takenInProcessing_;
	bool processingCompleted_;
};

struct LogRecord
{
	LogRecord(const std::string &logBody_, LogTypeEnum type_=LOG_INFO);

	LogTypeEnum type;
	std::string logBody;
	boost::posix_time::ptime timePoint;
	std::string logRecordUuidString;

	inline bool isLogRecordProcessingCompleted()	{ return processingCompleted_; }
	inline void completeProsessingLogRecord()		{ processingCompleted_ = true; }
	std::string getTypeInStringFormat();
	std::string getTimePointInStringFormat();
private:
	bool processingCompleted_;
};

std::string generateNewUuidString();
std::string timeToISOString(boost::posix_time::ptime time);
