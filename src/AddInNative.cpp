#include <boost/asio.hpp>

#pragma warning( disable : 4267)
#pragma warning( disable : 4311)
#pragma warning( disable : 4302)

#if defined(__linux__)
#define EXPORT __attribute__ ((visibility ("default")))
#else
#define EXPORT
#endif

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
#include <string>
#include <codecvt>

#include "AddInNative.h"

static const char16_t* g_PropNames[] = {
	u"Version",
	u"loggingRequired",
};
static const char16_t* g_MethodNames[] = {
	u"SetServerParameters",
	u"Listen",
	u"GetMessagesFromClientsWhenArrive",
	u"GetMessagesFromClients",
	u"AckMessagesReceipt",
	u"SendMessageToClient",
	u"SendMessageToRemoteServer",
	u"GetClientsState",
	u"StopServer",
	u"SendTerminationSignalToRunningInstanceOfServer",
	u"GetLastLogRecords",
};

static const char16_t* g_PropNamesRu[] = {
	u"Version",
	u"loggingRequired",
};
static const char16_t* g_MethodNamesRu[] = {
	u"SetServerParameters",
	u"Listen",
	u"GetMessagesFromClientsWhenArrive",
	u"GetMessagesFromClients",
	u"AckMessagesReceipt",
	u"SendMessageToClient",
	u"SendMessageToRemoteServer",
	u"GetClientsState",
	u"StopServer",
	u"SendTerminationSignalToRunningInstanceOfServer",
	u"GetLastLogRecords",
};

const char16_t* CAddInNative::componentName = u"SynchClientServer";
static AppCapabilities g_capabilities = eAppCapabilitiesInvalid;

//---------------------------------------------------------------------------//
EXPORT long GetClassObject(const WCHAR_T *wsName, IComponentBase **pInterface) {
    if (!*pInterface) {
        *pInterface = new CAddInNative();
        return (long) (*pInterface);
    }
    return 0;
}

//---------------------------------------------------------------------------//
EXPORT AppCapabilities SetPlatformCapabilities(const AppCapabilities capabilities) {
    g_capabilities = capabilities;
    return eAppCapabilitiesLast;
}

//---------------------------------------------------------------------------//
EXPORT long DestroyObject(IComponentBase **pInterface) {
    if (!*pInterface)
        return -1;

    delete *pInterface;
    *pInterface = nullptr;
    return 0;
}

//---------------------------------------------------------------------------//
EXPORT const WCHAR_T *GetClassNames() {
    return CAddInNative::componentName;
}

// CAddInNative
//---------------------------------------------------------------------------//
CAddInNative::CAddInNative() {
	LOGD("construct");
}

//---------------------------------------------------------------------------//
CAddInNative::~CAddInNative() {
	LOGD("destruct");
}

//---------------------------------------------------------------------------//
bool CAddInNative::Init(VOID_PTR pConnection) {
	LOGD("init start");
	synchClientServer_ = std::make_shared<SynchClientServer>();
	bool ret = synchClientServer_->init(static_cast<IAddInDefBase*>(pConnection));
	LOGD("init end");
	return ret;
}

//---------------------------------------------------------------------------//
long CAddInNative::GetInfo() {
	// Component should put supported component technology version 
	// This component supports 2.0 version
	return 2000;
}

//---------------------------------------------------------------------------//
void CAddInNative::Done() {
	LOGD("done start");
	synchClientServer_->done();
	LOGD("done end");
}

/////////////////////////////////////////////////////////////////////////////
// ILanguageExtenderBase
//---------------------------------------------------------------------------//
bool CAddInNative::RegisterExtensionAs(WCHAR_T** wsExtensionName) {
	return synchClientServer_->memoryManager().copyString((char16_t**)wsExtensionName, componentName);
}

//---------------------------------------------------------------------------//
long CAddInNative::GetNProps() {
	// You may delete next lines and add your own implementation code here
	return ePropLast;
}

//---------------------------------------------------------------------------//
long CAddInNative::FindProp(const WCHAR_T* wsPropName) {
	long plPropNum = -1;
	plPropNum = findName(g_PropNames, (char16_t*)wsPropName, ePropLast);

	if (plPropNum == -1)
		plPropNum = findName(g_PropNamesRu, (char16_t*)wsPropName, ePropLast);

	if (plPropNum == -1)
		synchClientServer_->setLastError(u"Property not found: " + u16string((char16_t*)wsPropName));

	return plPropNum;
}

//---------------------------------------------------------------------------//
const WCHAR_T* CAddInNative::GetPropName(long lPropNum, long lPropAlias) {
	if (lPropNum >= ePropLast)
		return NULL;

	const char16_t* wsCurrentName = NULL;

	switch (lPropAlias) {
	case 0: // First language
		wsCurrentName = g_PropNames[lPropNum];
		break;
	case 1: // Second language
		wsCurrentName = g_PropNamesRu[lPropNum];
		break;
	default:
		return 0;
	}

	return (WCHAR_T*)synchClientServer_->memoryManager().allocString(wsCurrentName);
}

//---------------------------------------------------------------------------//
bool CAddInNative::GetPropVal(const long lPropNum, tVariant* pvarPropVal) {
	LOGD("1C get prop start " + to_string(lPropNum));
	bool ret = false;
	switch (lPropNum) {
	case ePropVersion:
		ret = synchClientServer_->getVersion(pvarPropVal);
		break;
	case eProploggingRequired:
		ret = synchClientServer_->getPropertyLoggingRequired(pvarPropVal, lPropNum);
		break;
	default:
		ret = false;
		break;
	}
	LOGD("1C get prop end " + to_string(lPropNum));
	return ret;
}

//---------------------------------------------------------------------------//
bool CAddInNative::SetPropVal(const long lPropNum, tVariant* varPropVal) {
	LOGD("1C set prop start " + to_string(lPropNum));
	bool ret = false;
	switch (lPropNum) {
	default:
		ret = false;
		break;
	}
	LOGD("1C set prop end " + to_string(lPropNum));
	return ret;
}

//---------------------------------------------------------------------------//
bool CAddInNative::IsPropReadable(const long /*lPropNum*/) {
	return true;
}

//---------------------------------------------------------------------------//
bool CAddInNative::IsPropWritable(const long lPropNum) {
	return false;
}

//---------------------------------------------------------------------------//
long CAddInNative::GetNMethods() {
	return eMethLast;
}

//---------------------------------------------------------------------------//
long CAddInNative::FindMethod(const WCHAR_T* wsMethodName) {
	long plMethodNum = -1;
	plMethodNum = findName(g_MethodNames, (char16_t*)wsMethodName, eMethLast);

	if (plMethodNum == -1)
		plMethodNum = findName(g_MethodNamesRu, (char16_t*)wsMethodName, eMethLast);

	if (plMethodNum == -1)
		synchClientServer_->setLastError(u"Method not found: " + u16string((char16_t*)wsMethodName));

	return plMethodNum;
}

//---------------------------------------------------------------------------//
const WCHAR_T* CAddInNative::GetMethodName(const long lMethodNum, const long lMethodAlias) {
	if (lMethodNum >= eMethLast)
		return NULL;

	const char16_t* wsCurrentName = NULL;

	switch (lMethodAlias) {
	case 0: // First language
		wsCurrentName = g_MethodNames[lMethodNum];
		break;
	case 1: // Second language
		wsCurrentName = g_MethodNamesRu[lMethodNum];
		break;
	default:
		return 0;
	}
	return (WCHAR_T*)synchClientServer_->memoryManager().allocString(wsCurrentName);
}

//---------------------------------------------------------------------------//
long CAddInNative::GetNParams(const long lMethodNum) {
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
	tVariant* pvarParamDefValue) {
	
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
bool CAddInNative::HasRetVal(const long lMethodNum) {
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
	tVariant* paParams, const long lSizeArray) {
	LOGD("1C call proc start " + to_string(lMethodNum));
	bool ret = false;
	switch (lMethodNum)
	{
	case eMethSetServerParameters:
		ret = synchClientServer_->setServerParameters(paParams, lSizeArray);
		break;
	case eMethAckMessagesReceipt:
		ret = synchClientServer_->ackMessagesReceipt(paParams, lSizeArray);
		break;
	case eMethSendMessageToClient:
		ret = synchClientServer_->sendMessageToClient(paParams, lSizeArray);
		break;
	case eMethSendMessageToRemoteServer:
		ret = synchClientServer_->sendMessageToRemoteServer(paParams, lSizeArray);
		break;
	case eMethStopServer:
		ret = synchClientServer_->stopServer(paParams, lSizeArray);
		break;
	case eMethSendTerminationSignalToRunningInstanceOfServer:
		ret = synchClientServer_->sendTerminationSignalToRunningInstanceOfServer(paParams, lSizeArray);
		break;
	default:
		ret = false;
		break;
	}
	LOGD("1C call proc end " + to_string(lMethodNum));
	return ret;
}

//---------------------------------------------------------------------------//
bool CAddInNative::CallAsFunc(const long lMethodNum,
	tVariant* pvarRetValue, tVariant* paParams,
	const long lSizeArray) {
	LOGD("1C call func start " + to_string(lMethodNum));
	bool ret = false;
	switch (lMethodNum)
	{
	case eMethListen:
		ret = synchClientServer_->listen(pvarRetValue, paParams, lSizeArray);
		break;
	case eMethGetLastLogRecords:
		ret = synchClientServer_->getLastLogRecords(pvarRetValue, paParams, lSizeArray);
		break;
	case eMethGetMessagesFromClientsWhenArrive:
		ret = synchClientServer_->getMessagesFromClientsWhenArrive(pvarRetValue, paParams, lSizeArray);
		break;
	case eMethGetMessagesFromClients:
		ret = synchClientServer_->getMessagesFromClients(pvarRetValue, paParams, lSizeArray);
		break;
	case eMethGetClientsState:
		ret = synchClientServer_->getClientsState(pvarRetValue, paParams, lSizeArray);
		break;
	default:
		ret = false;
		break;
	}
	LOGD("1C call func end " + to_string(lMethodNum));
	return ret;
}


//---------------------------------------------------------------------------//
void CAddInNative::SetLocale(const WCHAR_T* loc) {
#if !defined( __linux__ ) && !defined(__APPLE__)
	_wsetlocale(LC_ALL, (wchar_t*)loc);
#else
	//We convert in char* char_locale
	//also we establish locale
	//setlocale(LC_ALL, char_locale);
#endif
}

/////////////////////////////////////////////////////////////////////////////
// LocaleBase
//---------------------------------------------------------------------------//
bool CAddInNative::setMemManager(void* mem) {
	synchClientServer_->memoryManager().setHandle((IMemoryManager*)mem);
	return mem != 0;
}

//---------------------------------------------------------------------------//
long CAddInNative::findName(const char16_t* names[], u16string name, const uint32_t size) const {
	long ret = -1;
	for (uint32_t i = 0; i < size; i++) {
		if (name == names[i]) {
			ret = i;
			break;
		}
	}
	return ret;
}
