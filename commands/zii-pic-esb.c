/*
 *  zii-pic-esb.c - Multifunction core driver for Zodiac Inflight Infotainment
 *  PIC MCU that is connected via dedicated UART port
 * (ESB board specific code)
 *
 * Copyright (C) 2015 Andrey Vostrikov <andrey.vostrikov@cogentembedded.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#define DEBUG

/* ESB and NIU have many common parts, reuse.
 * TODO: find which ones are different */

#include "zii-pic-niu.h"
#include "zii-pic-esb.h"

#define zii_pic_esb_process_status_response	zii_pic_niu_process_status_response
#define zii_pic_esb_process_watchdog_state	zii_pic_niu_process_watchdog_state
#define zii_pic_esb_process_reset_reason	zii_pic_niu_process_reset_reason
#define zii_pic_esb_process_12v			zii_pic_niu_process_12v
#define zii_pic_esb_process_5v			zii_pic_niu_process_5v
#define zii_pic_esb_process_3v3			zii_pic_niu_process_3v3
#define zii_pic_esb_process_temperature		zii_pic_niu_process_temperature
#define zii_pic_esb_process_eeprom_read		zii_pic_niu_process_eeprom_read
#define zii_pic_esb_process_eeprom_write	zii_pic_niu_process_eeprom_write
#define zii_pic_esb_process_firmware_version	zii_pic_niu_process_firmware_version
#define zii_pic_esb_process_bootloader_version	zii_pic_niu_process_bootloader_version

struct pic_cmd_desc zii_pic_esb_cmds[ZII_PIC_CMD_COUNT] = {
	[ZII_PIC_CMD_GET_STATUS] =
		{0x10, 0, zii_pic_esb_process_status_response},
	[ZII_PIC_CMD_SW_WDT_SET] =
		{0x1C, 3},
	[ZII_PIC_CMD_SW_WDT_GET] =
		{0x1C, 1, zii_pic_esb_process_watchdog_state},
	[ZII_PIC_CMD_PET_WDT] =
		{0x1D, 0},
	[ZII_PIC_CMD_RESET] =
		{0x1E, 1},
	[ZII_PIC_CMD_HW_RECOVERY_RESET] =
		{0, 0, NULL},
	[ZII_PIC_CMD_GET_RESET_REASON] =
		{0x1F, 0, zii_pic_esb_process_reset_reason},
	[ZII_PIC_CMD_GET_28V_READING] =
		{0, 0, NULL},
	[ZII_PIC_CMD_GET_12V_READING] =
		{0x2C, 0, zii_pic_esb_process_12v},
	[ZII_PIC_CMD_GET_5V_READING] =
		{0x2E, 0, zii_pic_esb_process_5v},
	[ZII_PIC_CMD_GET_3V3_READING] =
		{0x2F, 0, zii_pic_esb_process_3v3},
	[ZII_PIC_CMD_GET_TEMPERATURE] =
		{0x19, 0, zii_pic_esb_process_temperature},
	[ZII_PIC_CMD_EEPROM_READ] =
		{0x20, 3, zii_pic_esb_process_eeprom_read},
	[ZII_PIC_CMD_EEPROM_WRITE] =
		{0x20, 35, zii_pic_esb_process_eeprom_write},
	[ZII_PIC_CMD_GET_FIRMWARE_VERSION] =
		{0x11, 0, zii_pic_esb_process_firmware_version},
	[ZII_PIC_CMD_GET_BOOTLOADER_VERSION] =
		{0x12, 0, zii_pic_esb_process_bootloader_version},
	[ZII_PIC_CMD_DDS_EEPROM_READ] =
		{0, 0, NULL},
	[ZII_PIC_CMD_DDS_EEPROM_WRITE] =
		{0, 0, NULL},
};
