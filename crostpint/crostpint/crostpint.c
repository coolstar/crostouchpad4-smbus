#define DESCRIPTOR_DEF
#include "driver.h"
#include "stdint.h"

#define bool int
#define MHz 1000000

static ULONG CrosTpIntDebugLevel = 100;
static ULONG CrosTpIntDebugCatagories = DBG_INIT || DBG_PNP || DBG_IOCTL;

NTSTATUS
DriverEntry(
__in PDRIVER_OBJECT  DriverObject,
__in PUNICODE_STRING RegistryPath
)
{
	NTSTATUS               status = STATUS_SUCCESS;
	WDF_DRIVER_CONFIG      config;
	WDF_OBJECT_ATTRIBUTES  attributes;

	CrosTpIntPrint(DEBUG_LEVEL_INFO, DBG_INIT,
		"Driver Entry\n");

	WDF_DRIVER_CONFIG_INIT(&config, CrosTpIntEvtDeviceAdd);

	WDF_OBJECT_ATTRIBUTES_INIT(&attributes);

	//
	// Create a framework driver object to represent our driver.
	//

	status = WdfDriverCreate(DriverObject,
		RegistryPath,
		&attributes,
		&config,
		WDF_NO_HANDLE
		);

	if (!NT_SUCCESS(status))
	{
		CrosTpIntPrint(DEBUG_LEVEL_ERROR, DBG_INIT,
			"WdfDriverCreate failed with status 0x%x\n", status);
	}

	return status;
}

int CrosTpArg2 = 1;

VOID
CrosTpIntCallback(
	IN PCROSTPINT_CONTEXT pDevice,
	PCROSTPCALLBACK_PKT tpContext,
	PVOID Argument2
) {
	if (Argument2 == &CrosTpArg2) {
		return;
	}

	if (!tpContext) {
		return;
	}

	if (tpContext->signature != CrosTpSig) {
		return;
	}

	if (tpContext->actionSource != CrosTpCallbackSourceTouchpad) {
		return;
	}

	switch (tpContext->action) {
	case CrosTPCallbackActionRegister:
	{
		pDevice->touchpadDevContext = tpContext->actionParameters.registrationParameters.devContext;
		pDevice->touchpadCallbackFunction = tpContext->actionParameters.registrationParameters.callbackFunction;
		tpContext->actionStatus = STATUS_SUCCESS;
		break;
	}
	case CrosTPCallbackActionUnregister:
	{
		pDevice->touchpadDevContext = NULL;
		pDevice->touchpadCallbackFunction = NULL;
		tpContext->actionStatus = STATUS_SUCCESS;
		break;
	}
	default:
		break;
	}
}

NTSTATUS
OnPrepareHardware(
_In_  WDFDEVICE     FxDevice,
_In_  WDFCMRESLIST  FxResourcesRaw,
_In_  WDFCMRESLIST  FxResourcesTranslated
)
/*++

Routine Description:

This routine caches the SPB resource connection ID.

Arguments:

FxDevice - a handle to the framework device object
FxResourcesRaw - list of translated hardware resources that
the PnP manager has assigned to the device
FxResourcesTranslated - list of raw hardware resources that
the PnP manager has assigned to the device

Return Value:

Status

--*/
{
	PCROSTPINT_CONTEXT pDevice = GetDeviceContext(FxDevice);
	NTSTATUS status = STATUS_SUCCESS;

	UNREFERENCED_PARAMETER(FxResourcesRaw);

	//
	// Parse the peripheral's resources.
	//

	ULONG resourceCount = WdfCmResourceListGetCount(FxResourcesTranslated);

	for (ULONG i = 0; i < resourceCount; i++)
	{
		PCM_PARTIAL_RESOURCE_DESCRIPTOR pDescriptor;
		UCHAR Class;
		UCHAR Type;

		pDescriptor = WdfCmResourceListGetDescriptor(
			FxResourcesTranslated, i);

		switch (pDescriptor->Type)
		{
		default:
			//
			// Ignoring all other resource types.
			//
			break;
		}
	}

	UNICODE_STRING CrosTpSMBusInterrupt;
	RtlInitUnicodeString(&CrosTpSMBusInterrupt, L"\\CallBack\\CrosTpSMBusInterrupt");

	OBJECT_ATTRIBUTES attributes;
	InitializeObjectAttributes(&attributes,
		&CrosTpSMBusInterrupt,
		OBJ_KERNEL_HANDLE | OBJ_OPENIF | OBJ_CASE_INSENSITIVE | OBJ_PERMANENT,
		NULL,
		NULL
	);

	status = ExCreateCallback(&pDevice->CrosTpIntCallback, &attributes, TRUE, TRUE);
	if (!NT_SUCCESS(status)) {

		return status;
	}
	pDevice->CrosTpCallbackObj = ExRegisterCallback(pDevice->CrosTpIntCallback,
		CrosTpIntCallback,
		pDevice
	);
	if (!pDevice->CrosTpCallbackObj) {

		return STATUS_NO_CALLBACK_ACTIVE;
	}

	RtlZeroMemory(&pDevice->CrosTpCallbackTempContext, sizeof(CROSTPCALLBACK_PKT));
	pDevice->CrosTpCallbackTempContext.signature = CrosTpSig;
	pDevice->CrosTpCallbackTempContext.action = CrosTPCallbackActionRegister;
	pDevice->CrosTpCallbackTempContext.actionSource = CrosTpCallbackSourceInterrupt;
	pDevice->CrosTpCallbackTempContext.actionStatus = STATUS_DEVICE_NOT_READY;
	ExNotifyCallback(pDevice->CrosTpIntCallback, &pDevice->CrosTpCallbackTempContext, &CrosTpArg2);

	return status;
}

NTSTATUS
OnReleaseHardware(
_In_  WDFDEVICE     FxDevice,
_In_  WDFCMRESLIST  FxResourcesTranslated
)
/*++

Routine Description:

Arguments:

FxDevice - a handle to the framework device object
FxResourcesTranslated - list of raw hardware resources that
the PnP manager has assigned to the device

Return Value:

Status

--*/
{
	PCROSTPINT_CONTEXT pDevice = GetDeviceContext(FxDevice);
	NTSTATUS status = STATUS_SUCCESS;

	UNREFERENCED_PARAMETER(FxResourcesTranslated);

	if (pDevice->CrosTpCallbackObj) {
		RtlZeroMemory(&pDevice->CrosTpCallbackTempContext, sizeof(CROSTPCALLBACK_PKT));
		pDevice->CrosTpCallbackTempContext.signature = CrosTpSig;
		pDevice->CrosTpCallbackTempContext.action = CrosTPCallbackActionUnregister;
		pDevice->CrosTpCallbackTempContext.actionSource = CrosTpCallbackSourceInterrupt;
		pDevice->CrosTpCallbackTempContext.actionStatus = STATUS_DEVICE_NOT_READY;
		ExNotifyCallback(pDevice->CrosTpIntCallback, &pDevice->CrosTpCallbackTempContext, &CrosTpArg2); //Notify one last time before unregistration

		ExUnregisterCallback(pDevice->CrosTpCallbackObj);
		pDevice->CrosTpCallbackObj = NULL;
	}

	if (pDevice->CrosTpIntCallback) {
		ObDereferenceObject(pDevice->CrosTpIntCallback);
		pDevice->CrosTpIntCallback = NULL;
	}

	return status;
}

NTSTATUS
OnD0Entry(
_In_  WDFDEVICE               FxDevice,
_In_  WDF_POWER_DEVICE_STATE  FxPreviousState
)
/*++

Routine Description:

This routine allocates objects needed by the driver.

Arguments:

FxDevice - a handle to the framework device object
FxPreviousState - previous power state

Return Value:

Status

--*/
{
	UNREFERENCED_PARAMETER(FxPreviousState);

	PCROSTPINT_CONTEXT pDevice = GetDeviceContext(FxDevice);
	NTSTATUS status = STATUS_SUCCESS;

	pDevice->ConnectInterrupt = true;

	return status;
}

NTSTATUS
OnD0Exit(
_In_  WDFDEVICE               FxDevice,
_In_  WDF_POWER_DEVICE_STATE  FxPreviousState
)
/*++

Routine Description:

This routine destroys objects needed by the driver.

Arguments:

FxDevice - a handle to the framework device object
FxPreviousState - previous power state

Return Value:

Status

--*/
{
	UNREFERENCED_PARAMETER(FxPreviousState);

	PCROSTPINT_CONTEXT pDevice = GetDeviceContext(FxDevice);

	pDevice->ConnectInterrupt = false;

	return STATUS_SUCCESS;
}

BOOLEAN OnInterruptIsr(
	WDFINTERRUPT Interrupt,
	ULONG MessageID) {
	UNREFERENCED_PARAMETER(MessageID);

	WDFDEVICE Device = WdfInterruptGetDevice(Interrupt);
	PCROSTPINT_CONTEXT pDevice = GetDeviceContext(Device);

	if (!pDevice->ConnectInterrupt)
		return false;

	BOOLEAN retVal = false;
	if (pDevice->touchpadCallbackFunction) {
		retVal = pDevice->touchpadCallbackFunction(pDevice->touchpadDevContext);
	}

	return retVal;
}

NTSTATUS
CrosTpIntEvtDeviceAdd(
IN WDFDRIVER       Driver,
IN PWDFDEVICE_INIT DeviceInit
)
{
	NTSTATUS                      status = STATUS_SUCCESS;
	WDF_IO_QUEUE_CONFIG           queueConfig;
	WDF_OBJECT_ATTRIBUTES         attributes;
	WDFDEVICE                     device;
	WDF_INTERRUPT_CONFIG interruptConfig;
	WDFQUEUE                      queue;
	UCHAR                         minorFunction;
	PCROSTPINT_CONTEXT               devContext;

	UNREFERENCED_PARAMETER(Driver);

	PAGED_CODE();

	CrosTpIntPrint(DEBUG_LEVEL_INFO, DBG_PNP,
		"CrosTpIntEvtDeviceAdd called\n");

	{
		WDF_PNPPOWER_EVENT_CALLBACKS pnpCallbacks;
		WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpCallbacks);

		pnpCallbacks.EvtDevicePrepareHardware = OnPrepareHardware;
		pnpCallbacks.EvtDeviceReleaseHardware = OnReleaseHardware;
		pnpCallbacks.EvtDeviceD0Entry = OnD0Entry;
		pnpCallbacks.EvtDeviceD0Exit = OnD0Exit;

		WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpCallbacks);
	}

	//
	// Setup the device context
	//

	WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, CROSTPINT_CONTEXT);

	//
	// Create a framework device object.This call will in turn create
	// a WDM device object, attach to the lower stack, and set the
	// appropriate flags and attributes.
	//

	status = WdfDeviceCreate(&DeviceInit, &attributes, &device);

	if (!NT_SUCCESS(status))
	{
		CrosTpIntPrint(DEBUG_LEVEL_ERROR, DBG_PNP,
			"WdfDeviceCreate failed with status code 0x%x\n", status);

		return status;
	}

	WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueConfig, WdfIoQueueDispatchParallel);

	queueConfig.EvtIoInternalDeviceControl = CrosTpIntEvtInternalDeviceControl;

	status = WdfIoQueueCreate(device,
		&queueConfig,
		WDF_NO_OBJECT_ATTRIBUTES,
		&queue
		);

	if (!NT_SUCCESS(status))
	{
		CrosTpIntPrint(DEBUG_LEVEL_ERROR, DBG_PNP,
			"WdfIoQueueCreate failed 0x%x\n", status);

		return status;
	}

	//
	// Create manual I/O queue to take care of hid report read requests
	//

	devContext = GetDeviceContext(device);
	devContext->FxDevice = device;

	//
	// Create an interrupt object for hardware notifications
	//
	WDF_INTERRUPT_CONFIG_INIT(
		&interruptConfig,
		OnInterruptIsr,
		NULL);
	interruptConfig.PassiveHandling = TRUE;

	status = WdfInterruptCreate(
		device,
		&interruptConfig,
		WDF_NO_OBJECT_ATTRIBUTES,
		&devContext->Interrupt);

	if (!NT_SUCCESS(status))
	{
		CrosTpIntPrint(DEBUG_LEVEL_ERROR, DBG_PNP,
			"Error creating WDF interrupt object - %!STATUS!",
			status);

		return status;
	}

	WDF_DEVICE_POWER_POLICY_WAKE_SETTINGS wakeSettings;
	WDF_DEVICE_POWER_POLICY_WAKE_SETTINGS_INIT(&wakeSettings);
	wakeSettings.DxState = PowerDeviceD3;
	wakeSettings.UserControlOfWakeSettings = WakeAllowUserControl;
	wakeSettings.Enabled = WdfTrue;

	status = WdfDeviceAssignSxWakeSettings(device, &wakeSettings);

	if (!NT_SUCCESS(status))
	{
		CrosTpIntPrint(DEBUG_LEVEL_ERROR, DBG_PNP,
			"Error setting Wake Settings - %!STATUS!",
			status);

		return status;
	}

	return status;
}

VOID
CrosTpIntEvtInternalDeviceControl(
IN WDFQUEUE     Queue,
IN WDFREQUEST   Request,
IN size_t       OutputBufferLength,
IN size_t       InputBufferLength,
IN ULONG        IoControlCode
)
{
	NTSTATUS            status = STATUS_SUCCESS;
	WDFDEVICE           device;
	PCROSTPINT_CONTEXT     devContext;

	UNREFERENCED_PARAMETER(OutputBufferLength);
	UNREFERENCED_PARAMETER(InputBufferLength);

	device = WdfIoQueueGetDevice(Queue);
	devContext = GetDeviceContext(device);

	switch (IoControlCode)
	{
	default:
		status = STATUS_NOT_SUPPORTED;
		break;
	}

	WdfRequestComplete(Request, status);

	return;
}
