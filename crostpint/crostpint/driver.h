#if !defined(_CROSTPINT_H_)
#define _CROSTPINT_H_

#pragma warning(disable:4200)  // suppress nameless struct/union warning
#pragma warning(disable:4201)  // suppress nameless struct/union warning
#pragma warning(disable:4214)  // suppress bit field types other than int warning
#include <initguid.h>
#include <wdm.h>
#include <wdmguid.h>

#pragma warning(default:4200)
#pragma warning(default:4201)
#pragma warning(default:4214)
#include <wdf.h>

#pragma warning(disable:4201)  // suppress nameless struct/union warning
#pragma warning(disable:4214)  // suppress bit field types other than int warning

//
// String definitions
//

#define DRIVERNAME                 "crostpint.sys: "

#define CROSTPINT_POOL_TAG            (ULONG) 'msyC'

#define true 1
#define false 0

#define CrosTpSig 'ptyC'

typedef enum {
	CrosTpCallbackSourceTouchpad = 0,
	CrosTpCallbackSourceInterrupt = 1
} CrosTpCallbackSource;

typedef enum {
	CrosTPCallbackActionRegister = 0,
	CrosTPCallbackActionUnregister = 1
} CrosTPCallbackAction;

typedef BOOLEAN(*CrosTPInt_Callback)(PVOID devContext);

typedef struct _CROSTPCALLBACK_PKT
{
	UINT32 signature;

	CrosTPCallbackAction action;
	CrosTpCallbackSource actionSource;
	NTSTATUS actionStatus;

	union {
		struct {
			PVOID devContext;
			CrosTPInt_Callback callbackFunction;
		} registrationParameters;
	} actionParameters;
} CROSTPCALLBACK_PKT, *PCROSTPCALLBACK_PKT;

typedef struct _CROSTPINT_CONTEXT
{

	//
	// Handle back to the WDFDEVICE
	//

	WDFDEVICE FxDevice;

	WDFINTERRUPT Interrupt;

	PCALLBACK_OBJECT CrosTpIntCallback;
	PVOID CrosTpCallbackObj;
	CROSTPCALLBACK_PKT CrosTpCallbackTempContext;

	PVOID touchpadDevContext;
	CrosTPInt_Callback touchpadCallbackFunction;

	BOOLEAN ConnectInterrupt;

} CROSTPINT_CONTEXT, *PCROSTPINT_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(CROSTPINT_CONTEXT, GetDeviceContext)

//
// Function definitions
//

DRIVER_INITIALIZE DriverEntry;

EVT_WDF_DRIVER_UNLOAD CrosTpIntDriverUnload;

EVT_WDF_DRIVER_DEVICE_ADD CrosTpIntEvtDeviceAdd;

EVT_WDF_IO_QUEUE_IO_INTERNAL_DEVICE_CONTROL CrosTpIntEvtInternalDeviceControl;

//
// Helper macros
//

#define DEBUG_LEVEL_ERROR   1
#define DEBUG_LEVEL_INFO    2
#define DEBUG_LEVEL_VERBOSE 3

#define DBG_INIT  1
#define DBG_PNP   2
#define DBG_IOCTL 4

#if 0
#define CrosTpIntPrint(dbglevel, dbgcatagory, fmt, ...) {          \
    if (CrosTpIntDebugLevel >= dbglevel &&                         \
        (CrosTpIntDebugCatagories && dbgcatagory))                 \
		    {                                                           \
        DbgPrint(DRIVERNAME);                                   \
        DbgPrint(fmt, __VA_ARGS__);                             \
		    }                                                           \
}
#else
#define CrosTpIntPrint(dbglevel, fmt, ...) {                       \
}
#endif
#endif