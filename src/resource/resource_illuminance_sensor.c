/*
 * Copyright (c) 2018 Samsung Electronics Co., Ltd.
 *
 * Contact: Jin Yoon <jinny.yoon@samsung.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an AS IS BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdlib.h>
#include <unistd.h>
#include <peripheral_io.h>
#include <sys/time.h>

#include "log.h"
#include "resource_internal.h"

#define I2C_PIN_MAX 28
/* I2C */
#define GY30_ADDR 0x23 // 0x23 /* Address of GY30 light sensor */
#define GY30_CONT_HIGH_RES_MODE 0x10 /* Start measurement at 11x resolution. Measurement time is approx 120mx */
#define GY30_CONSTANT_NUM (1.2)


#define PM2008_ADDR									0x28

// Control modes
#define PM2008_CTRL_CLOSE_MEASUREMENT				0x1
#define PM2008_CTRL_OPEN_SINGLE_MEASUREMENT			0x2
#define PM2008_CTRL_SET_UP_CONTINUOUSLY_MEASUREMENT	0x3
#define PM2008_CTRL_SET_UP_TIMING_MEASUREMENT		0x4
#define PM2008_CTRL_SET_UP_DYNAMIC_MEASUREMENT		0x5
#define PM2008_CTRL_SET_UP_CALIBRATION_COEFFICIENT	0x6
#define PM2008_CTRL_SET_UP_WARM_MODE					0x7

#define PM2008_CONTROL_MODE							PM2008_CTRL_SET_UP_CONTINUOUSLY_MEASUREMENT

#define PM2008_MEASURING_TIME						180
#define PM2008_CALIBRATION_COEFFICIENT				70
#define PM2008_FRAME_HEADER							0x16

// Status
#define PM2008_STATUS_CLOSE							0x1
#define PM2008_STATUS_UNDER_MEASURING				0x2
#define PM2008_STATUS_FAILED							0x7
#define PM2008_STATUS_DATA_STABLE					0x80


static struct {
	int opened;
	peripheral_i2c_h sensor_h;
} resource_sensor_s;

void resource_close_illuminance_sensor(void)
{
	if (!resource_sensor_s.opened)
		return;

	_I("Illuminance Sensor is finishing...");
	peripheral_i2c_close(resource_sensor_s.sensor_h);
	resource_sensor_s.opened = 0;
}

int resource_read_illuminance_sensor(int i2c_bus, uint32_t *out_value)
{
	int ret = PERIPHERAL_ERROR_NONE;
	static int write = 0;
	unsigned char buf[32] = { 0, };

	if (!resource_sensor_s.opened) {
		ret = peripheral_i2c_open(i2c_bus, PM2008_ADDR, &resource_sensor_s.sensor_h);
		if (ret != PERIPHERAL_ERROR_NONE) {
			_E("i2c open error : %s", get_error_message(ret));
			return -1;
		}
		resource_sensor_s.opened = 1;
		write = 0;
	}
	buf[0] = PM2008_FRAME_HEADER;
	buf[1] = 0x7; // frame length
	buf[2] = PM2008_CONTROL_MODE;

	uint16_t data;

	switch (PM2008_CONTROL_MODE) {
		case PM2008_CTRL_SET_UP_CONTINUOUSLY_MEASUREMENT:
			data = 0xFFFF;
			break;
	    case PM2008_CTRL_SET_UP_CALIBRATION_COEFFICIENT:
	    		data = PM2008_CALIBRATION_COEFFICIENT;
	    		break;
	    default:
	    		data = PM2008_MEASURING_TIME;
	    		break;
	}

	buf[3] = data >> 8;
	buf[4] = data & 0xFF;
	buf[5] = 0; // Reserved

	// Calculate checksum
	buf[6] = buf[0];

	for (uint8_t i = 1; i < 6; i++) {
		buf[6] ^= buf[i];
	}



	//buf[0] = 0x10;

	if (!write) {
		ret = peripheral_i2c_write(resource_sensor_s.sensor_h, buf, 7);
		if (ret != PERIPHERAL_ERROR_NONE) {
			_E("i2c write error : %s", get_error_message(ret));
			return -1;
		}
		write = 1;
	}

	ret = peripheral_i2c_read(resource_sensor_s.sensor_h, buf, 32);
	if (ret != PERIPHERAL_ERROR_NONE) {
		_E("i2c read error : %s", get_error_message(ret));
		return -1;
	}

	//*out_value = (buf[0] << 8 | buf[1]) / GY30_CONSTANT_NUM; // Just Sum High 8bit and Low 8bit

	if (buf[0] != PM2008_FRAME_HEADER) {
		_E("i2c read error : frame header is different");
		return -1;
	}

	if (buf[1] != 32) {
		_E("i2c read error : frame length is not 32");
		return -1;
	}

	// Check checksum
	uint8_t check_code = buf[0];

	for (uint8_t i = 1; i < 31; i++) {
		check_code ^= buf[i];
	}

	if (buf[31] != check_code) {
		_E("i2c read error : check code is different");
		return -1;
	}

	uint8_t     status;
	uint16_t    measuring_mode;
	uint16_t    calibration_coefficient;
	uint16_t    pm1p0_grimm;
	uint16_t    pm2p5_grimm;
	uint16_t    pm10_grimm;
	uint16_t    pm1p0_tsi;
	uint16_t    pm2p5_tsi;
	uint16_t    pm10_tsi;
	uint16_t    number_of_0p3_um;
	uint16_t    number_of_0p5_um;
	uint16_t    number_of_1_um;
	uint16_t    number_of_2p5_um;
	uint16_t    number_of_5_um;
	uint16_t    number_of_10_um;

	/// Status
	status = buf[2];
	measuring_mode = (buf[3] << 8) + buf[4];
	calibration_coefficient = (buf[5] << 8) + buf[6];
	pm1p0_grimm = (buf[7] << 8) + buf[8];
	pm2p5_grimm = (buf[9] << 8) + buf[10];
	pm10_grimm = (buf[11] << 8) + buf[12];
	pm1p0_tsi = (buf[13] << 8) + buf[14];
	pm2p5_tsi= (buf[15] << 8) + buf[16];
	pm10_tsi= (buf[17] << 8) + buf[18];
	number_of_0p3_um= (buf[19] << 8) + buf[20];
	number_of_0p5_um= (buf[21] << 8) + buf[22];
	number_of_1_um= (buf[23] << 8) + buf[24];
	number_of_2p5_um= (buf[25] << 8) + buf[26];
	number_of_5_um= (buf[27] << 8) + buf[28];
	number_of_10_um= (buf[29] << 8) + buf[30];

	*out_value = pm10_tsi; //(buf[0] << 8 | buf[1]) / GY30_CONSTANT_NUM; // Just Sum High 8bit and Low 8bit

	return 0;
}
