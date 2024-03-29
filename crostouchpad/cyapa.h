/*
* Copyright (c) 2014 The DragonFly Project.  All rights reserved.
*
* This code is derived from software contributed to The DragonFly Project
* by Matthew Dillon <dillon@backplane.com>
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions
* are met:
*
* 1. Redistributions of source code must retain the above copyright
*    notice, this list of conditions and the following disclaimer.
* 2. Redistributions in binary form must reproduce the above copyright
*    notice, this list of conditions and the following disclaimer in
*    the documentation and/or other materials provided with the
*    distribution.
* 3. Neither the name of The DragonFly Project nor the names of its
*    contributors may be used to endorse or promote products derived
*    from this software without specific, prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
* ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
* LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
* FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
* COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
* INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
* BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
* LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
* AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
* OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
* OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
* SUCH DAMAGE.
*/

#include "stdint.h"

#ifndef __packed
#define __packed( __Declaration__ ) __pragma( pack(push, 1) ) __Declaration__ __pragma( pack(pop) )
#endif

#ifndef _SYS_DEV_SMBUS_CYAPA_CYAPA_H_
#define _SYS_DEV_SMBUS_CYAPA_CYAPA_H_

#define CYAPA_MAX_MT    5

/*
* Boot-time registers.  This is the device map
* if (stat & CYAPA_STAT_RUNNING) is 0.
*/
__packed(struct cyapa_boot_regs{
	uint8_t stat;			/* CYAPA_STAT_xxx */
	uint8_t boot;			/* CYAPA_BOOT_xxx */
	uint8_t error;
});

#define CYAPA_BOOT_BUSY		0x80
#define CYAPA_BOOT_RUNNING	0x10
#define CYAPA_BOOT_DATA_VALID	0x08
#define CYAPA_BOOT_CSUM_VALID	0x01

#define CYAPA_ERROR_INVALID     0x80
#define CYAPA_ERROR_INVALID_KEY 0x40
#define CYAPA_ERROR_BOOTLOADER	0x20
#define CYAPA_ERROR_CMD_CSUM    0x10
#define CYAPA_ERROR_FLASH_PROT  0x08
#define CYAPA_ERROR_FLASH_CSUM  0x04

/*
* Gen3 Operational Device Status Register
*
* bit 7: Valid interrupt source
* bit 6 - 4: Reserved
* bit 3 - 2: Power status
* bit 1 - 0: Device status
*/
#define REG_OP_STATUS     0x00
#define OP_STATUS_SRC     0x80
#define OP_STATUS_POWER   0x0c
#define OP_STATUS_DEV     0x03
#define OP_STATUS_MASK (OP_STATUS_SRC | OP_STATUS_POWER | OP_STATUS_DEV)

/*
* Operational Finger Count/Button Flags Register
*
* bit 7 - 4: Number of touched finger
* bit 3: Valid data
* bit 2: Middle Physical Button
* bit 1: Right Physical Button
* bit 0: Left physical Button
*/
#define REG_OP_DATA1       0x01
#define OP_DATA_VALID      0x08
#define OP_DATA_MIDDLE_BTN 0x04
#define OP_DATA_RIGHT_BTN  0x02
#define OP_DATA_LEFT_BTN   0x01
#define OP_DATA_BTN_MASK (OP_DATA_MIDDLE_BTN | OP_DATA_RIGHT_BTN | \
			  OP_DATA_LEFT_BTN)

__packed(struct cyapa_regs{
	uint8_t stat;
	uint8_t fngr;

	struct {
		uint8_t xy_high;        /* 7:4 high 4 bits of x */
		uint8_t x_low;          /* 3:0 high 4 bits of y */
		uint8_t y_low;
		uint8_t pressure;
		uint8_t id;             /* 1-15 incremented each touch */
	} touch[CYAPA_MAX_MT];
});

__packed(struct cyapa_cap{
	uint8_t prod_ida[5];    /* 0x00 - 0x04 */
	uint8_t prod_idb[6];    /* 0x05 - 0x0A */
	uint8_t prod_idc[2];    /* 0x0B - 0x0C */
	uint8_t reserved[6];    /* 0x0D - 0x12 */
	uint8_t buttons;        /* 0x13 */
	uint8_t gen;            /* 0x14, low 4 bits */
	uint8_t max_abs_xy_high;/* 0x15 7:4 high x bits, 3:0 high y bits */
	uint8_t max_abs_x_low;  /* 0x16 */
	uint8_t max_abs_y_low;  /* 0x17 */
	uint8_t phy_siz_xy_high;/* 0x18 7:4 high x bits, 3:0 high y bits */
	uint8_t phy_siz_x_low;  /* 0x19 */
	uint8_t phy_siz_y_low;  /* 0x1A */
});

#define CYAPA_STAT_RUNNING      0x80
#define CYAPA_STAT_PWR_MASK     0x0C
#define  CYAPA_PWR_OFF          0x00
#define  CYAPA_PWR_IDLE         0x08
#define  CYAPA_PWR_ACTIVE       0x0C

#define CYAPA_STAT_DEV_MASK     0x03
#define  CYAPA_DEV_NORMAL       0x03
#define  CYAPA_DEV_BUSY         0x01

#define CYAPA_FNGR_DATA_VALID   0x08
#define CYAPA_FNGR_MIDDLE       0x04 << 3
#define CYAPA_FNGR_RIGHT        0x02 << 3
#define CYAPA_FNGR_LEFT         0x01 << 3
#define CYAPA_FNGR_NUMFINGERS(c) (((c) >> 4) & 0x0F)

#define CYAPA_TOUCH_X(regs, i)  ((((regs)->touch[i].xy_high << 4) & 0x0F00) | \
				  (regs)->touch[i].x_low)
#define CYAPA_TOUCH_Y(regs, i)  ((((regs)->touch[i].xy_high << 8) & 0x0F00) | \
				  (regs)->touch[i].y_low)
#define CYAPA_TOUCH_P(regs, i)  ((regs)->touch[i].pressure)

#define  CMD_POWER_MODE_OFF	0x00
#define  CMD_POWER_MODE_IDLE	0x14
#define  CMD_POWER_MODE_FULL	0xFC

/* for byte read/write command */
#define CMD_RESET 0
#define CMD_POWER_MODE 1
#define CMD_DEV_STATUS 2
#define CMD_REPORT_MAX_BASELINE 3
#define CMD_REPORT_MIN_BASELINE 4
#define SMBUS_BYTE_CMD(cmd) (((cmd) & 0x3f) << 1)
#define CYAPA_SMBUS_RESET         SMBUS_BYTE_CMD(CMD_RESET)
#define CYAPA_SMBUS_POWER_MODE    SMBUS_BYTE_CMD(CMD_POWER_MODE)
#define CYAPA_SMBUS_DEV_STATUS    SMBUS_BYTE_CMD(CMD_DEV_STATUS)
#define CYAPA_SMBUS_MAX_BASELINE  SMBUS_BYTE_CMD(CMD_REPORT_MAX_BASELINE)
#define CYAPA_SMBUS_MIN_BASELINE  SMBUS_BYTE_CMD(CMD_REPORT_MIN_BASELINE)

/* for group registers read/write command */
#define REG_GROUP_DATA  0
#define REG_GROUP_CMD   2
#define REG_GROUP_QUERY 3
#define SMBUS_GROUP_CMD(grp) (0x80 | (((grp) & 0x07) << 3))

#define CYAPA_SMBUS_GROUP_DATA  SMBUS_GROUP_CMD(REG_GROUP_DATA)
#define CYAPA_SMBUS_GROUP_CMD   SMBUS_GROUP_CMD(REG_GROUP_CMD)
#define CYAPA_SMBUS_GROUP_QUERY SMBUS_GROUP_CMD(REG_GROUP_QUERY)

 /* for register block read/write command */
#define CMD_BL_STATUS		0
#define CMD_BL_HEAD		1
#define CMD_BL_CMD		2
#define CMD_BL_DATA		3
#define CMD_BL_ALL		4
#define CMD_BLK_PRODUCT_ID	5
#define CMD_BLK_HEAD		6
#define SMBUS_BLOCK_CMD(cmd) (0xc0 | (((cmd) & 0x1f) << 1))

/* register block read/write command in bootloader mode */
#define CYAPA_SMBUS_BL_STATUS SMBUS_BLOCK_CMD(CMD_BL_STATUS)
#define CYAPA_SMBUS_BL_HEAD   SMBUS_BLOCK_CMD(CMD_BL_HEAD)
#define CYAPA_SMBUS_BL_CMD    SMBUS_BLOCK_CMD(CMD_BL_CMD)
#define CYAPA_SMBUS_BL_DATA   SMBUS_BLOCK_CMD(CMD_BL_DATA)
#define CYAPA_SMBUS_BL_ALL    SMBUS_BLOCK_CMD(CMD_BL_ALL)

/* register block read/write command in operational mode */
#define CYAPA_SMBUS_BLK_PRODUCT_ID SMBUS_BLOCK_CMD(CMD_BLK_PRODUCT_ID)
#define CYAPA_SMBUS_BLK_HEAD       SMBUS_BLOCK_CMD(CMD_BLK_HEAD)

#define SMBUS_READ  0x01
#define SMBUS_WRITE 0x00
#define SMBUS_ENCODE_IDX(cmd, idx) ((cmd) | (((idx) & 0x03) << 1))
#define SMBUS_ENCODE_RW(cmd, rw) ((cmd) | ((rw) & 0x01))


/*struct cyapa_softc {
	uint32_t x;
	uint32_t y;
	bool mousedown;
	int mousebutton;
	int lastnfingers;
	int lastid = -1;
	bool mousedownfromtap;
	int tick = 0;
	int tickssincelastclick = 0;

	int multitaskinggesturetick = 0;
	int multitaskingx;
	int multitaskingy;
	bool multitaskingdone;
	bool hasmoved;

	int scrollratelimit = 0;
};*/

static int cyapa_minpressure = 16;
static int cyapa_norm_freq = 100;

#define ZSCALE		10
#define SIMULATE_BUT4	0x0100
#define SIMULATE_BUT5	0x0200
#define SIMULATE_LOCK	0x8000

#define MXT_T9_RELEASE		(1 << 5)
#define MXT_T9_PRESS		(1 << 6)
#define MXT_T9_DETECT		(1 << 7)

#endif