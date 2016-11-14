#include "driver.h"
#include <initguid.h>
#include <wdmguid.h>

static unsigned char inb_p(unsigned short int port) {
	unsigned char value = __inbyte(port);
	//DbgPrint("SMBus Read 0x%x: 0x%x\n", port, value);
	return value;
}

static void outb_p(unsigned char value, unsigned short int port) {
	//DbgPrint("SMBus Write 0x%x: 0x%x\n", port, value);
	__outbyte(port, value);
}

static void cyapa_set_slave_addr(PCYAPA_CONTEXT pDevice, int smbusread) {
	outb_p(0xce + smbusread, SMBHSTADD(pDevice)); // 0x67
}

uint32_t cyapa_read_byte(PCYAPA_CONTEXT pDevice, uint8_t cmd) {
	if (pDevice->SMBusLocked) {
		DbgPrint("SMBus is Locked! Can't use it :(\n");
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

	pDevice->SMBusCallbackType = SMBusCallbackTypeReadByte;

	//wait for interrupt
	return 0;
}

uint32_t cyapa_write_byte(PCYAPA_CONTEXT pDevice, uint8_t cmd, uint8_t value) {
	if (pDevice->SMBusLocked) {
		DbgPrint("SMBus is Locked! Can't use it :(\n");
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

	pDevice->SMBusCallbackType = SMBusCallbackTypeWriteByte;
	//wait for interrupt
	return 0;
}

uint32_t cyapa_read_block(PCYAPA_CONTEXT pDevice, uint8_t cmd) {
	if (pDevice->SMBusLocked) {
		//DbgPrint("SMBus is Locked! Can't use it :(\n");
		return 1;
	}

	if (cmd == 0x99)
		pDevice->CyapaBlockReadType = 2;
	else if (cmd == 0x81)
		pDevice->CyapaBlockReadType = 1;
	else
		pDevice->CyapaBlockReadType = 0;

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

	pDevice->SMBusCallbackType = SMBusCallbackTypeReadBlock;

	//wait for interrupt
	return 0;
}

uint8_t cyapa_write_block(PCYAPA_CONTEXT pDevice, uint8_t cmd, uint8_t *buf, uint8_t len) {
	if (pDevice->SMBusLocked) {
		//DbgPrint("SMBus is Locked! Can't use it :(\n");
		return 1;
	}
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

	uint8_t hostc;
	pDevice->BusInterface.GetBusData(
		pDevice->BusInterface.Context,
		PCI_WHICHSPACE_CONFIG, //READ config
		&hostc,
		SMBHSTCFG,
		1);

	DbgPrint("PCI Config hostc byte: 0x%x\n", hostc);

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

	pDevice->SMBusCallbackType = SMBusCallbackTypeWriteBlock;

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

uint8_t cyapa_write_byte_done(PCYAPA_CONTEXT pDevice) {
	pDevice->SMBusBlockWriteIdx++;
	if (pDevice->SMBusBlockWriteIdx < pDevice->SMBusBlockWriteLen) {
		DbgPrint("Wrote Byte %d of %d: 0x%x\n", pDevice->SMBusBlockWriteIdx, pDevice->SMBusBlockWriteLen, pDevice->SMBusBlockWriteBuf[pDevice->SMBusBlockWriteIdx]);
		outb_p(pDevice->SMBusBlockWriteBuf[pDevice->SMBusBlockWriteIdx], SMBBLKDAT(pDevice));
	}
	else {
		pDevice->SMBusCallbackType = SMBusCallbackTypeWriteBlockDone;
	}

	/* Clear BYTE_DONE to continue with next byte */
	outb_p(SMBHSTSTS_BYTE_DONE, SMBHSTSTS(pDevice));
	return 1;
}