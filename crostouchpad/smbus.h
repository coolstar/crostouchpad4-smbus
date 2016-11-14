/* i2c_smbus_xfer read or write markers */
#define I2C_SMBUS_READ	1
#define I2C_SMBUS_WRITE	0

/* SMBus transaction types (size parameter in the above functions)
Note: these no longer correspond to the (arbitrary) PIIX4 internal codes! */
#define I2C_SMBUS_QUICK		    0
#define I2C_SMBUS_BYTE		    1
#define I2C_SMBUS_BYTE_DATA	    2
#define I2C_SMBUS_WORD_DATA	    3
#define I2C_SMBUS_PROC_CALL	    4
#define I2C_SMBUS_BLOCK_DATA	    5
#define I2C_SMBUS_I2C_BLOCK_BROKEN  6
#define I2C_SMBUS_BLOCK_PROC_CALL   7		/* SMBus 2.0 */
#define I2C_SMBUS_I2C_BLOCK_DATA    8

/* flags for the client struct: */
#define I2C_CLIENT_PEC          0x04    /* Use Packet Error Checking */

/*
 * Data for SMBus Messages
 */
#define I2C_SMBUS_BLOCK_MAX     32      /* As specified in SMBus standard */
union i2c_smbus_data {
	uint8_t byte;
	uint16_t word;
	uint8_t block[I2C_SMBUS_BLOCK_MAX + 2];
};

/* I801 SMBus address offsets */
#define SMBHSTSTS(p)	(0 + (p)->SMBusBase)
#define SMBHSTCNT(p)	(2 + (p)->SMBusBase)
#define SMBHSTCMD(p)	(3 + (p)->SMBusBase)
#define SMBHSTADD(p)	(4 + (p)->SMBusBase)
#define SMBHSTDAT0(p)	(5 + (p)->SMBusBase)
#define SMBHSTDAT1(p)	(6 + (p)->SMBusBase)
#define SMBBLKDAT(p)	(7 + (p)->SMBusBase)
#define SMBPEC(p)	(8 + (p)->SMBusBase)		/* ICH3 and later */
#define SMBAUXSTS(p)	(12 + (p)->SMBusBase)	/* ICH4 and later */
#define SMBAUXCTL(p)	(13 + (p)->SMBusBase)	/* ICH4 and later */
#define SMBSLVSTS(p)	(16 + (p)->SMBusBase)	/* ICH3 and later */
#define SMBSLVCMD(p)	(17 + (p)->SMBusBase)	/* ICH3 and later */
#define SMBNTFDADD(p)	(20 + (p)->SMBusBase)	/* ICH3 and later */
#define SMBNTFDDAT(p)	(22 + (p)->SMBusBase)	/* ICH3 and later */

/* PCI Address Constants */
#define SMBBAR		4
#define SMBPCICTL	0x004
#define SMBPCISTS	0x006
#define SMBHSTCFG	0x040
#define TCOBASE		0x050
#define TCOCTL		0x054

#define ACPIBASE		0x040
#define ACPIBASE_SMI_OFF	0x030
#define ACPICTRL		0x044
#define ACPICTRL_EN		0x080

#define SBREG_BAR		0x10
#define SBREG_SMBCTRL		0xc6000c

/* Host status bits for SMBPCISTS */
#define SMBPCISTS_INTS		0x08

/* Control bits for SMBPCICTL */
#define SMBPCICTL_INTDIS	0x0400

/* Host configuration bits for SMBHSTCFG */
#define SMBHSTCFG_HST_EN	1
#define SMBHSTCFG_SMB_SMI_EN	2
#define SMBHSTCFG_I2C_EN	4

/* TCO configuration bits for TCOCTL */
#define TCOCTL_EN		0x0100

/* Auxiliary status register bits, ICH4+ only */
#define SMBAUXSTS_CRCE		1
#define SMBAUXSTS_STCO		2

/* Auxiliary control register bits, ICH4+ only */
#define SMBAUXCTL_CRC		1
#define SMBAUXCTL_E32B		2

/* Other settings */
#define MAX_RETRIES		400

/* I801 command constants */
#define I801_QUICK		0x00
#define I801_BYTE		0x04
#define I801_BYTE_DATA		0x08
#define I801_WORD_DATA		0x0C
#define I801_PROC_CALL		0x10	/* unimplemented */
#define I801_BLOCK_DATA		0x14
#define I801_I2C_BLOCK_DATA	0x18	/* ICH5 and later */

/* I801 Host Control register bits */
#define SMBHSTCNT_INTREN	0x01
#define SMBHSTCNT_KILL		0x02
#define SMBHSTCNT_LAST_BYTE	0x20
#define SMBHSTCNT_START		0x40
#define SMBHSTCNT_PEC_EN	0x80	/* ICH3 and later */

/* I801 Hosts Status register bits */
#define SMBHSTSTS_BYTE_DONE	0x80
#define SMBHSTSTS_INUSE_STS	0x40
#define SMBHSTSTS_SMBALERT_STS	0x20
#define SMBHSTSTS_FAILED	0x10
#define SMBHSTSTS_BUS_ERR	0x08
#define SMBHSTSTS_DEV_ERR	0x04
#define SMBHSTSTS_INTR		0x02
#define SMBHSTSTS_HOST_BUSY	0x01

/* Host Notify Status registers bits */
#define SMBSLVSTS_HST_NTFY_STS	1

/* Host Notify Command registers bits */
#define SMBSLVCMD_HST_NTFY_INTREN	0x01

#define STATUS_ERROR_FLAGS	(SMBHSTSTS_FAILED | SMBHSTSTS_BUS_ERR | \
				 SMBHSTSTS_DEV_ERR)

#define STATUS_FLAGS		(SMBHSTSTS_BYTE_DONE | SMBHSTSTS_INTR | \
				 STATUS_ERROR_FLAGS)

enum {
	SMBusCallbackTypeNone,
	SMBusCallbackTypeReadByte,
	SMBusCallbackTypeWriteByte,
	SMBusCallbackTypeReadBlock,
	SMBusCallbackTypeWriteBlock,
	SMBusCallbackTypeWriteBlockDone
} SMBusCallbackType;