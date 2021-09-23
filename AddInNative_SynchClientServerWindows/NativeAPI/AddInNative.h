#pragma once
#ifndef __ADDINNATIVE_H__
#define __ADDINNATIVE_H__

#include "ComponentBase.h"
#include "AddInDefBase.h"
#include "IMemoryManager.h"
#include "src/Utils.h"
#include  <string>
#include "src/SynchServer.h"

typedef void* VOID_PTR;

#pragma setlocale("ru-RU" )

///////////////////////////////////////////////////////////////////////////////
// class CAddInNative
class CAddInNative : public IComponentBase
{
public:
    enum Props
    {
		ePropVersion = 0,
		eProploggingRequired,
		ePropLast // Always last
    };

    enum Methods
    {
		eMethSetServerParameters = 0,
		eMethListen,
		eMethGetMessagesFromClientsWhenArrive,
		eMethGetMessagesFromClients,
		eMethAckMessagesReceipt,
		eMethSendMessageToClient,
		eMethSendMessageToRemoteServer,
		eMethGetClientsState,
		eMethStopServer,
		eMethGetLastLogRecords,
		eMethLast     // Always last
    };

    CAddInNative(void);
    virtual ~CAddInNative();
    // IInitDoneBase
    virtual bool ADDIN_API Init(VOID_PTR);
    virtual bool ADDIN_API setMemManager(void* mem);
    virtual long ADDIN_API GetInfo();
    virtual void ADDIN_API Done();
    // ILanguageExtenderBase
    virtual bool ADDIN_API RegisterExtensionAs(WCHAR_T**);
    virtual long ADDIN_API GetNProps();
    virtual long ADDIN_API FindProp(const WCHAR_T* wsPropName);
    virtual const WCHAR_T* ADDIN_API GetPropName(long lPropNum, long lPropAlias);
    virtual bool ADDIN_API GetPropVal(const long lPropNum, tVariant* pvarPropVal);
    virtual bool ADDIN_API SetPropVal(const long lPropNum, tVariant* varPropVal);
    virtual bool ADDIN_API IsPropReadable(const long lPropNum);
    virtual bool ADDIN_API IsPropWritable(const long lPropNum);
    virtual long ADDIN_API GetNMethods();
    virtual long ADDIN_API FindMethod(const WCHAR_T* wsMethodName);
    virtual const WCHAR_T* ADDIN_API GetMethodName(const long lMethodNum, 
                            const long lMethodAlias);
    virtual long ADDIN_API GetNParams(const long lMethodNum);
    virtual bool ADDIN_API GetParamDefValue(const long lMethodNum, const long lParamNum,
                            tVariant *pvarParamDefValue);   
    virtual bool ADDIN_API HasRetVal(const long lMethodNum);
    virtual bool ADDIN_API CallAsProc(const long lMethodNum,
                    tVariant* paParams, const long lSizeArray);
    virtual bool ADDIN_API CallAsFunc(const long lMethodNum,
                tVariant* pvarRetValue, tVariant* paParams, const long lSizeArray);
    // LocaleBase
    virtual void ADDIN_API SetLocale(const WCHAR_T* loc);
	void setWStringToTVariant(tVariant* dest, const wchar_t* source);
	void setStringToTVariant(tVariant* dest, std::string source);
    
private:

	synchServer_Sptr synchClientServer_;

    long findName(const wchar_t* names[], const wchar_t* name, const uint32_t size) const;
    void addError(uint32_t wcode, const wchar_t* source, const wchar_t* descriptor, long code);
 

	bool listen(tVariant* pvarRetValue, tVariant* paParams);
	bool getLastLogRecords(tVariant* pvarRetValue, tVariant* paParams);
	bool getClientsState(tVariant* pvarRetValue, tVariant* paParams);
	bool getMessagesFromClientsWhenArrive(tVariant* pvarRetValue, tVariant* paParams);
	bool getMessagesFromClients(tVariant* pvarRetValue, tVariant* paParams);

	bool checkTypeInputParameter(tVariant* params, const long methodNum, const long index, ENUMVAR type);
	bool checkFullnessInputParameter(tVariant* params, const long methodNum, const long index);
	bool validateInputParameters(tVariant* paParams, long const lMethodNum, long const lSizeArray);
	bool validateMethodWithOneStringParam(tVariant* paParams, long const lMethodNum, long const lSizeArray);
	bool validateGetMessagesFromClientsWhenArrive(tVariant* paParams, long const lMethodNum, long const lSizeArray);

	// Attributes
    IAddInDefBase      *m_iConnect;
    IMemoryManager     *m_iMemory;

	const wchar_t*      m_version = L"1.0";
};

class WcharWrapper
{
public:
#ifdef LINUX_OR_MACOS
    WcharWrapper(const WCHAR_T* str);
#endif
    WcharWrapper(const wchar_t* str);
	WcharWrapper(const WcharWrapper &) = delete;
	WcharWrapper(const WcharWrapper &&) = delete;
    ~WcharWrapper();
#ifdef LINUX_OR_MACOS
    operator const WCHAR_T*(){ return m_str_WCHAR; }
    operator WCHAR_T*(){ return m_str_WCHAR; }
#endif
	explicit operator const wchar_t*() { return m_str_wchar; }
	explicit operator wchar_t*() { return m_str_wchar; }
	WcharWrapper &operator==(const WcharWrapper &) = delete;
	WcharWrapper &operator==(const WcharWrapper &&) = delete;

#ifdef LINUX_OR_MACOS
    WCHAR_T* m_str_WCHAR;
#endif
    wchar_t* m_str_wchar;
};
#endif //__ADDINNATIVE_H__
