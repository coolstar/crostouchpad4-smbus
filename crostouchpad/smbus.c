#include "driver.h"
#include <initguid.h>
#include <wdmguid.h>

static unsigned char inb_p(unsigned short int port) {
	unsigned char value = __inbyte(port);
	return value;
}

static void outb_p(unsigned char value, unsigned short int port) {
	__outbyte(port, value);
}

static void cyapa_set_slave_addr(PCYAPA_CONTEXT pDevice, uint8_t smbusread) {
	outb_p(0xce + smbusread, SMBHSTADD(pDevice)); // 0x67
}

BOOLEAN cyapa_read_byte_callback(PCYAPA_CONTEXT pDevice, int status);
uint32_t cyapa_read_byte(PCYAPA_CONTEXT pDevice, uint8_t cmd, SMBUS_USER_CALLBACK callback, PVOID arg) {
	if (pDevice->SMBusLocked) {
		return 1;
	}
	pDevice->SMBusLocked = true;
	pDevice->InterruptRaised = false;

	cyapa_set_slave_addr(pDevice, 1);
	outb_p(cmd, SMBHSTCMD(pDevice));

	//Disable Hardware PEC
	outb_p(inb_p(SMBAUXCTL(pDevice)) & (~SMBAUXCTL_CRC),
		SMBAUXCTL(pDevice));

	//check if ready
	int status = inb_p(SMBHSTSTS(pDevice));
	if (status & SMBHSTSTS_HOST_BUSY) {
		DbgPrint("SMBus is Busy! Can't use it :(\n");
		return 1;
	}

	outb_p(0x49, SMBHSTCNT(pDevice));

	pDevice->SMBusInternalCallback = cyapa_read_byte_callback;
	pDevice->SMBusUserCallback = callback;
	pDevice->SMBusUserCallbackArg = arg;

	//wait for interrupt
	return 0;
}

BOOLEAN cyapa_read_byte_callback(PCYAPA_CONTEXT pDevice, int status) {
	status &= SMBHSTSTS_INTR | STATUS_ERROR_FLAGS;
	if (status) {
		outb_p(status, SMBHSTSTS(pDevice));
	}

	uint8_t data = inb_p(SMBHSTDAT0(pDevice));

	pDevice->SMBusLocked = false;
	pDevice->InterruptRaised = false;

	if (pDevice->SMBusUserCallback) {
		SMBUS_USER_CALLBACK callback = pDevice->SMBusUserCallback;
		PVOID arg = pDevice->SMBusUserCallbackArg;
		pDevice->SMBusUserCallback = NULL;
		pDevice->SMBusUserCallbackArg = NULL;

		callback(pDevice, true, &data, 1, arg);
	}
	return true;
}

BOOLEAN cyapa_write_byte_callback(PCYAPA_CONTEXT pDevice, int status);
uint32_t cyapa_write_byte(PCYAPA_CONTEXT pDevice, uint8_t cmd, uint8_t value, SMBUS_USER_CALLBACK callback, PVOID arg) {
	if (pDevice->SMBusLocked) {
		return 1;
	}
	pDevice->SMBusLocked = true;
	pDevice->InterruptRaised = false;

	cyapa_set_slave_addr(pDevice, 0);
	outb_p(cmd, SMBHSTCMD(pDevice));
	outb_p(value, SMBHSTDAT0(pDevice));

	//Disable Hardware PEC
	outb_p(inb_p(SMBAUXCTL(pDevice)) & (~SMBAUXCTL_CRC),
		SMBAUXCTL(pDevice));

	//check if ready
	int status = inb_p(SMBHSTSTS(pDevice));
	if (status & SMBHSTSTS_HOST_BUSY) {
		DbgPrint("SMBus is Busy! Can't use it :(\n");
		return 1;
	}

	outb_p(0x49, SMBHSTCNT(pDevice));

	pDevice->SMBusInternalCallback = cyapa_write_byte_callback;
	pDevice->SMBusUserCallback = callback;
	pDevice->SMBusUserCallbackArg = arg;
	//wait for interrupt
	return 0;
}

BOOLEAN cyapa_write_byte_callback(PCYAPA_CONTEXT pDevice, int status) {
	status &= SMBHSTSTS_INTR | STATUS_ERROR_FLAGS;
	if (status) {
		outb_p(status, SMBHSTSTS(pDevice));
	}

	pDevice->SMBusLocked = false;
	pDevice->InterruptRaised = false;

	if (pDevice->SMBusUserCallback) {
		SMBUS_USER_CALLBACK callback = pDevice->SMBusUserCallback;
		PVOID arg = pDevice->SMBusUserCallbackArg;
		pDevice->SMBusUserCallback = NULL;
		pDevice->SMBusUserCallbackArg = NULL;

		callback(pDevice, true, NULL, 0, arg);
	}
	return true;
}

BOOLEAN cyapa_read_block_callback(PCYAPA_CONTEXT pDevice, int status);
uint32_t cyapa_read_block(PCYAPA_CONTEXT pDevice, uint8_t cmd, SMBUS_USER_CALLBACK callback, PVOID arg) {
	if (pDevice->SMBusLocked) {
		return 1;
	}

	/* Clear special mode bits */
	outb_p(inb_p(SMBAUXCTL(pDevice)) &
		~(SMBAUXCTL_CRC | SMBAUXCTL_E32B), SMBAUXCTL(pDevice));

	pDevice->SMBusLocked = true;
	pDevice->InterruptRaised = false;

	cyapa_set_slave_addr(pDevice, 1);
	outb_p(cmd, SMBHSTCMD(pDevice));

	//Enable Hardware PEC
	outb_p(inb_p(SMBAUXCTL(pDevice)) & (~SMBAUXCTL_CRC),
		SMBAUXCTL(pDevice));

	//set block buffer mode
	outb_p(inb_p(SMBAUXCTL(pDevice)) | SMBAUXCTL_E32B, SMBAUXCTL(pDevice));
	if ((inb_p(SMBAUXCTL(pDevice)) & SMBAUXCTL_E32B) == 0) {
		DbgPrint("SMBus was unable to set block buffer mode :(\n");
		return 1;
	}

	inb_p(SMBHSTCNT(pDevice)); /* reset the data buffer index */

	//check if ready
	int status = inb_p(SMBHSTSTS(pDevice));
	if (status & SMBHSTSTS_HOST_BUSY) {
		DbgPrint("SMBus is Busy! Can't use it :(\n");
		return 1;
	}

	outb_p(0x55, SMBHSTCNT(pDevice));

	pDevice->SMBusInternalCallback = cyapa_read_block_callback;
	pDevice->SMBusUserCallback = callback;
	pDevice->SMBusUserCallbackArg = arg;

	//wait for interrupt
	return 0;
}

BOOLEAN cyapa_read_block_callback(PCYAPA_CONTEXT pDevice, int status) {
	CyapaPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL, "SMB Read Status: 0x%x\n", status);
	status &= SMBHSTSTS_INTR | STATUS_ERROR_FLAGS;
	if (status) {
		outb_p(status, SMBHSTSTS(pDevice));
	}

	unsigned char len = inb_p(SMBHSTDAT0(pDevice));
	if (len < 1 || len > I2C_SMBUS_BLOCK_MAX) {
		CyapaPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL, "SMBus invalid length received... %d\n", len);
		outb_p(inb_p(SMBAUXCTL(pDevice)) &
			~(SMBAUXCTL_CRC | SMBAUXCTL_E32B), SMBAUXCTL(pDevice));

		pDevice->SMBusLocked = false;
		pDevice->InterruptRaised = false;

		if (pDevice->SMBusUserCallback) {
			SMBUS_USER_CALLBACK callback = pDevice->SMBusUserCallback;
			PVOID arg = pDevice->SMBusUserCallbackArg;
			pDevice->SMBusUserCallback = NULL;
			pDevice->SMBusUserCallbackArg = NULL;

			callback(pDevice, false, NULL, 0, arg);
		}
		return TRUE;
	}

	uint8_t val[256];
	for (int i = 0; i < len; i++) {
		val[i] = inb_p(SMBBLKDAT(pDevice));
	}
	//disable hardware PEC
	outb_p(inb_p(SMBAUXCTL(pDevice)) &
		~(SMBAUXCTL_CRC | SMBAUXCTL_E32B), SMBAUXCTL(pDevice));

	pDevice->SMBusLocked = false;
	pDevice->InterruptRaised = false;

	if (pDevice->SMBusUserCallback) {
		SMBUS_USER_CALLBACK callback = pDevice->SMBusUserCallback;
		PVOID arg = pDevice->SMBusUserCallbackArg;
		pDevice->SMBusUserCallback = NULL;
		pDevice->SMBusUserCallbackArg = NULL;

		callback(pDevice, true, val, len, arg);
	}
	return TRUE;
}

BOOLEAN cyapa_write_block_callback(PCYAPA_CONTEXT pDevice, int status);
BOOLEAN cyapa_write_block_callback_done(PCYAPA_CONTEXT pDevice, int status);

uint8_t cyapa_write_block(PCYAPA_CONTEXT pDevice, uint8_t cmd, uint8_t *buf, uint8_t len, SMBUS_USER_CALLBACK callback, PVOID arg) {
	if (pDevice->SMBusLocked) {
		return 1;
	}
	CyapaPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL, "SMB Write block, cmd: 0x%x, len: 0x%x\n", cmd, len);

	//
	// Get the BUS_INTERFACE_STANDARD for our device so that we can
	// read & write to PCI config space.
	//
	NTSTATUS ntstatus = WdfFdoQueryForInterface(pDevice->FxDevice,
		&GUID_BUS_INTERFACE_STANDARD,
		(PINTERFACE)&pDevice->BusInterface,
		sizeof(BUS_INTERFACE_STANDARD),
		1, // Version
		NULL); //InterfaceSpecificData
	if (!NT_SUCCESS(ntstatus))
		return 1;

	/* Clear special mode bits */
	outb_p(inb_p(SMBAUXCTL(pDevice)) &
		~(SMBAUXCTL_CRC | SMBAUXCTL_E32B), SMBAUXCTL(pDevice));

	pDevice->SMBusLocked = true;
	pDevice->InterruptRaised = false;

	cyapa_set_slave_addr(pDevice, 0);
	outb_p(cmd, SMBHSTCMD(pDevice));

	//Disable Hardware PEC
	outb_p(inb_p(SMBAUXCTL(pDevice)) & (~SMBAUXCTL_CRC),
		SMBAUXCTL(pDevice));

	uint8_t hostc = 0;
	pDevice->BusInterface.GetBusData(
		pDevice->BusInterface.Context,
		PCI_WHICHSPACE_CONFIG, //READ config
		&hostc,
		SMBHSTCFG,
		1);

	CyapaPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL, "PCI Config hostc byte: 0x%x\n", hostc);

	uint8_t newhostc = hostc | SMBHSTCFG_I2C_EN;
	pDevice->BusInterface.SetBusData(
		pDevice->BusInterface.Context,
		PCI_WHICHSPACE_CONFIG, //WRITE config
		&newhostc,
		SMBHSTCFG,
		1); // set I2C_EN

	//check if ready
	int status = inb_p(SMBHSTSTS(pDevice));
	if (status & SMBHSTSTS_HOST_BUSY) {
		DbgPrint("SMBus is Busy! Can't use it :(\n");
		goto exit;
	}

	outb_p(len, SMBHSTDAT0(pDevice));
	outb_p(buf[0], SMBBLKDAT(pDevice));

	outb_p(0x55, SMBHSTCNT(pDevice));

	pDevice->SMBusBlockWriteBuf = buf;
	pDevice->SMBusBlockWriteIdx = 0;
	pDevice->SMBusBlockWriteLen = len;

	pDevice->SMBusHostc = hostc;
	
	pDevice->SMBusInternalCallback = cyapa_write_block_callback;
	pDevice->SMBusUserCallback = callback;
	pDevice->SMBusUserCallbackArg = arg;

	//wait for interrupt
	return 0;

exit:
	pDevice->BusInterface.SetBusData(
		pDevice->BusInterface.Context,
		PCI_WHICHSPACE_CONFIG, //WRITE config
		&hostc,
		SMBHSTCFG,
		1); //restore I2C_EN
	return 1;
}

BOOLEAN cyapa_write_block_callback_internal(PCYAPA_CONTEXT pDevice, int status, BOOLEAN done) {
	CyapaPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL, "SMB Write Done: %d, Status: 0x%x\n", done, status);
	if (!done && status & SMBHSTSTS_BYTE_DONE) {
		pDevice->SMBusBlockWriteIdx++;
		if (pDevice->SMBusBlockWriteIdx < pDevice->SMBusBlockWriteLen) {
			CyapaPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL, "SMB Wrote Byte %d of %d: 0x%x\n", pDevice->SMBusBlockWriteIdx, pDevice->SMBusBlockWriteLen, pDevice->SMBusBlockWriteBuf[pDevice->SMBusBlockWriteIdx]);
			outb_p(pDevice->SMBusBlockWriteBuf[pDevice->SMBusBlockWriteIdx], SMBBLKDAT(pDevice));
			pDevice->SMBusInternalCallback = cyapa_write_block_callback;
		}
		else {
			CyapaPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL, "SMB Write Done next!\n");
			pDevice->SMBusInternalCallback = cyapa_write_block_callback_done;
		}

		/* Clear BYTE_DONE to continue with next byte */
		outb_p(SMBHSTSTS_BYTE_DONE, SMBHSTSTS(pDevice));
		return TRUE;
	}

	status &= SMBHSTSTS_INTR | STATUS_ERROR_FLAGS;
	if (status) {
		outb_p(status, SMBHSTSTS(pDevice));
		
		if (NT_SUCCESS(status)) {
			pDevice->BusInterface.SetBusData(
				pDevice->BusInterface.Context,
				PCI_WHICHSPACE_CONFIG, //WRITE config
				&pDevice->SMBusHostc,
				SMBHSTCFG,
				1); //restore I2C_EN
		}

		//SMBus Error raised!
		pDevice->SMBusLocked = false;
		pDevice->InterruptRaised = false;

		if (status & STATUS_ERROR_FLAGS) {
			DbgPrint("SMB Write Returning due to error.\n");
		}
		else {
			CyapaPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL, "Finished SMB write without errors!\n");
		}

		if (pDevice->SMBusUserCallback) {
			SMBUS_USER_CALLBACK callback = pDevice->SMBusUserCallback;
			PVOID arg = pDevice->SMBusUserCallbackArg;
			pDevice->SMBusUserCallback = NULL;
			pDevice->SMBusUserCallbackArg = NULL;

			callback(pDevice, !(status & STATUS_ERROR_FLAGS), NULL, 0, arg);
		}
		return TRUE;
	}
	return FALSE;
}

BOOLEAN cyapa_write_block_callback(PCYAPA_CONTEXT pDevice, int status) {
	return cyapa_write_block_callback_internal(pDevice, status, FALSE);
}

BOOLEAN cyapa_write_block_callback_done(PCYAPA_CONTEXT pDevice, int status) {
	return cyapa_write_block_callback_internal(pDevice, status, TRUE);
}