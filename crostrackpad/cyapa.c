#define DESCRIPTOR_DEF
#include "driver.h"
#include <initguid.h>
#include <wdmguid.h>

static ULONG CyapaDebugLevel = 100;
static ULONG CyapaDebugCatagories = DBG_INIT || DBG_PNP || DBG_IOCTL;

static bool deviceLoaded = false;

static unsigned char inb_p(unsigned short int port) {
	unsigned char value = __inbyte(port);
	//DbgPrint("SMBus Read 0x%x: 0x%x\n", port, value);
	return value;
}

static void outb_p(unsigned char value, unsigned short int port) {
	//DbgPrint("SMBus Write 0x%x: 0x%x\n", port, value);
	__outbyte(port, value);
}

uint32_t cyapa_read_byte(PCYAPA_CONTEXT pDevice, uint8_t cmd, SMBUS_USER_CALLBACK callback, PVOID arg);
uint32_t cyapa_write_byte(PCYAPA_CONTEXT pDevice, uint8_t cmd, uint8_t value, SMBUS_USER_CALLBACK callback, PVOID arg);
uint32_t cyapa_read_block(PCYAPA_CONTEXT pDevice, uint8_t cmd, SMBUS_USER_CALLBACK callback, PVOID arg);
uint8_t cyapa_write_block(PCYAPA_CONTEXT pDevice, uint8_t cmd, uint8_t *buf, uint8_t len, SMBUS_USER_CALLBACK callback, PVOID arg);
void SetDefaultSettings(struct csgesture_softc* sc);
void CyapaTimerFunc(_In_ WDFTIMER hTimer);
void ProcessSetting(PCYAPA_CONTEXT pDevice, struct csgesture_softc* sc, int settingRegister, int settingValue);

static int sqr(int num) {
	return num * num;
}

static int diffsig(int x, int y, int lastx, int lasty) {
	uint32_t distsq = sqr(x - lastx) + sqr(y - lasty);
	if (distsq < 4)
		return 0;
	return 1;
}

NTSTATUS
DriverEntry(
__in PDRIVER_OBJECT  DriverObject,
__in PUNICODE_STRING RegistryPath
)
{
	NTSTATUS               status = STATUS_SUCCESS;
	WDF_DRIVER_CONFIG      config;
	WDF_OBJECT_ATTRIBUTES  attributes;

	CyapaPrint(DEBUG_LEVEL_INFO, DBG_INIT,
		"Driver Entry\n");

	WDF_DRIVER_CONFIG_INIT(&config, CyapaEvtDeviceAdd);

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
		CyapaPrint(DEBUG_LEVEL_ERROR, DBG_INIT,
			"WdfDriverCreate failed with status 0x%x\n", status);
	}

	return status;
}

VOID cyapa_set_full_power(
	IN PCYAPA_CONTEXT pDevice,
	BOOLEAN success,
	void* readbuf,
	int readlen,
	PVOID userArg
) {
	UNREFERENCED_PARAMETER(userArg);

	if (!success || readlen != 1) {
		return;
	}

	uint8_t power_mode = CMD_POWER_MODE_FULL;

	//DbgPrint("Read Power Mode: 0x%x\n", power);

	uint8_t power = ((uint8_t *)readbuf)[0];
	power = (power & ~0xFC);
	power |= power_mode & 0xFc;

	cyapa_write_byte(pDevice, SMBUS_ENCODE_RW(CYAPA_SMBUS_POWER_MODE, SMBUS_WRITE), power, NULL, NULL);
}

VOID CyapaReadCapabilities(
	IN PCYAPA_CONTEXT pDevice,
	BOOLEAN success,
	void* readbuf,
	int readlen,
	PVOID userArg
) {
	UNREFERENCED_PARAMETER(userArg);

	if (!success || readlen < sizeof(struct cyapa_cap)) {
		return;
	}

	struct csgesture_softc* sc = &pDevice->sc;

	if (!sc->infoSetup) {
		struct cyapa_cap* cap = (struct cyapa_cap*)readbuf;
		if (strncmp((const char*)cap->prod_ida, "CYTRA", 5) != 0) {
			DbgPrint("[cyapainit] Product ID \"%5.5s\" mismatch\n",
				cap->prod_ida);
		}

		sc->resx = ((cap->max_abs_xy_high << 4) & 0x0F00) |
			cap->max_abs_x_low;
		sc->resy = ((cap->max_abs_xy_high << 8) & 0x0F00) |
			cap->max_abs_y_low;
		sc->phyx = ((cap->phy_siz_xy_high << 4) & 0x0F00) |
			cap->phy_siz_x_low;
		sc->phyy = ((cap->phy_siz_xy_high << 8) & 0x0F00) |
			cap->phy_siz_y_low;

		DbgPrint("[cyapainit] %5.5s-%6.6s-%2.2s buttons=%c%c%c res=%dx%d\n",
			cap->prod_ida, cap->prod_idb, cap->prod_idc,
			((cap->buttons & CYAPA_FNGR_LEFT) ? 'L' : '-'),
			((cap->buttons & CYAPA_FNGR_MIDDLE) ? 'M' : '-'),
			((cap->buttons & CYAPA_FNGR_RIGHT) ? 'R' : '-'),
			sc->resx,
			sc->resy);

		for (int i = 0; i < 5; i++) {
			sc->product_id[i] = cap->prod_ida[i];
		}
		sc->product_id[5] = '-';
		for (int i = 0; i < 6; i++) {
			sc->product_id[i + 6] = cap->prod_idb[i];
		}
		sc->product_id[12] = '-';
		for (int i = 0; i < 2; i++) {
			sc->product_id[i + 13] = cap->prod_idc[i];
		}
		sc->product_id[15] = '\0';

		RtlStringCbPrintfA(sc->firmware_version, sizeof(sc->firmware_version), "%d.%d", cap->fw_maj_ver, cap->fw_min_ver);
		sc->infoSetup = true;
	}

	cyapa_read_byte(pDevice, SMBUS_ENCODE_RW(CYAPA_SMBUS_POWER_MODE, SMBUS_READ), cyapa_set_full_power, NULL); //read power mode

	pDevice->TrackpadIsBooted = TRUE;
}

VOID
CyapaBootWorkItem(
	IN WDFWORKITEM  WorkItem
)
{
	WDFDEVICE Device = (WDFDEVICE)WdfWorkItemGetParentObject(WorkItem);
	PCYAPA_CONTEXT pDevice = GetDeviceContext(Device);

	WdfObjectDelete(WorkItem);

	cyapa_read_block(pDevice, SMBUS_ENCODE_RW(CYAPA_SMBUS_GROUP_QUERY, SMBUS_READ), CyapaReadCapabilities, NULL);
}

void CyapaBootTimer(_In_ WDFTIMER hTimer) {
	WDFDEVICE Device = (WDFDEVICE)WdfTimerGetParentObject(hTimer);

	WDF_OBJECT_ATTRIBUTES attributes;
	WDF_WORKITEM_CONFIG workitemConfig;
	WDFWORKITEM hWorkItem;

	WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
	WDF_OBJECT_ATTRIBUTES_SET_CONTEXT_TYPE(&attributes, CYAPA_CONTEXT);
	attributes.ParentObject = Device;
	WDF_WORKITEM_CONFIG_INIT(&workitemConfig, CyapaBootWorkItem);

	WdfWorkItemCreate(&workitemConfig,
		&attributes,
		&hWorkItem);

	WdfWorkItemEnqueue(hWorkItem);

	WdfTimerStop(hTimer, FALSE);
}

static char bl_exit[] = {
		0x00, 0xff, 0xa5, 0x00, 0x01,
		0x02, 0x03, 0x04, 0x05, 0x06, 0x07 };

#define TPAD_BOOT_CHECK 0x10
#define TPAD_BOOT_ATTEMPTS_MASK 0xF

VOID BOOTTRACKPAD2(
	_In_ PCYAPA_CONTEXT pDevice,
	BOOLEAN success,
	void* readbuf,
	int readlen,
	int bootdata
) {
	bool bootchecked = bootdata & TPAD_BOOT_CHECK;
	bool booted = false;
	if (bootchecked) {
		DbgPrint("Checked boot... %d\n", success);
		if (success) {
			if (readlen >= sizeof(struct cyapa_boot_regs)) {
				struct cyapa_boot_regs* boot = readbuf;
				if (boot->stat & CYAPA_STAT_RUNNING) {
					booted = true;
					DbgPrint("Device is already running! No need to boot!\n");
				}
				else {
					DbgPrint("Device needs to boot!\n");
					if (boot->error & CYAPA_ERROR_BOOTLOADER) {
						DbgPrint("Device is errored in bootloader!\n");
					}
				}
			}
		}
	}
	else {
		booted = success;
	}

	int attempts = bootdata & TPAD_BOOT_ATTEMPTS_MASK;
	if (booted || attempts == 2) {
		if (!success) {
			DbgPrint("Warning: Device won't function if not already booted!\n");
		}

		WDF_TIMER_CONFIG              timerConfig;
		WDFTIMER                      hTimer;
		WDF_OBJECT_ATTRIBUTES         attributes;

		WDF_TIMER_CONFIG_INIT(&timerConfig, CyapaBootTimer);

		WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
		attributes.ParentObject = pDevice->FxDevice;
		WdfTimerCreate(&timerConfig, &attributes, &hTimer);

		WdfTimerStart(hTimer, WDF_REL_TIMEOUT_IN_MS(75));
	}
	else if (attempts < 2) {
		attempts += 1;
		cyapa_write_block(pDevice, SMBUS_ENCODE_RW(0, SMBUS_WRITE), (uint8_t*)bl_exit, sizeof(bl_exit), (SMBUS_USER_CALLBACK)BOOTTRACKPAD2, (PVOID)attempts);
	}
}

NTSTATUS BOOTTRACKPAD(
	_In_  PCYAPA_CONTEXT  pDevice
	)
{
	NTSTATUS status = 0;

	pDevice->TrackpadIsBooted = false;

	cyapa_read_block(pDevice, SMBUS_ENCODE_RW(CYAPA_SMBUS_BL_STATUS, SMBUS_READ), (SMBUS_USER_CALLBACK)BOOTTRACKPAD2, (PVOID)TPAD_BOOT_CHECK);

	return status;
}

VOID CyapaReadWriteCallback(
	IN PCYAPA_CONTEXT pDevice,
	BOOLEAN success,
	void* readbuf,
	int readlen,
	PVOID userArg
) {
	UNREFERENCED_PARAMETER(userArg);
	if (!success || readlen < sizeof(struct cyapa_regs)) {
		return;
	}
	//DbgPrint("Reading Multitouch Report!\n");

	struct cyapa_regs* regs = (struct cyapa_regs*)readbuf;

	RtlCopyMemory(&pDevice->lastregs, regs, sizeof(pDevice->lastregs));
	pDevice->RegsSet = true;
}

int CyapaArg2 = 1;

BOOLEAN
CyapaInterrupt(
	IN PCYAPA_CONTEXT pDevice
) {
	if (!pDevice) {
		return false;
	}

	if (!pDevice->ConnectInterrupt) {
		return false;
	}

	if (!pDevice->TrackpadIsBooted) {
		return false;
	}

	cyapa_read_block(pDevice, SMBUS_ENCODE_RW(CYAPA_SMBUS_GROUP_DATA, SMBUS_READ), CyapaReadWriteCallback, NULL);
	return true;
}

VOID
CyapaIntCallback(
	IN PCYAPA_CONTEXT pDevice,
	PCROSTPCALLBACK_PKT tpContext,
	PVOID Argument2
) {
	if (Argument2 == &CyapaArg2) {
		return;
	}

	if (!tpContext) {
		return;
	}

	if (tpContext->signature != CrosTpSig) {
		return;
	}

	if (tpContext->actionSource != CrosTpCallbackSourceInterrupt) {
		return;
	}

	switch (tpContext->action) {
	case CrosTPCallbackActionRegister:
	{
		tpContext->actionStatus = STATUS_SUCCESS;

		RtlZeroMemory(&pDevice->CrosTpCallbackTempContext, sizeof(CROSTPCALLBACK_PKT));
		pDevice->CrosTpCallbackTempContext.signature = CrosTpSig;
		pDevice->CrosTpCallbackTempContext.action = CrosTPCallbackActionRegister;
		pDevice->CrosTpCallbackTempContext.actionSource = CrosTpCallbackSourceTouchpad;
		pDevice->CrosTpCallbackTempContext.actionParameters.registrationParameters.devContext = pDevice;
		pDevice->CrosTpCallbackTempContext.actionParameters.registrationParameters.callbackFunction = CyapaInterrupt;
		pDevice->CrosTpCallbackTempContext.actionStatus = STATUS_DEVICE_NOT_READY;
		ExNotifyCallback(pDevice->CrosTpIntCallback, &pDevice->CrosTpCallbackTempContext, &CyapaArg2);
	}
	case CrosTPCallbackActionUnregister:
	{
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
	PCYAPA_CONTEXT pDevice = GetDeviceContext(FxDevice);
	NTSTATUS status = STATUS_INSUFFICIENT_RESOURCES;

	UNREFERENCED_PARAMETER(FxResourcesRaw);

	//
	// Parse the peripheral's resources.
	//

	ULONG resourceCount = WdfCmResourceListGetCount(FxResourcesTranslated);

	for (ULONG i = 0; i < resourceCount; i++)
	{
		PCM_PARTIAL_RESOURCE_DESCRIPTOR pDescriptor;

		pDescriptor = WdfCmResourceListGetDescriptor(
			FxResourcesTranslated, i);

		switch (pDescriptor->Type)
		{
		case CmResourceTypePort:
			pDevice->SMBusBase = (unsigned short)pDescriptor->u.Port.Start.LowPart;
			pDevice->SMBusLen = pDescriptor->u.Port.Length;

			CyapaPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL, "crostrackpad-smbus: Got IO Port 0x%x (len 0x%x)\n", pDevice->SMBusBase, pDevice->SMBusLen);

			status = STATUS_SUCCESS;
			break;
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
		CyapaIntCallback,
		pDevice
	);
	if (!pDevice->CrosTpCallbackObj) {

		return STATUS_NO_CALLBACK_ACTIVE;
	}

	RtlZeroMemory(&pDevice->CrosTpCallbackTempContext, sizeof(CROSTPCALLBACK_PKT));
	pDevice->CrosTpCallbackTempContext.signature = CrosTpSig;
	pDevice->CrosTpCallbackTempContext.action = CrosTPCallbackActionRegister;
	pDevice->CrosTpCallbackTempContext.actionSource = CrosTpCallbackSourceTouchpad;
	pDevice->CrosTpCallbackTempContext.actionParameters.registrationParameters.devContext = pDevice;
	pDevice->CrosTpCallbackTempContext.actionParameters.registrationParameters.callbackFunction = CyapaInterrupt;
	pDevice->CrosTpCallbackTempContext.actionStatus = STATUS_DEVICE_NOT_READY;
	ExNotifyCallback(pDevice->CrosTpIntCallback, &pDevice->CrosTpCallbackTempContext, &CyapaArg2);

	pDevice->SMBusLocked = false;
	pDevice->SMBusUserCallback = NULL;
	pDevice->SMBusInternalCallback = NULL;
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
	PCYAPA_CONTEXT pDevice = GetDeviceContext(FxDevice);
	NTSTATUS status = STATUS_SUCCESS;

	UNREFERENCED_PARAMETER(FxResourcesTranslated);

	if (pDevice->CrosTpCallbackObj) {
		RtlZeroMemory(&pDevice->CrosTpCallbackTempContext, sizeof(CROSTPCALLBACK_PKT));
		pDevice->CrosTpCallbackTempContext.signature = CrosTpSig;
		pDevice->CrosTpCallbackTempContext.action = CrosTPCallbackActionUnregister;
		pDevice->CrosTpCallbackTempContext.actionSource = CrosTpCallbackSourceTouchpad;
		pDevice->CrosTpCallbackTempContext.actionStatus = STATUS_DEVICE_NOT_READY;
		ExNotifyCallback(pDevice->CrosTpIntCallback, &pDevice->CrosTpCallbackTempContext, &CyapaArg2); //Notify one last time before unregistration

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

	PCYAPA_CONTEXT pDevice = GetDeviceContext(FxDevice);
	NTSTATUS status = STATUS_SUCCESS;

	pDevice->TrackpadIsBooted = false;

	WdfTimerStart(pDevice->Timer, WDF_REL_TIMEOUT_IN_MS(10));

	BOOTTRACKPAD(pDevice);

	pDevice->RegsSet = false;
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

	PCYAPA_CONTEXT pDevice = GetDeviceContext(FxDevice);

	WdfTimerStop(pDevice->Timer, TRUE);

	pDevice->ConnectInterrupt = false;

	/* Clear special mode bits */
	outb_p(inb_p(SMBAUXCTL(pDevice)) &
		~(SMBAUXCTL_CRC | SMBAUXCTL_E32B), SMBAUXCTL(pDevice));

	return STATUS_SUCCESS;
}

BOOLEAN OnInterruptIsr(
	WDFINTERRUPT Interrupt,
	ULONG MessageID) {
	UNREFERENCED_PARAMETER(MessageID);

	WDFDEVICE Device = WdfInterruptGetDevice(Interrupt);
	PCYAPA_CONTEXT pDevice = GetDeviceContext(Device);

	if (!pDevice->ConnectInterrupt)
		return false;

	if (!pDevice->SMBusLocked)
		return false;

	if (pDevice->SMBusInternalCallback == NULL)
		return false;

	//DbgPrint("SMBus Interrupt Raised!\n");
	pDevice->InterruptRaised = true;

	uint8_t status = inb_p(SMBHSTSTS(pDevice));

	if (pDevice->SMBusInternalCallback) {
		SMBUS_INTERNAL_CALLBACK callback = pDevice->SMBusInternalCallback;
		pDevice->SMBusInternalCallback = NULL;

		BOOLEAN ret = callback(pDevice, status);
		if (ret) {
			return ret;
		}
		else {
			status &= SMBHSTSTS_INTR | STATUS_ERROR_FLAGS;
			if (status) {
				outb_p(status, SMBHSTSTS(pDevice));
			}
			return true;
		}
	}
	else {
		DbgPrint("Warning: No Internal Callback... Spurious interrupt?\n");
	}

	status &= SMBHSTSTS_INTR | STATUS_ERROR_FLAGS;
	if (status) {
		outb_p(status, SMBHSTSTS(pDevice));
	}

	return true;
}

NTSTATUS
CyapaEvtDeviceAdd(
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
	PCYAPA_CONTEXT               devContext;

	UNREFERENCED_PARAMETER(Driver);

	PAGED_CODE();

	CyapaPrint(DEBUG_LEVEL_INFO, DBG_PNP,
		"CyapaEvtDeviceAdd called\n");

	//
	// Tell framework this is a filter driver. Filter drivers by default are  
	// not power policy owners. This works well for this driver because
	// HIDclass driver is the power policy owner for HID minidrivers.
	//

	WdfFdoInitSetFilter(DeviceInit);

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

	WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, CYAPA_CONTEXT);

	//
	// Create a framework device object.This call will in turn create
	// a WDM device object, attach to the lower stack, and set the
	// appropriate flags and attributes.
	//

	status = WdfDeviceCreate(&DeviceInit, &attributes, &device);

	if (!NT_SUCCESS(status))
	{
		CyapaPrint(DEBUG_LEVEL_ERROR, DBG_PNP,
			"WdfDeviceCreate failed with status code 0x%x\n", status);

		return status;
	}

	WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueConfig, WdfIoQueueDispatchParallel);

	queueConfig.EvtIoInternalDeviceControl = CyapaEvtInternalDeviceControl;

	status = WdfIoQueueCreate(device,
		&queueConfig,
		WDF_NO_OBJECT_ATTRIBUTES,
		&queue
		);

	if (!NT_SUCCESS(status))
	{
		CyapaPrint(DEBUG_LEVEL_ERROR, DBG_PNP,
			"WdfIoQueueCreate failed 0x%x\n", status);

		return status;
	}

	//
	// Create manual I/O queue to take care of hid report read requests
	//

	devContext = GetDeviceContext(device);

	WDF_IO_QUEUE_CONFIG_INIT(&queueConfig, WdfIoQueueDispatchManual);

	queueConfig.PowerManaged = WdfFalse;

	status = WdfIoQueueCreate(device,
		&queueConfig,
		WDF_NO_OBJECT_ATTRIBUTES,
		&devContext->ReportQueue
		);

	if (!NT_SUCCESS(status))
	{
		CyapaPrint(DEBUG_LEVEL_ERROR, DBG_PNP,
			"WdfIoQueueCreate failed 0x%x\n", status);

		return status;
	}

	//
	// Create an interrupt object for hardware notifications
	//
	WDF_INTERRUPT_CONFIG_INIT(
		&interruptConfig,
		OnInterruptIsr,
		NULL);

	status = WdfInterruptCreate(
		device,
		&interruptConfig,
		WDF_NO_OBJECT_ATTRIBUTES,
		&devContext->Interrupt);

	if (!NT_SUCCESS(status))
	{
		CyapaPrint(DEBUG_LEVEL_ERROR, DBG_PNP,
			"Error creating WDF interrupt object - %!STATUS!",
			status);

		return status;
	}

	WDF_TIMER_CONFIG              timerConfig;
	WDFTIMER                      hTimer;

	WDF_TIMER_CONFIG_INIT_PERIODIC(&timerConfig, CyapaTimerFunc, 10);

	WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
	attributes.ParentObject = device;
	status = WdfTimerCreate(&timerConfig, &attributes, &hTimer);
	devContext->Timer = hTimer;
	if (!NT_SUCCESS(status))
	{
		CyapaPrint(DEBUG_LEVEL_ERROR, DBG_PNP, "(%!FUNC!) WdfTimerCreate failed status:%!STATUS!\n", status);
		return status;
	}

	CyapaPrint(DEBUG_LEVEL_ERROR, DBG_PNP,
		"Success! 0x%x\n", status);

	//
	// Initialize DeviceMode
	//

	SetDefaultSettings(&devContext->sc);

	devContext->DeviceMode = DEVICE_MODE_MOUSE;
	devContext->FxDevice = device;

	return status;
}

VOID
CyapaEvtInternalDeviceControl(
IN WDFQUEUE     Queue,
IN WDFREQUEST   Request,
IN size_t       OutputBufferLength,
IN size_t       InputBufferLength,
IN ULONG        IoControlCode
)
{
	NTSTATUS            status = STATUS_SUCCESS;
	WDFDEVICE           device;
	PCYAPA_CONTEXT     devContext;
	BOOLEAN             completeRequest = TRUE;

	UNREFERENCED_PARAMETER(OutputBufferLength);
	UNREFERENCED_PARAMETER(InputBufferLength);

	device = WdfIoQueueGetDevice(Queue);
	devContext = GetDeviceContext(device);

	CyapaPrint(DEBUG_LEVEL_INFO, DBG_IOCTL,
		"%s, Queue:0x%p, Request:0x%p\n",
		DbgHidInternalIoctlString(IoControlCode),
		Queue,
		Request
		);

	//
	// Please note that HIDCLASS provides the buffer in the Irp->UserBuffer
	// field irrespective of the ioctl buffer type. However, framework is very
	// strict about type checking. You cannot get Irp->UserBuffer by using
	// WdfRequestRetrieveOutputMemory if the ioctl is not a METHOD_NEITHER
	// internal ioctl. So depending on the ioctl code, we will either
	// use retreive function or escape to WDM to get the UserBuffer.
	//

	switch (IoControlCode)
	{

	case IOCTL_HID_GET_DEVICE_DESCRIPTOR:
		//
		// Retrieves the device's HID descriptor.
		//
		status = CyapaGetHidDescriptor(device, Request);
		break;

	case IOCTL_HID_GET_DEVICE_ATTRIBUTES:
		//
		//Retrieves a device's attributes in a HID_DEVICE_ATTRIBUTES structure.
		//
		status = CyapaGetDeviceAttributes(Request);
		break;

	case IOCTL_HID_GET_REPORT_DESCRIPTOR:
		//
		//Obtains the report descriptor for the HID device.
		//
		status = CyapaGetReportDescriptor(device, Request, FALSE, &completeRequest);
		break;

	case IOCTL_HID_GET_STRING:
		//
		// Requests that the HID minidriver retrieve a human-readable string
		// for either the manufacturer ID, the product ID, or the serial number
		// from the string descriptor of the device. The minidriver must send
		// a Get String Descriptor request to the device, in order to retrieve
		// the string descriptor, then it must extract the string at the
		// appropriate index from the string descriptor and return it in the
		// output buffer indicated by the IRP. Before sending the Get String
		// Descriptor request, the minidriver must retrieve the appropriate
		// index for the manufacturer ID, the product ID or the serial number
		// from the device extension of a top level collection associated with
		// the device.
		//
		status = CyapaGetString(Request);
		break;

	case IOCTL_HID_WRITE_REPORT:
	case IOCTL_HID_SET_OUTPUT_REPORT:
		//
		//Transmits a class driver-supplied report to the device.
		//
		status = CyapaWriteReport(devContext, Request);
		break;

	case IOCTL_HID_READ_REPORT:
	case IOCTL_HID_GET_INPUT_REPORT:
		//
		// Returns a report from the device into a class driver-supplied buffer.
		// 
		status = CyapaReadReport(devContext, Request, &completeRequest);
		break;

	case IOCTL_HID_SET_FEATURE:
		//
		// This sends a HID class feature report to a top-level collection of
		// a HID class device.
		//
		status = CyapaSetFeature(devContext, Request, &completeRequest);
		break;

	case IOCTL_HID_GET_FEATURE:
		//
		// returns a feature report associated with a top-level collection
		//
		status = CyapaGetFeature(devContext, Request, &completeRequest);
		break;

	case IOCTL_HID_ACTIVATE_DEVICE:
		//
		// Makes the device ready for I/O operations.
		//
	case IOCTL_HID_DEACTIVATE_DEVICE:
		//
		// Causes the device to cease operations and terminate all outstanding
		// I/O requests.
		//
	default:
		status = STATUS_NOT_SUPPORTED;
		break;
	}

	if (completeRequest)
	{
		WdfRequestComplete(Request, status);

		CyapaPrint(DEBUG_LEVEL_INFO, DBG_IOCTL,
			"%s completed, Queue:0x%p, Request:0x%p\n",
			DbgHidInternalIoctlString(IoControlCode),
			Queue,
			Request
			);
	}
	else
	{
		CyapaPrint(DEBUG_LEVEL_INFO, DBG_IOCTL,
			"%s deferred, Queue:0x%p, Request:0x%p\n",
			DbgHidInternalIoctlString(IoControlCode),
			Queue,
			Request
			);
	}

	return;
}

NTSTATUS
CyapaGetHidDescriptor(
IN WDFDEVICE Device,
IN WDFREQUEST Request
)
{
	NTSTATUS            status = STATUS_SUCCESS;
	size_t              bytesToCopy = 0;
	WDFMEMORY           memory;

	UNREFERENCED_PARAMETER(Device);

	CyapaPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"CyapaGetHidDescriptor Entry\n");

	//
	// This IOCTL is METHOD_NEITHER so WdfRequestRetrieveOutputMemory
	// will correctly retrieve buffer from Irp->UserBuffer. 
	// Remember that HIDCLASS provides the buffer in the Irp->UserBuffer
	// field irrespective of the ioctl buffer type. However, framework is very
	// strict about type checking. You cannot get Irp->UserBuffer by using
	// WdfRequestRetrieveOutputMemory if the ioctl is not a METHOD_NEITHER
	// internal ioctl.
	//
	status = WdfRequestRetrieveOutputMemory(Request, &memory);

	if (!NT_SUCCESS(status))
	{
		CyapaPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"WdfRequestRetrieveOutputMemory failed 0x%x\n", status);

		return status;
	}

	//
	// Use hardcoded "HID Descriptor" 
	//
	bytesToCopy = DefaultHidDescriptor.bLength;

	if (bytesToCopy == 0)
	{
		status = STATUS_INVALID_DEVICE_STATE;

		CyapaPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"DefaultHidDescriptor is zero, 0x%x\n", status);

		return status;
	}

	status = WdfMemoryCopyFromBuffer(memory,
		0, // Offset
		(PVOID)&DefaultHidDescriptor,
		bytesToCopy);

	if (!NT_SUCCESS(status))
	{
		CyapaPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"WdfMemoryCopyFromBuffer failed 0x%x\n", status);

		return status;
	}

	//
	// Report how many bytes were copied
	//
	WdfRequestSetInformation(Request, bytesToCopy);

	CyapaPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"CyapaGetHidDescriptor Exit = 0x%x\n", status);

	return status;
}

void CyapaDescriptorTimer(_In_ WDFTIMER hTimer) {
	WDFDEVICE Device = (WDFDEVICE)WdfTimerGetParentObject(hTimer);
	PCYAPA_CONTEXT pDevice = GetDeviceContext(Device);

	WdfRequestForwardToIoQueue(pDevice->lastRequest, pDevice->ReportQueue);

	pDevice->lastRequest = NULL;

	WdfTimerStop(hTimer, FALSE);
}

NTSTATUS
CyapaGetReportDescriptor(
	IN WDFDEVICE Device,
	IN WDFREQUEST Request,
	IN BOOLEAN Retried,
	OUT BOOLEAN* CompleteRequest
)
{
	NTSTATUS            status = STATUS_SUCCESS;
	ULONG_PTR           bytesToCopy;
	WDFMEMORY           memory;

	UNREFERENCED_PARAMETER(Device);

	CyapaPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"CyapaGetReportDescriptor Entry\n");

	//
	// This IOCTL is METHOD_NEITHER so WdfRequestRetrieveOutputMemory
	// will correctly retrieve buffer from Irp->UserBuffer. 
	// Remember that HIDCLASS provides the buffer in the Irp->UserBuffer
	// field irrespective of the ioctl buffer type. However, framework is very
	// strict about type checking. You cannot get Irp->UserBuffer by using
	// WdfRequestRetrieveOutputMemory if the ioctl is not a METHOD_NEITHER
	// internal ioctl.
	//
	status = WdfRequestRetrieveOutputMemory(Request, &memory);
	if (!NT_SUCCESS(status))
	{
		CyapaPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"WdfRequestRetrieveOutputMemory failed 0x%x\n", status);

		return status;
	}

	//
	// Use hardcoded Report descriptor
	//
	bytesToCopy = DefaultHidDescriptor.DescriptorList[0].wReportLength;

	if (bytesToCopy == 0)
	{
		status = STATUS_INVALID_DEVICE_STATE;

		CyapaPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"DefaultHidDescriptor's reportLength is zero, 0x%x\n", status);

		return status;
	}

	status = WdfMemoryCopyFromBuffer(memory,
		0,
		(PVOID)DefaultReportDescriptor,
		bytesToCopy);
	if (!NT_SUCCESS(status))
	{
		CyapaPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"WdfMemoryCopyFromBuffer failed 0x%x\n", status);

		return status;
	}

	//
	// Report how many bytes were copied
	//
	WdfRequestSetInformation(Request, bytesToCopy);

	CyapaPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"CyapaGetReportDescriptor Exit = 0x%x\n", status);

	if (Retried) {
		WdfRequestComplete(Request, status);
	}

	return status;
}


NTSTATUS
CyapaGetDeviceAttributes(
IN WDFREQUEST Request
)
{
	NTSTATUS                 status = STATUS_SUCCESS;
	PHID_DEVICE_ATTRIBUTES   deviceAttributes = NULL;

	CyapaPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"CyapaGetDeviceAttributes Entry\n");

	//
	// This IOCTL is METHOD_NEITHER so WdfRequestRetrieveOutputMemory
	// will correctly retrieve buffer from Irp->UserBuffer. 
	// Remember that HIDCLASS provides the buffer in the Irp->UserBuffer
	// field irrespective of the ioctl buffer type. However, framework is very
	// strict about type checking. You cannot get Irp->UserBuffer by using
	// WdfRequestRetrieveOutputMemory if the ioctl is not a METHOD_NEITHER
	// internal ioctl.
	//
	status = WdfRequestRetrieveOutputBuffer(Request,
		sizeof(HID_DEVICE_ATTRIBUTES),
		&deviceAttributes,
		NULL);
	if (!NT_SUCCESS(status))
	{
		CyapaPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"WdfRequestRetrieveOutputBuffer failed 0x%x\n", status);

		return status;
	}

	//
	// Set USB device descriptor
	//

	deviceAttributes->Size = sizeof(HID_DEVICE_ATTRIBUTES);
	deviceAttributes->VendorID = CYAPA_VID;
	deviceAttributes->ProductID = CYAPA_PID;
	deviceAttributes->VersionNumber = CYAPA_VERSION;

	//
	// Report how many bytes were copied
	//
	WdfRequestSetInformation(Request, sizeof(HID_DEVICE_ATTRIBUTES));

	CyapaPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"CyapaGetDeviceAttributes Exit = 0x%x\n", status);

	return status;
}

NTSTATUS
CyapaGetString(
IN WDFREQUEST Request
)
{

	NTSTATUS status = STATUS_SUCCESS;
	PWSTR pwstrID;
	size_t lenID;
	WDF_REQUEST_PARAMETERS params;
	void *pStringBuffer = NULL;

	CyapaPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"CyapaGetString Entry\n");

	WDF_REQUEST_PARAMETERS_INIT(&params);
	WdfRequestGetParameters(Request, &params);

	switch ((ULONG_PTR)params.Parameters.DeviceIoControl.Type3InputBuffer & 0xFFFF)
	{
	case HID_STRING_ID_IMANUFACTURER:
		pwstrID = L"Cyapa.\0";
		break;

	case HID_STRING_ID_IPRODUCT:
		pwstrID = L"MaxTouch Touch Screen\0";
		break;

	case HID_STRING_ID_ISERIALNUMBER:
		pwstrID = L"123123123\0";
		break;

	default:
		pwstrID = NULL;
		break;
	}

	lenID = pwstrID ? wcslen(pwstrID)*sizeof(WCHAR) + sizeof(UNICODE_NULL) : 0;

	if (pwstrID == NULL)
	{

		CyapaPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"CyapaGetString Invalid request type\n");

		status = STATUS_INVALID_PARAMETER;

		return status;
	}

	status = WdfRequestRetrieveOutputBuffer(Request,
		lenID,
		&pStringBuffer,
		&lenID);

	if (!NT_SUCCESS(status))
	{

		CyapaPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"CyapaGetString WdfRequestRetrieveOutputBuffer failed Status 0x%x\n", status);

		return status;
	}

	RtlCopyMemory(pStringBuffer, pwstrID, lenID);

	WdfRequestSetInformation(Request, lenID);

	CyapaPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"CyapaGetString Exit = 0x%x\n", status);

	return status;
}

NTSTATUS
CyapaWriteReport(
IN PCYAPA_CONTEXT DevContext,
IN WDFREQUEST Request
)
{
	UNREFERENCED_PARAMETER(DevContext);

	NTSTATUS status = STATUS_SUCCESS;
	WDF_REQUEST_PARAMETERS params;
	PHID_XFER_PACKET transferPacket = NULL;
	CyapaScrollControlReport* pScrollCtrlReport = NULL;
	CyapaSettingsReport* pSettingsReport = NULL;

	CyapaPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"CyapaWriteReport Entry\n");

	WDF_REQUEST_PARAMETERS_INIT(&params);
	WdfRequestGetParameters(Request, &params);

	if (params.Parameters.DeviceIoControl.InputBufferLength < sizeof(HID_XFER_PACKET))
	{
		CyapaPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"CyapaWriteReport Xfer packet too small\n");

		status = STATUS_BUFFER_TOO_SMALL;
	}
	else
	{

		transferPacket = (PHID_XFER_PACKET)WdfRequestWdmGetIrp(Request)->UserBuffer;

		if (transferPacket == NULL)
		{
			CyapaPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
				"CyapaWriteReport No xfer packet\n");

			status = STATUS_INVALID_DEVICE_REQUEST;
		}
		else
		{
			//
			// switch on the report id
			//

			switch (transferPacket->reportId)
			{
			case REPORTID_SCROLLCTRL:

				pScrollCtrlReport = (CyapaScrollControlReport*)transferPacket->reportBuffer;

				if (pScrollCtrlReport->Flag == 1) {
					DevContext->sc.scrollInertiaActive = 1;
				}
				else {
					DevContext->sc.scrollInertiaActive = 0;
				}

				break;

			case REPORTID_SETTINGS:
				pSettingsReport = (CyapaSettingsReport*)transferPacket->reportBuffer;
				ProcessSetting(DevContext, &DevContext->sc, pSettingsReport->SettingsRegister, pSettingsReport->SettingsValue);
				break;
			default:

				CyapaPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
					"CyapaWriteReport Unhandled report type %d\n", transferPacket->reportId);

				status = STATUS_INVALID_PARAMETER;

				break;
			}
		}
	}

	CyapaPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"CyapaWriteReport Exit = 0x%x\n", status);

	return status;

}

NTSTATUS
CyapaProcessVendorReport(
IN PCYAPA_CONTEXT DevContext,
IN PVOID ReportBuffer,
IN ULONG ReportBufferLen,
OUT size_t* BytesWritten
)
{
	NTSTATUS status = STATUS_SUCCESS;
	WDFREQUEST reqRead;
	PVOID pReadReport = NULL;
	size_t bytesReturned = 0;

	CyapaPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"CyapaProcessVendorReport Entry\n");

	status = WdfIoQueueRetrieveNextRequest(DevContext->ReportQueue,
		&reqRead);

	if (NT_SUCCESS(status))
	{
		status = WdfRequestRetrieveOutputBuffer(reqRead,
			ReportBufferLen,
			&pReadReport,
			&bytesReturned);

		if (NT_SUCCESS(status))
		{
			//
			// Copy ReportBuffer into read request
			//

			if (bytesReturned > ReportBufferLen)
			{
				bytesReturned = ReportBufferLen;
			}

			RtlCopyMemory(pReadReport,
				ReportBuffer,
				bytesReturned);

			//
			// Complete read with the number of bytes returned as info
			//

			WdfRequestCompleteWithInformation(reqRead,
				status,
				bytesReturned);

			CyapaPrint(DEBUG_LEVEL_INFO, DBG_IOCTL,
				"CyapaProcessVendorReport %d bytes returned\n", bytesReturned);

			//
			// Return the number of bytes written for the write request completion
			//

			*BytesWritten = bytesReturned;

			CyapaPrint(DEBUG_LEVEL_INFO, DBG_IOCTL,
				"%s completed, Queue:0x%p, Request:0x%p\n",
				DbgHidInternalIoctlString(IOCTL_HID_READ_REPORT),
				DevContext->ReportQueue,
				reqRead);
		}
		else
		{
			CyapaPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
				"WdfRequestRetrieveOutputBuffer failed Status 0x%x\n", status);
		}
	}
	else
	{
		CyapaPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"WdfIoQueueRetrieveNextRequest failed Status 0x%x\n", status);
	}

	CyapaPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"CyapaProcessVendorReport Exit = 0x%x\n", status);

	return status;
}

NTSTATUS
CyapaReadReport(
IN PCYAPA_CONTEXT DevContext,
IN WDFREQUEST Request,
OUT BOOLEAN* CompleteRequest
)
{
	NTSTATUS status = STATUS_SUCCESS;

	CyapaPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"CyapaReadReport Entry\n");

	//
	// Forward this read request to our manual queue
	// (in other words, we are going to defer this request
	// until we have a corresponding write request to
	// match it with)
	//

	status = WdfRequestForwardToIoQueue(Request, DevContext->ReportQueue);

	if (!NT_SUCCESS(status))
	{
		CyapaPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"WdfRequestForwardToIoQueue failed Status 0x%x\n", status);
	}
	else
	{
		*CompleteRequest = FALSE;
	}

	CyapaPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"CyapaReadReport Exit = 0x%x\n", status);

	return status;
}

NTSTATUS
CyapaSetFeature(
IN PCYAPA_CONTEXT DevContext,
IN WDFREQUEST Request,
OUT BOOLEAN* CompleteRequest
)
{
	UNREFERENCED_PARAMETER(CompleteRequest);

	NTSTATUS status = STATUS_SUCCESS;
	WDF_REQUEST_PARAMETERS params;
	PHID_XFER_PACKET transferPacket = NULL;
	CyapaFeatureReport* pReport = NULL;

	CyapaPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"CyapaSetFeature Entry\n");

	WDF_REQUEST_PARAMETERS_INIT(&params);
	WdfRequestGetParameters(Request, &params);

	if (params.Parameters.DeviceIoControl.InputBufferLength < sizeof(HID_XFER_PACKET))
	{
		CyapaPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"CyapaSetFeature Xfer packet too small\n");

		status = STATUS_BUFFER_TOO_SMALL;
	}
	else
	{

		transferPacket = (PHID_XFER_PACKET)WdfRequestWdmGetIrp(Request)->UserBuffer;

		if (transferPacket == NULL)
		{
			CyapaPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
				"CyapaWriteReport No xfer packet\n");

			status = STATUS_INVALID_DEVICE_REQUEST;
		}
		else
		{
			//
			// switch on the report id
			//

			switch (transferPacket->reportId)
			{
			case REPORTID_FEATURE:

				if (transferPacket->reportBufferLen == sizeof(CyapaFeatureReport))
				{
					pReport = (CyapaFeatureReport*)transferPacket->reportBuffer;

					DevContext->DeviceMode = pReport->DeviceMode;

					CyapaPrint(DEBUG_LEVEL_INFO, DBG_IOCTL,
						"CyapaSetFeature DeviceMode = 0x%x\n", DevContext->DeviceMode);
				}
				else
				{
					status = STATUS_INVALID_PARAMETER;

					CyapaPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
						"CyapaSetFeature Error transferPacket->reportBufferLen (%d) is different from sizeof(CyapaFeatureReport) (%d)\n",
						transferPacket->reportBufferLen,
						sizeof(CyapaFeatureReport));
				}

				break;

			default:

				CyapaPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
					"CyapaSetFeature Unhandled report type %d\n", transferPacket->reportId);

				status = STATUS_INVALID_PARAMETER;

				break;
			}
		}
	}

	CyapaPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"CyapaSetFeature Exit = 0x%x\n", status);

	return status;
}

NTSTATUS
CyapaGetFeature(
IN PCYAPA_CONTEXT DevContext,
IN WDFREQUEST Request,
OUT BOOLEAN* CompleteRequest
)
{
	UNREFERENCED_PARAMETER(CompleteRequest);

	NTSTATUS status = STATUS_SUCCESS;
	WDF_REQUEST_PARAMETERS params;
	PHID_XFER_PACKET transferPacket = NULL;

	CyapaPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"CyapaGetFeature Entry\n");

	WDF_REQUEST_PARAMETERS_INIT(&params);
	WdfRequestGetParameters(Request, &params);

	if (params.Parameters.DeviceIoControl.OutputBufferLength < sizeof(HID_XFER_PACKET))
	{
		CyapaPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"CyapaGetFeature Xfer packet too small\n");

		status = STATUS_BUFFER_TOO_SMALL;
	}
	else
	{

		transferPacket = (PHID_XFER_PACKET)WdfRequestWdmGetIrp(Request)->UserBuffer;

		if (transferPacket == NULL)
		{
			CyapaPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
				"CyapaGetFeature No xfer packet\n");

			status = STATUS_INVALID_DEVICE_REQUEST;
		}
		else
		{
			//
			// switch on the report id
			//

			switch (transferPacket->reportId)
			{
			case REPORTID_FEATURE:
			{

				CyapaFeatureReport* pReport = NULL;

				if (transferPacket->reportBufferLen >= sizeof(CyapaFeatureReport))
				{
					pReport = (CyapaFeatureReport*)transferPacket->reportBuffer;

					pReport->DeviceMode = DevContext->DeviceMode;

					pReport->DeviceIdentifier = 0;

					CyapaPrint(DEBUG_LEVEL_INFO, DBG_IOCTL,
						"CyapaGetFeature DeviceMode = 0x%x\n", DevContext->DeviceMode);
				}
				else
				{
					status = STATUS_INVALID_PARAMETER;

					CyapaPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
						"CyapaGetFeature Error transferPacket->reportBufferLen (%d) is different from sizeof(CyapaFeatureReport) (%d)\n",
						transferPacket->reportBufferLen,
						sizeof(CyapaFeatureReport));
				}

				break;
			}

			default:

				CyapaPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
					"CyapaGetFeature Unhandled report type %d\n", transferPacket->reportId);

				status = STATUS_INVALID_PARAMETER;

				break;
			}
		}
	}

	CyapaPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"CyapaGetFeature Exit = 0x%x\n", status);

	return status;
}

PCHAR
DbgHidInternalIoctlString(
IN ULONG IoControlCode
)
{
	switch (IoControlCode)
	{
	case IOCTL_HID_GET_DEVICE_DESCRIPTOR:
		return "IOCTL_HID_GET_DEVICE_DESCRIPTOR";
	case IOCTL_HID_GET_REPORT_DESCRIPTOR:
		return "IOCTL_HID_GET_REPORT_DESCRIPTOR";
	case IOCTL_HID_READ_REPORT:
		return "IOCTL_HID_READ_REPORT";
	case IOCTL_HID_GET_DEVICE_ATTRIBUTES:
		return "IOCTL_HID_GET_DEVICE_ATTRIBUTES";
	case IOCTL_HID_WRITE_REPORT:
		return "IOCTL_HID_WRITE_REPORT";
	case IOCTL_HID_SET_FEATURE:
		return "IOCTL_HID_SET_FEATURE";
	case IOCTL_HID_GET_FEATURE:
		return "IOCTL_HID_GET_FEATURE";
	case IOCTL_HID_GET_STRING:
		return "IOCTL_HID_GET_STRING";
	case IOCTL_HID_ACTIVATE_DEVICE:
		return "IOCTL_HID_ACTIVATE_DEVICE";
	case IOCTL_HID_DEACTIVATE_DEVICE:
		return "IOCTL_HID_DEACTIVATE_DEVICE";
	case IOCTL_HID_SEND_IDLE_NOTIFICATION_REQUEST:
		return "IOCTL_HID_SEND_IDLE_NOTIFICATION_REQUEST";
	case IOCTL_HID_SET_OUTPUT_REPORT:
		return "IOCTL_HID_SET_OUTPUT_REPORT";
	case IOCTL_HID_GET_INPUT_REPORT:
		return "IOCTL_HID_GET_INPUT_REPORT";
	default:
		return "Unknown IOCTL";
	}
}
