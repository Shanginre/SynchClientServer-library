
#include "addIn/stdafx.h"
#pragma warning( disable : 4267)	
#pragma warning( disable : 4311)	
#pragma warning( disable : 4302)

#if defined( __linux__ ) || defined(__APPLE__)
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <iconv.h>
#include <sys/time.h>
#endif

#include <stdio.h>
#include <wchar.h>
#include "AddInNative.h"
#include <string>

#include "src/Utils.h"	
#include <codecvt>

/*constexpr size_t TIME_LEN = 65;

#define BASE_ERRNO   */ 

#define TIME_LEN 65

#define BASE_ERRNO     7

static const wchar_t *g_PropNames[] = {
	L"Version",
	L"loggingRequired",
};
static const wchar_t *g_MethodNames[] = {
	L"SetServerParameters",
	L"Listen",
	L"GetMessagesFromClientsWhenArrive",
	L"GetMessagesFromClients",
	L"AckMessagesReceipt",
	L"SendMessageToClient",
	L"SendMessageToRemoteServer",
	L"GetClientsState",
	L"StopServer",
	L"SendTerminationSignalToRunningInstanceOfServer",
	L"GetLastLogRecords",
};

static const wchar_t *g_PropNamesRu[] = {
	L"Version",
	L"loggingRequired",
};
static const wchar_t *g_MethodNamesRu[] = {
	L"SetServerParameters",
	L"Listen",
	L"GetMessagesFromClientsWhenArrive",
	L"GetMessagesFromClients",
	L"AckMessagesReceipt",
	L"SendMessageToClient",
	L"SendMessageToRemoteServer",
	L"GetClientsState",
	L"StopServer",
	L"SendTerminationSignalToRunningInstanceOfServer",
	L"GetLastLogRecords",
};

static const wchar_t g_kClassNames[] = L"SynchClientServer";
static IAddInDefBase *pAsyncEvent = NULL;
static void *consumedMessage = NULL;

uint32_t convToShortWchar(WCHAR_T** Dest, const wchar_t* Source, uint32_t len = 0);
uint32_t convFromShortWchar(wchar_t** Dest, const WCHAR_T* Source, uint32_t len = 0);
uint32_t getLenShortWcharStr(const WCHAR_T* Source);
static AppCapabilities g_capabilities = eAppCapabilitiesInvalid;
static WcharWrapper s_names(g_kClassNames);
//---------------------------------------------------------------------------//
long GetClassObject(const WCHAR_T* /*wsName*/, IComponentBase** pInterface)
{
	if (!*pInterface)
	{
		*pInterface = new CAddInNative;
		intptr_t res = reinterpret_cast<intptr_t>(*pInterface);
		return static_cast<long>(res);
	}
	return 0;
}
//---------------------------------------------------------------------------//
AppCapabilities SetPlatformCapabilities(const AppCapabilities capabilities)
{
	g_capabilities = capabilities;
	return eAppCapabilitiesLast;
}
//---------------------------------------------------------------------------//
long DestroyObject(IComponentBase** pIntf)
{
	if (!*pIntf)
		return -1;

	delete *pIntf;
	*pIntf = 0;
	return 0;
}
//---------------------------------------------------------------------------//
const WCHAR_T* GetClassNames()
{
	return (const WCHAR_T*)s_names;
}
//---------------------------------------------------------------------------//
#if !defined( __linux__ ) && !defined(__APPLE__)
VOID CALLBACK MyTimerProc(PVOID lpParam, BOOLEAN TimerOrWaitFired);
#else
static void MyTimerProc(int sig);
#endif //__linux__

// CAddInNative
//---------------------------------------------------------------------------//
CAddInNative::CAddInNative()
{
	m_iMemory = 0;
	m_iConnect = 0;
}
//---------------------------------------------------------------------------//
CAddInNative::~CAddInNative()
{
}
//---------------------------------------------------------------------------//
bool CAddInNative::Init(VOID_PTR pConnection)
{
	synchClientServer_ = std::make_shared<SynchClientServer>();

	m_iConnect = (IAddInDefBase*)pConnection;
	return m_iConnect != NULL;
}
//---------------------------------------------------------------------------//
long CAddInNative::GetInfo()
{
	// Component should put supported component technology version 
	// This component supports 2.0 version
	return 2000;
}
//---------------------------------------------------------------------------//
void CAddInNative::Done()
{
	// synchClientServer_ delete automatically;
}
/////////////////////////////////////////////////////////////////////////////
// ILanguageExtenderBase
//---------------------------------------------------------------------------//
bool CAddInNative::RegisterExtensionAs(WCHAR_T** wsExtensionName)
{
	const wchar_t *wsExtension = g_kClassNames;
	size_t iActualSize = ::wcslen(wsExtension) + 1;

	if (m_iMemory)
	{
		if (m_iMemory->AllocMemory((void**)wsExtensionName, iActualSize * sizeof(WCHAR_T)))
			::convToShortWchar(wsExtensionName, wsExtension, iActualSize);
		return true;
	}

	return false;
}
//---------------------------------------------------------------------------//
long CAddInNative::GetNProps()
{
	// You may delete next lines and add your own implementation code here
	return ePropLast;
}
//---------------------------------------------------------------------------//
long CAddInNative::FindProp(const WCHAR_T* wsPropName)
{
	long plPropNum = -1;
	wchar_t* propName = 0;

	::convFromShortWchar(&propName, wsPropName);
	plPropNum = findName(g_PropNames, propName, ePropLast);

	if (plPropNum == -1)
		plPropNum = findName(g_PropNamesRu, propName, ePropLast);

	delete[] propName;

	return plPropNum;
}
//---------------------------------------------------------------------------//
const WCHAR_T* CAddInNative::GetPropName(long lPropNum, long lPropAlias)
{
	if (lPropNum >= ePropLast)
		return NULL;

	const wchar_t *wsCurrentName = NULL;
	WCHAR_T *wsPropName = NULL;
	int iActualSize = 0;

	switch (lPropAlias)
	{
	case 0: // First language
		wsCurrentName = g_PropNames[lPropNum];
		break;
	case 1: // Second language
		wsCurrentName = g_PropNamesRu[lPropNum];
		break;
	default:
		return 0;
	}

	iActualSize = wcslen(wsCurrentName) + 1;

	if (m_iMemory && wsCurrentName && (m_iMemory->AllocMemory((void**)&wsPropName, iActualSize * sizeof(WCHAR_T)))) {
		::convToShortWchar(&wsPropName, wsCurrentName, iActualSize);
	}

	return wsPropName;
}
//---------------------------------------------------------------------------//
bool CAddInNative::GetPropVal(const long lPropNum, tVariant* pvarPropVal)
{
	std::string prop;
	switch (lPropNum)
	{
	case ePropVersion:
		setWStringToTVariant(pvarPropVal, m_version);
		break;
	case eProploggingRequired:
		TV_VT(pvarPropVal) = VTYPE_BOOL;
		TV_BOOL(pvarPropVal) = synchClientServer_->getLoggingRequired();
		break;
	default:
		return false;
	}

	return true;
}
//---------------------------------------------------------------------------//
bool CAddInNative::SetPropVal(const long lPropNum, tVariant *varPropVal)
{
	switch (lPropNum)
	{
	case eProploggingRequired:
		if (TV_VT(varPropVal) != VTYPE_BOOL)
			return false;
		synchClientServer_->setLoggingRequired(TV_BOOL(varPropVal));
		break;
	default:
		return false;
	}

	return true;
}
//---------------------------------------------------------------------------//
bool CAddInNative::IsPropReadable(const long /*lPropNum*/)
{
	return true;
}
//---------------------------------------------------------------------------//
bool CAddInNative::IsPropWritable(const long lPropNum)
{
	switch (lPropNum)
	{
	case ePropVersion:
		return false;
	case eProploggingRequired:
		return true;
	default:
		return true;
	}
}
//---------------------------------------------------------------------------//
long CAddInNative::GetNMethods()
{
	return eMethLast;
}
//---------------------------------------------------------------------------//
long CAddInNative::FindMethod(const WCHAR_T* wsMethodName)
{
	long plMethodNum = -1;
	wchar_t* name = 0;

	::convFromShortWchar(&name, wsMethodName);

	plMethodNum = findName(g_MethodNames, name, eMethLast);

	if (plMethodNum == -1)
		plMethodNum = findName(g_MethodNamesRu, name, eMethLast);

	delete[] name;

	return plMethodNum;
}
//---------------------------------------------------------------------------//
const WCHAR_T* CAddInNative::GetMethodName(const long lMethodNum, const long lMethodAlias)
{
	if (lMethodNum >= eMethLast)
		return NULL;

	const wchar_t *wsCurrentName = NULL;
	WCHAR_T *wsMethodName = NULL;
	int iActualSize = 0;

	switch (lMethodAlias)
	{
	case 0: // First language
		wsCurrentName = g_MethodNames[lMethodNum];
		break;
	case 1: // Second language
		wsCurrentName = g_MethodNamesRu[lMethodNum];
		break;
	default:
		return 0;
	}

	iActualSize = wcslen(wsCurrentName) + 1;

	if (m_iMemory && wsCurrentName && (m_iMemory->AllocMemory((void**)&wsMethodName, iActualSize * sizeof(WCHAR_T)))) {
		::convToShortWchar(&wsMethodName, wsCurrentName, iActualSize);
	}

	return wsMethodName;
}
//---------------------------------------------------------------------------//
long CAddInNative::GetNParams(const long lMethodNum)
{
	switch (lMethodNum)
	{
	case eMethSetServerParameters:
		return 1;
	case eMethListen:
		return 0;
	case eMethGetMessagesFromClientsWhenArrive:
		return 3;
	case eMethGetMessagesFromClients:
		return 1;
	case eMethAckMessagesReceipt:
		return 1;
	case eMethSendMessageToClient:
		return 1;
	case eMethSendMessageToRemoteServer:
		return 1;
	case eMethGetClientsState:
		return 0;
	case eMethStopServer:
		return 0;
	case eMethSendTerminationSignalToRunningInstanceOfServer:
		return 0;
	case eMethGetLastLogRecords:
		return 2;
	default:
		return 0;
	}
}
//---------------------------------------------------------------------------//
bool CAddInNative::GetParamDefValue(const long lMethodNum, const long lParamNum,
	tVariant *pvarParamDefValue)
{
	TV_VT(pvarParamDefValue) = VTYPE_EMPTY;

	switch (lMethodNum)
	{
	case eMethGetLastLogRecords:
		if (lParamNum == 0) {
			TV_VT(pvarParamDefValue) = VTYPE_I4;
			TV_I4(pvarParamDefValue) = -1;
			return true;
		}
	case eMethSetServerParameters:
	case eMethListen:
	case eMethGetMessagesFromClientsWhenArrive:
	case eMethGetMessagesFromClients:
	case eMethAckMessagesReceipt:
	case eMethSendMessageToClient:
	case eMethSendMessageToRemoteServer:
	case eMethGetClientsState:
	case eMethStopServer:
	case eMethSendTerminationSignalToRunningInstanceOfServer:
		// There are no parameter values by default 
		break;
	default:
		return false;
	}

	return false;
}
//---------------------------------------------------------------------------//
bool CAddInNative::HasRetVal(const long lMethodNum)
{
	switch (lMethodNum)
	{
	case eMethListen:
	case eMethGetLastLogRecords:
	case eMethGetMessagesFromClientsWhenArrive:
	case eMethGetMessagesFromClients:
	case eMethGetClientsState:
		return true;
	default:
		return false;
	}
}
//---------------------------------------------------------------------------//
bool CAddInNative::CallAsProc(const long lMethodNum,
	tVariant* paParams, const long lSizeArray)
{

	if (!validateInputParameters(paParams, lMethodNum, lSizeArray))
		return false;

	switch (lMethodNum)
	{
	case eMethSetServerParameters:
		return synchClientServer_->setServerParameters(
			Utils::wsToString(paParams[0].pwstrVal)
		);
	case eMethAckMessagesReceipt:
		return synchClientServer_->ackMessagesReceipt(
			Utils::wsToString(paParams[0].pwstrVal)
		);
	case eMethSendMessageToClient:
	{
		return synchClientServer_->sendMessageToClient(
			Utils::wsToString(paParams[0].pwstrVal)
		);
	}
	case eMethSendMessageToRemoteServer:
	{
		return synchClientServer_->sendMessageToRemoteServer(
			Utils::wsToString(paParams[0].pwstrVal)
		);
	}
	case eMethStopServer:
		return synchClientServer_->stopServer();
	case eMethSendTerminationSignalToRunningInstanceOfServer:
		return synchClientServer_->sendTerminationSignalToRunningInstanceOfServer();
	default:
		return false;
	}
}

//---------------------------------------------------------------------------//
bool CAddInNative::CallAsFunc(const long lMethodNum,
	tVariant* pvarRetValue, tVariant* paParams, const long lSizeArray)
{
	if (!validateInputParameters(paParams, lMethodNum, lSizeArray))
		return false;

	switch (lMethodNum)
	{
	case eMethListen:
		return listen(pvarRetValue, paParams);
	case eMethGetLastLogRecords:
		return getLastLogRecords(pvarRetValue, paParams);
	case eMethGetMessagesFromClientsWhenArrive:
		return getMessagesFromClientsWhenArrive(pvarRetValue, paParams);
	case eMethGetMessagesFromClients:
		return getMessagesFromClients(pvarRetValue, paParams);
	case eMethGetClientsState:
		return getClientsState(pvarRetValue, paParams);
	default:
		return false;
	}
}

bool CAddInNative::listen(tVariant * pvarRetValue, tVariant * paParams)
{
	std::string error = synchClientServer_->listen();
	setWStringToTVariant(pvarRetValue, Utils::stringUnicodeToWs(error).c_str());
	TV_VT(pvarRetValue) = VTYPE_PWSTR;

	return true;
}

bool CAddInNative::getLastLogRecords(tVariant* pvarRetValue, tVariant* paParams) {
	std::string logRecordsStringJson = synchClientServer_->getLastRecordsFromLogHistory(paParams[0].intVal, paParams[1].bVal);
	setWStringToTVariant(pvarRetValue, Utils::stringUnicodeToWs(logRecordsStringJson).c_str());
	TV_VT(pvarRetValue) = VTYPE_PWSTR;

	return true;
}

bool CAddInNative::getClientsState(tVariant* pvarRetValue, tVariant* paParams) {
	std::string clientsState = synchClientServer_->getClientsState();
	setWStringToTVariant(pvarRetValue, Utils::stringToWs(clientsState).c_str());
	TV_VT(pvarRetValue) = VTYPE_PWSTR;

	return true;
}

bool CAddInNative::getMessagesFromClientsWhenArrive(tVariant* pvarRetValue, tVariant* paParams) {
	std::string outdata;

	bool hasNewMessages = synchClientServer_->getMessagesFromClientsWhenArrive(
		outdata,
		paParams[1].intVal,
		paParams[2].intVal
	);

	if (hasNewMessages)
	{
		setWStringToTVariant(&paParams[0], Utils::stringUnicodeToWs(outdata).c_str());
	}
	TV_VT(&paParams[0]) = VTYPE_PWSTR;

	TV_VT(pvarRetValue) = VTYPE_BOOL;
	TV_BOOL(pvarRetValue) = hasNewMessages;
	
	return true;
}

bool CAddInNative::getMessagesFromClients(tVariant * pvarRetValue, tVariant * paParams)
{
	std::string outdata;

	bool hasNewMessages = synchClientServer_->getMessagesFromClients(
		outdata
	);

	if (hasNewMessages)
	{
		setWStringToTVariant(&paParams[0], Utils::stringUnicodeToWs(outdata).c_str());
	}
	TV_VT(&paParams[0]) = VTYPE_PWSTR;

	TV_VT(pvarRetValue) = VTYPE_BOOL;
	TV_BOOL(pvarRetValue) = hasNewMessages;

	return true;
}

bool CAddInNative::validateInputParameters(tVariant* paParams, long const lMethodNum, long const lSizeArray) {

	switch (lMethodNum)
	{
	case eMethSetServerParameters:
	case eMethAckMessagesReceipt:
	case eMethSendMessageToClient:
	case eMethSendMessageToRemoteServer:
		return validateMethodWithOneStringParam(paParams, lMethodNum, lSizeArray);
	case eMethGetMessagesFromClientsWhenArrive:
	case eMethGetMessagesFromClients:
		return validateGetMessagesFromClientsWhenArrive(paParams, lMethodNum, lSizeArray);
	default:
		return true;
	}
}

bool CAddInNative::validateMethodWithOneStringParam(tVariant* paParams, long const lMethodNum, long const lSizeArray) {
	for (int i = 0; i < lSizeArray; i++)
	{	
		ENUMVAR typeCheck = VTYPE_PWSTR;
		if (!checkTypeInputParameter(paParams, lMethodNum, i, typeCheck))
			return false;

		if (!checkFullnessInputParameter(paParams, lMethodNum, i))
			return false;
	}
	return true;
}

bool CAddInNative::validateGetMessagesFromClientsWhenArrive(tVariant* paParams, long const lMethodNum, long const lSizeArray) {
	for (int i = 0; i < lSizeArray; i++)
	{
		ENUMVAR typeCheck = VTYPE_PWSTR;
		if (i == 1 || i == 2)
			typeCheck = VTYPE_I4;

		if (!checkTypeInputParameter(paParams, lMethodNum, i, typeCheck))
			return false;
		
		if (!checkFullnessInputParameter(paParams, lMethodNum, i))
			return false;
	}
	return true;
}

bool CAddInNative::checkTypeInputParameter(tVariant* params, long const methodNum, long const parameterNum, ENUMVAR type) {

	if (!(TV_VT(&params[parameterNum]) == type)) {
		std::string errDescr = "Error occured when calling method "
			+ Utils::wsToString(g_MethodNames[methodNum])
			+ "() - wrong type for parameter number "
			+ Utils::anyToString(parameterNum);

		addError(ADDIN_E_FAIL, L"SynchClientServer", Utils::stringToWs(errDescr).c_str(), 1);
		synchClientServer_->addLog(errDescr);
		return false;
	}
	return true;
}

bool CAddInNative::checkFullnessInputParameter(tVariant * params, const long methodNum, const long parameterNum)
{
	if (TV_VT(&params[parameterNum]) == VTYPE_PWSTR) {
		if (!params[0].pwstrVal){
			std::string errDescr = "Error occured when calling method "
				+ Utils::wsToString(g_MethodNames[methodNum])
				+ "() - parameter must not be empty "
				+ Utils::anyToString(parameterNum);

			addError(ADDIN_E_FAIL, L"SynchClientServer", Utils::stringToWs(errDescr).c_str(), 1);
			synchClientServer_->addLog(errDescr);
			return false;
		}
	}
	return true;
}

//---------------------------------------------------------------------------//
// This code will work only on the client!
#if !defined( __linux__ ) && !defined(__APPLE__)
VOID CALLBACK MyTimerProc(PVOID /*lpParam*/, BOOLEAN /*TimerOrWaitFired*/)
{
	if (!pAsyncEvent)
		return;

	wchar_t* who = L"ComponentNative";
	wchar_t* what = L"Timer";

	wchar_t *wstime = new wchar_t[TIME_LEN];
	if (wstime)
	{
		wmemset(wstime, 0, TIME_LEN);
		time_t vtime;
		time(&vtime);
		::_ui64tow_s(vtime, wstime, TIME_LEN, 10);
		pAsyncEvent->ExternalEvent(who, what, wstime);
		delete[] wstime;
	}
}
#else
void MyTimerProc(int sig)
{
	if (!pAsyncEvent)
		return;

	WCHAR_T *who = 0, *what = 0, *wdata = 0;
	wchar_t *data = 0;
	time_t dwTime = time(NULL);

	data = new wchar_t[TIME_LEN];

	if (data)
	{
		wmemset(data, 0, TIME_LEN);
		swprintf(data, TIME_LEN, L"%ul", dwTime);
		::convToShortWchar(&who, L"ComponentNative");
		::convToShortWchar(&what, L"Timer");
		::convToShortWchar(&wdata, data);

		pAsyncEvent->ExternalEvent(who, what, wdata);

		delete[] who;
		delete[] what;
		delete[] wdata;
		delete[] data;
	}
}
#endif
//---------------------------------------------------------------------------//
void CAddInNative::SetLocale(const WCHAR_T* loc)
{
#if !defined( __linux__ ) && !defined(__APPLE__)
	_wsetlocale(LC_ALL, loc);
#else
	//We convert in char* char_locale
	//also we establish locale
	//setlocale(LC_ALL, char_locale);
#endif
}

void CAddInNative::setStringToTVariant(tVariant* dest, std::string source) {
	size_t len = source.length();
	TV_VT(dest) = VTYPE_PSTR;
	if (m_iMemory->AllocMemory((void**)& dest->pstrVal, len))
		memcpy((void*)dest->pstrVal, (void*)source.c_str(), len);

	dest->strLen = len;
}

void CAddInNative::setWStringToTVariant(tVariant* dest, const wchar_t* source) {

	size_t len = ::wcslen(source) + 1;

	TV_VT(dest) = VTYPE_PWSTR;

	if (m_iMemory->AllocMemory((void**)& dest->pwstrVal, len * sizeof(WCHAR_T)))
		convToShortWchar(&dest->pwstrVal, source, len);
	dest->wstrLen = ::wcslen(source);
}
/////////////////////////////////////////////////////////////////////////////
// LocaleBase
//---------------------------------------------------------------------------//
bool CAddInNative::setMemManager(void* mem)
{
	m_iMemory = (IMemoryManager*)mem;
	return m_iMemory != 0;
}
//---------------------------------------------------------------------------//
void CAddInNative::addError(uint32_t wcode, const wchar_t* source,
	const wchar_t* descriptor, long code)
{
	if (m_iConnect)
	{
		WCHAR_T *err = 0;
		WCHAR_T *descr = 0;

		::convToShortWchar(&err, source);
		::convToShortWchar(&descr, descriptor);

		m_iConnect->AddError(wcode, err, descr, code);
		delete[] err;
		delete[] descr;
	}
}
//---------------------------------------------------------------------------//
long CAddInNative::findName(const wchar_t* names[], const wchar_t* name,
	const uint32_t size) const
{
	long ret = -1;
	for (uint32_t i = 0; i < size; i++)
	{
		if (!wcscmp(names[i], name))
		{
			ret = i;
			break;
		}
	}
	return ret;
}
//---------------------------------------------------------------------------//
uint32_t convToShortWchar(WCHAR_T** Dest, const wchar_t* Source, uint32_t len)
{
	if (!len)
		len = ::wcslen(Source) + 1;

	if (!*Dest)
		*Dest = new WCHAR_T[len];

	WCHAR_T* tmpShort = *Dest;
	const wchar_t* tmpWChar = Source;
	uint32_t res = 0;

	::memset(*Dest, 0, len * sizeof(WCHAR_T));
#ifdef __linux__
	size_t succeed = (size_t)-1;
	size_t f = len * sizeof(wchar_t), t = len * sizeof(WCHAR_T);
	const char* fromCode = sizeof(wchar_t) == 2 ? "UTF-16" : "UTF-32";
	iconv_t cd = iconv_open("UTF-16LE", fromCode);
	if (cd != (iconv_t)-1)
	{
		succeed = iconv(cd, (char**)&tmpWChar, &f, (char**)&tmpShort, &t);
		iconv_close(cd);
		if (succeed != (size_t)-1)
			return (uint32_t)succeed;
	}
#endif //__linux__
	for (; len; --len, ++res, ++tmpWChar, ++tmpShort)
	{
		*tmpShort = (WCHAR_T)*tmpWChar;
	}

	return res;
}
//---------------------------------------------------------------------------//
uint32_t convFromShortWchar(wchar_t** Dest, const WCHAR_T* Source, uint32_t len)
{
	if (!len)
		len = getLenShortWcharStr(Source) + 1;

	if (!*Dest)
		*Dest = new wchar_t[len];

	wchar_t* tmpWChar = *Dest;
	const  WCHAR_T* tmpShort = Source;
	uint32_t res = 0;

	::memset(*Dest, 0, len * sizeof(wchar_t));
#ifdef __linux__
	size_t succeed = (size_t)-1;
	const char* fromCode = sizeof(wchar_t) == 2 ? "UTF-16" : "UTF-32";
	size_t f = len * sizeof(WCHAR_T), t = len * sizeof(wchar_t);
	iconv_t cd = iconv_open("UTF-32LE", fromCode);
	if (cd != (iconv_t)-1)
	{
		succeed = iconv(cd, (char**)&tmpShort, &f, (char**)&tmpWChar, &t);
		iconv_close(cd);
		if (succeed != (size_t)-1)
			return (uint32_t)succeed;
	}
#endif //__linux__
	for (; len; --len, ++res, ++tmpWChar, ++tmpShort)
	{
		*tmpWChar = (wchar_t)*tmpShort;
	}

	return res;
}
//---------------------------------------------------------------------------//
uint32_t getLenShortWcharStr(const WCHAR_T* Source)
{
	uint32_t res = 0;
	const WCHAR_T *tmpShort = Source;

	while (*tmpShort++)
		++res;

	return res;
}
//---------------------------------------------------------------------------//

#ifdef LINUX_OR_MACOS
WcharWrapper::WcharWrapper(const WCHAR_T* str) : m_str_WCHAR(NULL),
m_str_wchar(NULL)
{
	if (str)
	{
		int len = getLenShortWcharStr(str);
		m_str_WCHAR = new WCHAR_T[len + 1];
		memset(m_str_WCHAR, 0, sizeof(WCHAR_T) * (len + 1));
		memcpy(m_str_WCHAR, str, sizeof(WCHAR_T) * len);
		::convFromShortWchar(&m_str_wchar, m_str_WCHAR);
	}
}
#endif
//---------------------------------------------------------------------------//
WcharWrapper::WcharWrapper(const wchar_t* str) :
#ifdef LINUX_OR_MACOS
	m_str_WCHAR(NULL),
#endif 
	m_str_wchar(NULL)
{
	if (str)
	{
		int len = wcslen(str);

		m_str_wchar = new wchar_t[len + 1];
		memset(m_str_wchar, 0, sizeof(wchar_t) * (len + 1));
		memcpy(m_str_wchar, str, sizeof(wchar_t) * len);
#ifdef LINUX_OR_MACOS
		::convToShortWchar(&m_str_WCHAR, m_str_wchar);
#endif
	}

}
//---------------------------------------------------------------------------//
WcharWrapper::~WcharWrapper()
{
#ifdef LINUX_OR_MACOS
	if (m_str_WCHAR)
	{
		delete[] m_str_WCHAR;
		m_str_WCHAR = NULL;
	}
#endif
	if (m_str_wchar)
	{
		delete[] m_str_wchar;
		m_str_wchar = NULL;
	}
}
//---------------------------------------------------------------------------//
