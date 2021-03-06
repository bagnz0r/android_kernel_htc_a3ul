/* Copyright (c) 2011-2013, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#define pr_fmt(fmt) "%s:%d " fmt, __func__, __LINE__

#include <linux/module.h>
#include "msm_sd.h"
#include "msm_actuator.h"
#include "msm_cci.h"

DEFINE_MSM_MUTEX(msm_actuator_mutex);

/*#define MSM_ACUTUATOR_DEBUG*/
#undef CDBG
#ifdef MSM_ACUTUATOR_DEBUG
#define CDBG(fmt, args...) pr_info("[CAM] : " fmt, ##args)
#else
#define CDBG(fmt, args...) pr_debug(fmt, ##args)
#endif

static struct msm_actuator msm_vcm_actuator_table;
static struct msm_actuator msm_piezo_actuator_table;

static struct i2c_driver msm_actuator_i2c_driver;
static struct msm_actuator *actuators[] = {
	&msm_vcm_actuator_table,
	&msm_piezo_actuator_table,
};

/*HTC_START Harvey 20130628 - Porting OIS*/
#if defined(CONFIG_TI201_ACT)
extern struct msm_actuator_ext ti201_act_ext;
extern int32_t ti201_act_set_af_value(struct msm_actuator_ctrl_t *a_ctrl, af_value_t af_value);
extern int32_t ti201_act_set_ois_mode(struct msm_actuator_ctrl_t *a_ctrl, int ois_mode);
extern int32_t ti201_act_update_ois_tbl(struct msm_actuator_ctrl_t *a_ctrl, struct sensor_actuator_info_t * sensor_actuator_info);
extern int g_support_ois;

#if defined(CONFIG_ACT_OIS_BINDER)
extern void HtcActOisBinder_i2c_add_driver(struct msm_camera_i2c_client* cam_i2c_client);
extern void HtcActOisBinder_open_init(void);
extern void HtcActOisBinder_power_down(void);
extern int32_t HtcActOisBinder_act_set_ois_mode(int ois_mode);
extern int32_t HtcActOisBinder_mappingTbl_i2c_write(int startup_mode, struct sensor_actuator_info_t * sensor_actuator_info);

static struct msm_camera_i2c_fn_t msm_sensor_cci_func_tbl;
#endif
#endif

#if defined(CONFIG_RUMBAS_ACT)
extern struct msm_actuator_ext rumbas_act_ext;
extern int32_t rumbas_act_set_af_value(struct msm_actuator_ctrl_t *a_ctrl, af_value_t af_value);
extern int32_t rumbas_act_set_ois_mode(struct msm_actuator_ctrl_t *a_ctrl, int ois_mode);
extern int32_t rumbas_act_update_ois_tbl(struct msm_actuator_ctrl_t *a_ctrl, struct sensor_actuator_info_t * sensor_actuator_info);
#endif

typedef struct {
  const char *act_name;
/*HTC_START Harvey 20130704 - Separate code by different actuator module and dynamic launch*/
  struct msm_actuator_ext *act_ext_info;
/*HTC_END*/
  int32_t (*set_af_value) (struct msm_actuator_ctrl_t *a_ctrl, af_value_t af_value); /*HTC Harvey 20130701 - Set otp af value*/
  int32_t (*set_ois_mode)(struct msm_actuator_ctrl_t *a_ctrl, int ois_mode);
  int32_t (*update_ois_tbl)(struct msm_actuator_ctrl_t *a_ctrl, struct sensor_actuator_info_t * sensor_actuator_info);
}act_func_t;

#define ACT_FUNC(name) { \
	.act_name = ""#name, \
	.act_ext_info = &name##_ext, \
	.set_af_value = name##_set_af_value, \
	.set_ois_mode = name##_set_ois_mode, \
	.update_ois_tbl = name##_update_ois_tbl, \
}

static act_func_t act_func[] = {
#if defined(CONFIG_TI201_ACT)
  ACT_FUNC(ti201_act),
#endif
#if defined(CONFIG_RUMBAS_ACT)
  ACT_FUNC(rumbas_act),
#endif
};

/*HTC_START Porting lc898212*/

#define DEFAULT_BIAS 0x40
#define DEFAULT_OFFSET 0x80
#define DEFAULT_INFINITY 0x7000
#define DEFAULT_MACRO -0x7000

/*HTC_START - For lc898212 VCM sorting*/
static int16_t lc898212_sorting_step_table[]	= {
    0x6000, 0x5A00, 0x5400, 0x4E00, 0x4800, 0x4200, 0x3C00, 0x3600, 0x3000, 0x2A00, 0x2400, 0x1E00, 0x1800, 0x1200, 0x0C00, 0x0600, 0x0000,
    0xFA00, 0xF400, 0xEE00, 0xE800, 0xE200, 0xDC00, 0xD600, 0xD000, 0xCA00, 0xC400, 0xBE00, 0xB800, 0xB200, 0xAC00, 0xA600, 0xA000
};
//HTC_END
static struct msm_camera_i2c_reg_array lc898212_settings_1[] = {
{0x80, 0x34},
{0x81, 0x20},
{0x84, 0xE0},
{0x87, 0x05},
{0xA4, 0x24},
{0x8B, 0x80},
{0x3A, 0x00},
{0x3B, 0x00},
{0x04, 0x00},
{0x05, 0x00},
{0x02, 0x00},
{0x03, 0x00},
{0x18, 0x00},
{0x19, 0x00},
{0x28, 0x80},
{0x29, 0x40},
{0x83, 0x2C},
{0x84, 0xE3},
{0x97, 0x00},
{0x98, 0x42},
{0x99, 0x00},
{0x9A, 0x00},
};

//ST1130305-1.h
static struct msm_camera_i2c_reg_array lc898212_settings_2_0x12[] = {
{0x88, 0x70},
{0x92, 0x00},
{0xA0, 0x01},
{0x7A, 0x68},
{0x7B, 0x00},
{0x7E, 0x78},
{0x7F, 0x00},
{0x7C, 0x03},
{0x7D, 0x00},
{0x93, 0x00},
{0x86, 0x60},

{0x40, 0x80},
{0x41, 0x10},
{0x42, 0x71},
{0x43, 0x50},
{0x44, 0x8F},
{0x45, 0x90},
{0x46, 0x61},
{0x47, 0xB0},
{0x48, 0x72},
{0x49, 0x10},
{0x76, 0x0C},
{0x77, 0x50},
{0x4A, 0x28},
{0x4B, 0x70},
{0x50, 0x04},
{0x51, 0xF0},
{0x52, 0x76},
{0x53, 0x10},
{0x54, 0x16},
{0x55, 0xC0},
{0x56, 0x00},
{0x57, 0x00},
{0x58, 0x7F},
{0x59, 0xF0},
{0x4C, 0x40},
{0x4D, 0x30},
{0x78, 0x60},
{0x79, 0x00},
{0x4E, 0x7F},
{0x4F, 0xF0},
{0x6E, 0x00},
{0x6F, 0x00},
{0x72, 0x18},
{0x73, 0xE0},
{0x74, 0x4E},
{0x75, 0x30},
{0x30, 0x00},
{0x31, 0x00},
{0x5A, 0x06},
{0x5B, 0x80},
{0x5C, 0x72},
{0x5D, 0xF0},
{0x5E, 0x7F},
{0x5F, 0x70},
{0x60, 0x7E},
{0x61, 0xD0},
{0x62, 0x7F},
{0x63, 0xF0},
{0x64, 0x00},
{0x65, 0x00},
{0x66, 0x00},
{0x67, 0x00},
{0x68, 0x51},
{0x69, 0x30},
{0x6A, 0x72},
{0x6B, 0xF0},
{0x70, 0x00},
{0x71, 0x00},
{0x6C, 0x80},
{0x6D, 0x10},

{0x76, 0x0c},
{0x77, 0x50},
{0x78, 0x40},
{0x79, 0x00},
{0x30, 0x00},
{0x31, 0x00},
};

// ST1130325-T2.h
static struct msm_camera_i2c_reg_array lc898212_settings_2_0x13[] = {
{0x88, 0x70},
{0x92, 0x00},
{0xA0, 0x01},
{0x7A, 0x68},
{0x7B, 0x00},
{0x7E, 0x78},
{0x7F, 0x00},
{0x7C, 0x03},
{0x7D, 0x00},
{0x93, 0x00},
{0x86, 0x60},

{0x40, 0x80},
{0x41, 0x10},
{0x42, 0x71},
{0x43, 0x50},
{0x44, 0x8F},
{0x45, 0x90},
{0x46, 0x61},
{0x47, 0xB0},
{0x48, 0x65},
{0x49, 0xB0},
{0x76, 0x0C},
{0x77, 0x50},
{0x4A, 0x28},
{0x4B, 0x70},
{0x50, 0x04},
{0x51, 0xF0},
{0x52, 0x76},
{0x53, 0x10},
{0x54, 0x16},
{0x55, 0xC0},
{0x56, 0x00},
{0x57, 0x00},
{0x58, 0x7F},
{0x59, 0xF0},
{0x4C, 0x40},
{0x4D, 0x30},
{0x78, 0x40},
{0x79, 0x00},
{0x4E, 0x7F},
{0x4F, 0xF0},
{0x6E, 0x00},
{0x6F, 0x00},
{0x72, 0x18},
{0x73, 0xE0},
{0x74, 0x4E},
{0x75, 0x30},
{0x30, 0x00},
{0x31, 0x00},
{0x5A, 0x06},
{0x5B, 0x80},
{0x5C, 0x72},
{0x5D, 0xF0},
{0x5E, 0x7F},
{0x5F, 0x70},
{0x60, 0x7E},
{0x61, 0xD0},
{0x62, 0x7F},
{0x63, 0xF0},
{0x64, 0x00},
{0x65, 0x00},
{0x66, 0x00},
{0x67, 0x00},
{0x68, 0x51},
{0x69, 0x30},
{0x6A, 0x72},
{0x6B, 0xF0},
{0x70, 0x00},
{0x71, 0x00},
{0x6C, 0x80},
{0x6D, 0x10},

{0x76, 0x0c},
{0x77, 0x50},
{0x78, 0x40},
{0x79, 0x00},
{0x30, 0x00},
{0x31, 0x00},
};

// ST1130325-T2.h, Fix the damping issue in the old components.(VCM_Vendor_Id_Version = 0x11), the table is based on the lc898212_settings_2_0x11 table and applied the vcm countermeasure patch.
static struct msm_camera_i2c_reg_array lc898212_settings_2_default[] = {
{0x88, 0x70},
{0x92, 0x00},
{0xA0, 0x01},
{0x7A, 0x68},
{0x7B, 0x00},
{0x7E, 0x78},
{0x7F, 0x00},
{0x7C, 0x03},
{0x7D, 0x00},
{0x93, 0x00},
{0x86, 0x60},

{0x40, 0x80},
{0x41, 0x10},
{0x42, 0x71},
{0x43, 0x10},//0x50
{0x44, 0x8F},
{0x45, 0x50},//0x90
{0x46, 0x61},
{0x47, 0xB0},
{0x48, 0x65},
{0x49, 0xB0},
{0x76, 0x08},//0x0c
{0x77, 0x50},
{0x4A, 0x28},
{0x4B, 0x70},
{0x50, 0x04},
{0x51, 0xF0},
{0x52, 0x76},
{0x53, 0x10},
{0x54, 0x16},
{0x55, 0xC0},
{0x56, 0x00},
{0x57, 0x00},
{0x58, 0x7F},
{0x59, 0xF0},
{0x4C, 0x40},
{0x4D, 0x30},
{0x78, 0x20},//0x40
{0x79, 0x00},
{0x4E, 0x7F},
{0x4F, 0xF0},
{0x6E, 0x00},
{0x6F, 0x00},
{0x72, 0x18},
{0x73, 0xE0},
{0x74, 0x4E},
{0x75, 0x30},
{0x30, 0x00},
{0x31, 0x00},
{0x5A, 0x06},
{0x5B, 0x80},
{0x5C, 0x72},
{0x5D, 0xF0},
{0x5E, 0x7F},
{0x5F, 0x70},
{0x60, 0x7E},
{0x61, 0xD0},
{0x62, 0x7F},
{0x63, 0xF0},
{0x64, 0x00},
{0x65, 0x00},
{0x66, 0x00},
{0x67, 0x00},
{0x68, 0x51},
{0x69, 0x30},
{0x6A, 0x72},
{0x6B, 0xF0},
{0x70, 0x00},
{0x71, 0x00},
{0x6C, 0x80},
{0x6D, 0x10},

{0x76, 0x08},//0x0c
{0x77, 0x50},
{0x78, 0x20},//0x40
{0x79, 0x00},
{0x30, 0x00},
{0x31, 0x00},
};


static struct msm_camera_i2c_reg_array lc898212_settings_3[] = {
{0x3A, 0x00},
{0x3B, 0x00}, // OFFSET Clear
{0x04, 0x00},
{0x05, 0x00}, // RZ Clear(Target Value)
{0x02, 0x00},
{0x03, 0x00}, // PIDZO Clear
// Calibration value (load from otp)
//{0x28, 0xaf}, // Read from OTP and write into 0x28h(Offset) @ fuse  0x3ce
//{0x29, 0x40}, // Read from OTP and write into 0x29h(Bias #17) @ fuse 0x3cd
// Servo ON
{0x85, 0xC0}, // AF filter,MS1 Clr (Need > 100uS delay time)
};
// HTC_START pg 20130221 step move
static struct msm_camera_i2c_reg_array lc898212_settings_4[] = {
// switch to stmv
{0x5A, 0x08},
{0x5B, 0x00},
{0x83, 0xac},
// stmv int time
{0xA0, 0x01},
};

/*HTC_START*/
#define STORE_OTP_INF //Record Original VCM infinity for MFG calibration

#ifdef STORE_OTP_INF
int g_infinity_pos = 100;
#endif
/*HTC_END*/

/*HTC_START - For lc898212 VCM sorting*/
static void lc898212_move_center(struct msm_actuator_ctrl_t *a_ctrl)
{
	int32_t rc = 0;
	pr_info("%s move center\n", __func__);
	rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_write(&a_ctrl->i2c_client, 0xa1, 0x0, MSM_CAMERA_I2C_WORD_DATA);
	if (rc < 0) {
	    pr_err("%s 0xa1 i2c write failed (%d)\n", __func__, rc);
	}

	rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_write(&a_ctrl->i2c_client, 0x16, 0xfe80, MSM_CAMERA_I2C_WORD_DATA);
	if (rc < 0) {
	    pr_err("%s 0x16 i2c write failed (%d)\n", __func__, rc);
	}

	rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_write(&a_ctrl->i2c_client, 0x8a, 5, MSM_CAMERA_I2C_BYTE_DATA);
	if (rc < 0) {
	    pr_err("%s 0x8a i2c write failed (%d)\n", __func__, rc);
	}
}
static void lc898212_sorting(struct msm_actuator_ctrl_t *a_ctrl, int16_t* final_max_diff)
{
	int i = 0, j = 0;
	int32_t rc = 0;
	int size =sizeof(lc898212_sorting_step_table)/sizeof(int16_t);
	uint16_t data = 0;
	int16_t temp_data[3];
	int16_t avg_data = 0;
	int16_t avg_data1[size];
	int16_t avg_data2[size];
	int16_t max_diff = 0;
	int16_t diff = 0;
	//Move initial position to center first.
	lc898212_move_center(a_ctrl);
	msleep(30);

    rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_write(&a_ctrl->i2c_client, 0x4C, 0x7F, MSM_CAMERA_I2C_BYTE_DATA);
	if (rc < 0) {
	    pr_err("%s i2c write 0x4c failed (%d)\n", __func__, rc);
	}
	rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_write(&a_ctrl->i2c_client, 0x4D, 0xF0, MSM_CAMERA_I2C_BYTE_DATA);
	if (rc < 0) {
	    pr_err("%s i2c write 0x4d failed (%d)\n", __func__, rc);
	}
	//#1. Move position from 0x6000(infinity) to 0xA000(macro), and every step is 0x600. Total 33 steps. For every step, get avg value from address 0x3c
	//#2. Move position from 0xA000(macro) to 0x6000(infinity), and every step is 0x600. Total 33 steps. For every step, get avg value from address 0x3c
	//#3. Compare the avg value of every step between #1 and #2, and then find the max diff value.

	pr_info("%s size(%d)\n", __func__, size);
	//#1
	for(i =0 ; i < size ; i++)
	{
	    pr_info("%s (%d)=0x%x\n", __func__, i, lc898212_sorting_step_table[i]);
	    rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_write(&a_ctrl->i2c_client, 0xa1, lc898212_sorting_step_table[i], MSM_CAMERA_I2C_WORD_DATA);
	    if (rc < 0) {
	        pr_err("%s 0xa1 i2c write failed (%d)\n", __func__, rc);
	    }
	    if(i == 0) //for first step, center move to infinity
	    rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_write(&a_ctrl->i2c_client, 0x16, 0x180, MSM_CAMERA_I2C_WORD_DATA);
	    else
	    rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_write(&a_ctrl->i2c_client, 0x16, 0xfe80, MSM_CAMERA_I2C_WORD_DATA);
	    /*
	    infinity -> macro : dir (1)   0xfe80
	    macro -> infinity : dir (-1)  0x180
	    */
	    if (rc < 0) {
	        pr_err("%s 0x16 i2c write failed (%d)\n", __func__, rc);
	    }

	    rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_write(&a_ctrl->i2c_client,
	         0x8a,
	         5,
	         MSM_CAMERA_I2C_BYTE_DATA);
	    if (rc < 0) {
	         pr_err("%s 0x8a i2c write failed (%d)\n", __func__, rc);
	    }

	    for (j = 0; j < 20 ; j++)
	    {
	        rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_read(&a_ctrl->i2c_client, 0x8a, &data, MSM_CAMERA_I2C_BYTE_DATA);
	        if (rc < 0) {
	            pr_err("%s:(%d) i2c read 0x8a failed (%d)\n", __func__, j , rc);
	        }
	        else
	        {
	            if((data & 0x1) == 0)
	            {
	                pr_info("%s:(%d) move done, break\n", __func__, j);
	                break;
	            }
	            else
	                msleep(5);
	        }
	    }

	    msleep(30);
	    for (j = 0; j < 3 ; j++)
	    {
	        temp_data[j] = 0;
	        rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_read(&a_ctrl->i2c_client, 0x3c, &data, MSM_CAMERA_I2C_WORD_DATA);
	        if (rc < 0) {
	            pr_err("%s: (%d)(%d) i2c read 0x3c failed (%d)\n", __func__, i, j, rc);
	        }
	        else
	        {
	            pr_info("%s: (%d)(%d) i2c read 0x3c :(0x%x)(%d)\n", __func__, i, j, data,data);
	            temp_data[j]= (int16_t)data;
	        }
	    }
	    avg_data = (temp_data[0] + temp_data[1] + temp_data[2])/3 ;
	    pr_info("%s: (%d) avg :(0x%x)\n", __func__, i, avg_data);
	    avg_data1[i] = avg_data;
	}

	//#2
	for(i = size-1 ; i >= 0 ; i--)
	{
	    pr_info("%s (%d)=0x%x\n", __func__, i, lc898212_sorting_step_table[i]);
	    rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_write(&a_ctrl->i2c_client, 0xa1, lc898212_sorting_step_table[i], MSM_CAMERA_I2C_WORD_DATA);
	    if (rc < 0) {
	        pr_err("%s 0xa1 i2c write failed (%d)\n", __func__, rc);
	    }

	    rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_write(&a_ctrl->i2c_client, 0x16, 0x180, MSM_CAMERA_I2C_WORD_DATA);
	    /*
	    infinity -> macro : dir (1)   0xfe80
	    macro -> infinity : dir (-1)  0x180
	    */
	    if (rc < 0) {
	        pr_err("%s 0x16 i2c write failed (%d)\n", __func__, rc);
	    }

	    rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_write(&a_ctrl->i2c_client, 0x8a, 5, MSM_CAMERA_I2C_BYTE_DATA);
	    if (rc < 0) {
	        pr_err("%s 0x8a i2c write failed (%d)\n", __func__, rc);
	    }

	    for (j = 0; j < 20 ; j++)
	    {
	        rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_read(&a_ctrl->i2c_client, 0x8a, &data, MSM_CAMERA_I2C_BYTE_DATA);
	        if (rc < 0) {
	            pr_err("%s:(%d) i2c read 0x8a failed (%d)\n", __func__, j , rc);
	        }
	        else
	        {
	            if((data & 0x1) == 0)
	            {
	                pr_info("%s:(%d) move done, break\n", __func__, j);
	                break;
	            }
	            else
	                msleep(5);
	        }
	    }

	    msleep(30);
	    for (j = 0; j < 3 ; j++)
	    {
	        temp_data[j] = 0;
	        rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_read(&a_ctrl->i2c_client, 0x3c, &data, MSM_CAMERA_I2C_WORD_DATA);
	        if (rc < 0) {
	            pr_err("%s: (%d)(%d) i2c read 0x3c failed (%d)\n", __func__, i, j, rc);
	        }
	        else
	        {
	            pr_info("%s: (%d)(%d) i2c read 0x3c :(0x%x)(%d)\n", __func__, i, j, data,data);
	            temp_data[j]= (int16_t)data;
	        }
	    }
	    avg_data = (temp_data[0] + temp_data[1] + temp_data[2])/3 ;
	    pr_info("%s: (%d) avg :(0x%x)\n", __func__, i, avg_data);
	    avg_data2[i] = avg_data;
	}

	//#3
	for(i =0 ; i < size ; i++)
	{
	    pr_info("%s: (%d) avg_data :(0x%x, 0x%x)\n", __func__, i, avg_data1[i], avg_data2[i]);
	    if(avg_data1[i] > avg_data2[i])
	        diff = avg_data1[i] - avg_data2[i];
	    else
	        diff = avg_data2[i] - avg_data1[i];

	    if(diff > max_diff)
	        max_diff = diff;
	    pr_info("%s: (%d) diff :(0x%x, 0x%x)\n", __func__, i, diff, max_diff);
	}
	*final_max_diff = max_diff;

	rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_write(&a_ctrl->i2c_client, 0x4C, 0x40, MSM_CAMERA_I2C_BYTE_DATA);
	if (rc < 0) {
	    pr_err("%s i2c write 0x4c failed (%d)\n", __func__, rc);
	}
	rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_write(&a_ctrl->i2c_client, 0x4D, 0x30, MSM_CAMERA_I2C_BYTE_DATA);
	if (rc < 0) {
	    pr_err("%s i2c write 0x4d failed (%d)\n", __func__, rc);
	}
	lc898212_move_center(a_ctrl);
}

static void lc898212_loop_gain_sorting(struct msm_actuator_ctrl_t *a_ctrl, uint32_t* gain_G1, uint32_t* gain_G2, uint8_t vcm_freq, uint16_t vcm_freq_ms22e)
{
	int i = 0, j = 0;
	int32_t rc = 0;
	uint16_t data = 0;
	uint16_t data_0x3a = 0;
	uint16_t data_0x6e = 0;
	uint16_t data_0x83 = 0;
	uint16_t data_0x87 = 0;
	uint16_t data_0x8f = 0;
	uint16_t data_0xa4 = 0;
	uint16_t data_0x76 = 0;

	uint16_t data_0x62 = 0;
	uint16_t data_0x64 = 0;
	uint16_t data_0x66 = 0;
	uint16_t data_0x68 = 0;
	uint16_t data_0x6A = 0;
	uint16_t data_0x70 = 0;
	uint16_t data_0x6C = 0;

	uint16_t data_0x4C = 0;
	uint16_t G1_H = 0, G1_L = 0, G2_H = 0, G2_L = 0 ;
	uint32_t G1 = 0, G2 = 0 ;

//===============
//READ OFFSET (3Ah)
//READ ms22e (6Eh)
//READ SWTCH (83h)
//READ EQENBL (87h)
//READ MSSET (8Fh)
//READ ANA2 (A4h)

	rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_read(&a_ctrl->i2c_client, 0x3a, &data_0x3a, MSM_CAMERA_I2C_WORD_DATA);
	if (rc < 0)
	{
		pr_err("%s: i2c read 0x3a failed (%d)\n", __func__, rc);
	}

	rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_read(&a_ctrl->i2c_client, 0x6e, &data_0x6e, MSM_CAMERA_I2C_WORD_DATA);
	if (rc < 0)
	{
		pr_err("%s: i2c read 0x6e failed (%d)\n", __func__, rc);
	}

	rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_read(&a_ctrl->i2c_client, 0x83, &data_0x83, MSM_CAMERA_I2C_BYTE_DATA);
	if (rc < 0)
	{
		pr_err("%s: i2c read 0x83 failed (%d)\n", __func__, rc);
	}

	rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_read(&a_ctrl->i2c_client, 0x87, &data_0x87, MSM_CAMERA_I2C_BYTE_DATA);
	if (rc < 0)
	{
		pr_err("%s: i2c read 0x87 failed (%d)\n", __func__, rc);
	}

	rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_read(&a_ctrl->i2c_client, 0x8f, &data_0x8f, MSM_CAMERA_I2C_BYTE_DATA);
	if (rc < 0)
	{
		pr_err("%s: i2c read 0x8f failed (%d)\n", __func__, rc);
	}

	rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_read(&a_ctrl->i2c_client, 0xa4, &data_0xa4, MSM_CAMERA_I2C_BYTE_DATA);
	if (rc < 0)
	{
		pr_err("%s: i2c read 0xa4 failed (%d)\n", __func__, rc);
	}

//===============
//READ A5h = 0x00 , SET A4h = 0x76
//READ A5h != 0x00, Don't Change
	rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_read(&a_ctrl->i2c_client, 0xa5, &data, MSM_CAMERA_I2C_BYTE_DATA);
	if (rc < 0)
	{
		pr_err("%s: i2c read 0xa5 failed (%d)\n", __func__, rc);
	}
	pr_info("%s addr 0xa5:0x%x\n", __func__, data);
	if(data == 0)
	{
		rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_write(&a_ctrl->i2c_client, 0xa4, 0x76, MSM_CAMERA_I2C_BYTE_DATA);
		if (rc < 0) {
	    pr_err("%s i2c write 0xa4 failed (%d)\n", __func__, rc);
		}
	}

//===============
//EQENBL(87h) = 0x00

	rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_write(&a_ctrl->i2c_client, 0x87, 0, MSM_CAMERA_I2C_BYTE_DATA);
	if (rc < 0) {
	    pr_err("%s i2c write 0x87 failed (%d)\n", __func__, rc);
	}

//===============
//READ PX12 (76h)

	rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_read(&a_ctrl->i2c_client, 0x76, &data_0x76, MSM_CAMERA_I2C_WORD_DATA);
	if (rc < 0)
	{
		pr_err("%s: i2c read 0x76 failed (%d)\n", __func__, rc);
	}

//======
//PX12(76h Bit9~Bit0) = 0

	data =  data_0x76 & 0xfc00;
	rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_write(&a_ctrl->i2c_client, 0x76, data, MSM_CAMERA_I2C_WORD_DATA);
	if (rc < 0) {
	    pr_err("%s i2c write 0x87 failed (%d)\n", __func__, rc);
	}

//======
//READ ms21a (62h)
//READ ms21b (64h)
//READ ms21c (66h)
//READ ms22a (68h)
//READ ms22c (6Ah)
//READ ms23p (70h)
//READ ms22d (6Ch)

	rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_read(&a_ctrl->i2c_client, 0x62, &data_0x62, MSM_CAMERA_I2C_WORD_DATA);
	if (rc < 0)
	{
		pr_err("%s: i2c read 0x62 failed (%d)\n", __func__, rc);
	}

	rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_read(&a_ctrl->i2c_client, 0x64, &data_0x64, MSM_CAMERA_I2C_WORD_DATA);
	if (rc < 0)
	{
		pr_err("%s: i2c read 0x64 failed (%d)\n", __func__, rc);
	}

	rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_read(&a_ctrl->i2c_client, 0x66, &data_0x66, MSM_CAMERA_I2C_WORD_DATA);
	if (rc < 0)
	{
		pr_err("%s: i2c read 0x66 failed (%d)\n", __func__, rc);
	}

	rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_read(&a_ctrl->i2c_client, 0x68, &data_0x68, MSM_CAMERA_I2C_WORD_DATA);
	if (rc < 0)
	{
		pr_err("%s: i2c read 0x68 failed (%d)\n", __func__, rc);
	}

	rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_read(&a_ctrl->i2c_client, 0x6A, &data_0x6A, MSM_CAMERA_I2C_WORD_DATA);
	if (rc < 0)
	{
		pr_err("%s: i2c read 0x6A failed (%d)\n", __func__, rc);
	}

	rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_read(&a_ctrl->i2c_client, 0x70, &data_0x70, MSM_CAMERA_I2C_WORD_DATA);
	if (rc < 0)
	{
		pr_err("%s: i2c read 0x70 failed (%d)\n", __func__, rc);
	}

	rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_read(&a_ctrl->i2c_client, 0x6C, &data_0x6C, MSM_CAMERA_I2C_WORD_DATA);
	if (rc < 0)
	{
		pr_err("%s: i2c read 0x6C failed (%d)\n", __func__, rc);
	}

//======
//ms21a (62h) = ms11a (5Ah)
//ms21b (64h) = ms11a (5Ah)
//ms21c (66h) = ms11c (5Ch)
//ms22a (68h) = ms12a (5Eh)
//ms22c (6Ah) = ms12c (60h)
//Workaround to set (0x5A) = 0x680
	rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_write(&a_ctrl->i2c_client, 0x5A, 0x680, MSM_CAMERA_I2C_WORD_DATA);
	if (rc < 0) {
	    pr_err("%s i2c write 0x5A failed (%d)\n", __func__, rc);
	}

	rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_read(&a_ctrl->i2c_client, 0x5A, &data, MSM_CAMERA_I2C_WORD_DATA);
	if (rc < 0)
	{
		pr_err("%s: i2c read 0x5A failed (%d)\n", __func__, rc);
	}
	else
		pr_info("%s: i2c read 0x5A (0x%x)\n", __func__, data);

	rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_write(&a_ctrl->i2c_client, 0x62, data, MSM_CAMERA_I2C_WORD_DATA);
	if (rc < 0) {
	    pr_err("%s i2c write 0x62 failed (%d)\n", __func__, rc);
	}

	rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_write(&a_ctrl->i2c_client, 0x64, data, MSM_CAMERA_I2C_WORD_DATA);
	if (rc < 0) {
	    pr_err("%s i2c write 0x64 failed (%d)\n", __func__, rc);
	}

	rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_read(&a_ctrl->i2c_client, 0x5C, &data, MSM_CAMERA_I2C_WORD_DATA);
	if (rc < 0)
	{
		pr_err("%s: i2c read 0x5C failed (%d)\n", __func__, rc);
	}
	rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_write(&a_ctrl->i2c_client, 0x66, data, MSM_CAMERA_I2C_WORD_DATA);
	if (rc < 0) {
	    pr_err("%s i2c write 0x66 failed (%d)\n", __func__, rc);
	}

	rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_read(&a_ctrl->i2c_client, 0x5E, &data, MSM_CAMERA_I2C_WORD_DATA);
	if (rc < 0)
	{
		pr_err("%s: i2c read 0x5E failed (%d)\n", __func__, rc);
	}
	rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_write(&a_ctrl->i2c_client, 0x68, data, MSM_CAMERA_I2C_WORD_DATA);
	if (rc < 0) {
	    pr_err("%s i2c write 0x68 failed (%d)\n", __func__, rc);
	}

	rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_read(&a_ctrl->i2c_client, 0x60, &data, MSM_CAMERA_I2C_WORD_DATA);
	if (rc < 0)
	{
		pr_err("%s: i2c read 0x60 failed (%d)\n", __func__, rc);
	}
	rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_write(&a_ctrl->i2c_client, 0x6A, data, MSM_CAMERA_I2C_WORD_DATA);
	if (rc < 0) {
	    pr_err("%s i2c write 0x6A failed (%d)\n", __func__, rc);
	}

//=========
//ms23p (70h) = 0x0000
//ms22d (6Ch) = 0x0000
//RZ (04h) = 0x0000
//OFFSET (3Ah) = 0x0000

    rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_write(&a_ctrl->i2c_client, 0x70, 0, MSM_CAMERA_I2C_WORD_DATA);
    if (rc < 0) {
	    pr_err("%s i2c write 0x70 failed (%d)\n", __func__, rc);
    }

    rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_write(&a_ctrl->i2c_client, 0x6C, 0, MSM_CAMERA_I2C_WORD_DATA);
    if (rc < 0) {
	    pr_err("%s i2c write 0x6C failed (%d)\n", __func__, rc);
    }

    rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_write(&a_ctrl->i2c_client, 0x04, 0, MSM_CAMERA_I2C_WORD_DATA);
    if (rc < 0) {
	    pr_err("%s i2c write 0x04 failed (%d)\n", __func__, rc);
    }

    rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_write(&a_ctrl->i2c_client, 0x3A, 0, MSM_CAMERA_I2C_WORD_DATA);
    if (rc < 0) {
	    pr_err("%s i2c write 0x3A failed (%d)\n", __func__, rc);
    }

//=========
//ms22e (6Eh)= 0x2000

    rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_write(&a_ctrl->i2c_client, 0x6e, vcm_freq_ms22e, MSM_CAMERA_I2C_WORD_DATA);
    if (rc < 0) {
	    pr_err("%s i2c write 0x6e failed (%d)\n", __func__, rc);
    }

//======
//READ gain1 (4Ch)
    rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_read(&a_ctrl->i2c_client, 0x4c, &data_0x4C, MSM_CAMERA_I2C_WORD_DATA);
    if (rc < 0)
    {
		pr_err("%s: i2c read 0x4c failed (%d)\n", __func__, rc);
    }

//======
//SWTCH (83h) = 0x56

    rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_write(&a_ctrl->i2c_client, 0x83, 0x56, MSM_CAMERA_I2C_BYTE_DATA);
    if (rc < 0) {
	    pr_err("%s i2c write 0x83 failed (%d)\n", __func__, rc);
    }

//====
//EQENBL(87h) = 0x80

    rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_write(&a_ctrl->i2c_client, 0x87, 0x80, MSM_CAMERA_I2C_BYTE_DATA);
    if (rc < 0) {
	    pr_err("%s i2c write 0x87 failed (%d)\n", __func__, rc);
    }

//====
//DLYCLR (85h) = 0xE0

    rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_write(&a_ctrl->i2c_client, 0x85, 0xE0, MSM_CAMERA_I2C_BYTE_DATA);
    if (rc < 0) {
	    pr_err("%s i2c write 0x85 failed (%d)\n", __func__, rc);
    }

//======
//Wait DLYCLR (85h Bit6 ~ Bit7) = 0
    for (j = 0; j < 20 ; j++)
    {
        rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_read(&a_ctrl->i2c_client, 0x85, &data, MSM_CAMERA_I2C_BYTE_DATA);
        if (rc < 0) {
            pr_err("%s:(%d) i2c read 0x85 failed (%d)\n", __func__, j , rc);
        }
        else
        {
            if((data & 0xC0) == 0)
            {
                pr_info("%s:(%d) ram clean done, break\n", __func__, j);
                break;
            }
            else
                msleep(5);
        }
    }

//========
//SWTCH (83h) = 0x56

    rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_write(&a_ctrl->i2c_client, 0x83, 0x56, MSM_CAMERA_I2C_BYTE_DATA);
    if (rc < 0) {
	    pr_err("%s i2c write 0x83 failed (%d)\n", __func__, rc);
    }

//==========
//EQENBL(87h) = 0x87

    rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_write(&a_ctrl->i2c_client, 0x87, 0x87, MSM_CAMERA_I2C_BYTE_DATA);
    if (rc < 0) {
	    pr_err("%s i2c write 0x87 failed (%d)\n", __func__, rc);
    }

//====
//SWFC (8Ch)= 0x7E
    pr_err("%s  vcm_freq:0x%x\n", __func__, vcm_freq);
    rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_write(&a_ctrl->i2c_client, 0x8C, vcm_freq, MSM_CAMERA_I2C_BYTE_DATA);
    if (rc < 0) {
	    pr_err("%s  i2c write 0x8C failed (%d)\n", __func__, rc);
    }

//====
//SWEN (8Dh)= 0x80

    rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_write(&a_ctrl->i2c_client, 0x8D, 0x80, MSM_CAMERA_I2C_BYTE_DATA);
    if (rc < 0) {
	    pr_err("%s  i2c write 0x8D failed (%d)\n", __func__, rc);
    }

//=======
//MSNUM (A3h) = 0x0A

    rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_write(&a_ctrl->i2c_client, 0xA3, 0x0A, MSM_CAMERA_I2C_BYTE_DATA);
    if (rc < 0) {
	    pr_err("%s  i2c write 0xA3 failed (%d)\n", __func__, rc);
    }

//=====
//Wait 100ms

    msleep(100);

//=====
    for (i = 0; i < 5 ; i++)
    {
        //MSSET (8Fh) = 0x80
        rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_write(&a_ctrl->i2c_client, 0x8F, 0x80, MSM_CAMERA_I2C_BYTE_DATA);
        if (rc < 0) {
		    pr_err("%s  i2c write 0x8F failed (%d)\n", __func__, rc);
        }
        //wait MSSET (8Fh Bit7) = 0
        for (j = 0; j < 20 ; j++)
        {
            rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_read(&a_ctrl->i2c_client, 0x8F, &data, MSM_CAMERA_I2C_BYTE_DATA);
            if (rc < 0) {
                pr_err("%s:(%d) i2c read 0x8F failed (%d)\n", __func__, j , rc);
            }
            else
            {
                if((data & 0x80) == 0)
                {
                    pr_info("%s:(%d) set done, break\n", __func__, j);
                    break;
                }
                else
                    msleep(5);
            }
        }

        //READ MSR1CH (32h) for G1_H
        //READ MSR1CL (34h) for G1_L
        //READ MSR2CH (36h) for G2_H
        //READ MSR2CL (38h) for G2_L
        rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_read(&a_ctrl->i2c_client, 0x32, &G1_H, MSM_CAMERA_I2C_WORD_DATA);
        if (rc < 0)
        {
            pr_err("%s: i2c read 0x32 failed (%d)\n", __func__, rc);
		}

        rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_read(&a_ctrl->i2c_client, 0x34, &G1_L, MSM_CAMERA_I2C_WORD_DATA);
        if (rc < 0)
        {
            pr_err("%s: i2c read 0x34 failed (%d)\n", __func__, rc);
        }

        rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_read(&a_ctrl->i2c_client, 0x36, &G2_H, MSM_CAMERA_I2C_WORD_DATA);
        if (rc < 0)
        {
            pr_err("%s: i2c read 0x36 failed (%d)\n", __func__, rc);
        }

        rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_read(&a_ctrl->i2c_client, 0x38, &G2_L, MSM_CAMERA_I2C_WORD_DATA);
        if (rc < 0)
        {
            pr_err("%s: i2c read 0x38 failed (%d)\n", __func__, rc);
        }

        G1 = (G1_H << 16) | G1_L ;
        G2 = (G2_H << 16) | G2_L ;
        pr_info("%s: (%d) G1:(0x%x), G1_H:0x%x, G1_L:0x%x \n", __func__, i, G1, G1_H, G1_L);
        pr_info("%s: (%d) G2:(0x%x), G2_H:0x%x, G2_L:0x%x \n", __func__, i, G2, G2_H, G2_L);

        gain_G1[i]= G1;
        gain_G2[i]= G2;
    }

//====
//SWFC (8Ch)= 0x00

    rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_write(&a_ctrl->i2c_client, 0x8C, 0x0, MSM_CAMERA_I2C_BYTE_DATA);
    if (rc < 0) {
	    pr_err("%s  i2c write 0x8C failed (%d)\n", __func__, rc);
    }

//====
//SWEN (8Dh)= 0x00

    rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_write(&a_ctrl->i2c_client, 0x8D, 0x0, MSM_CAMERA_I2C_BYTE_DATA);
    if (rc < 0) {
	    pr_err("%s  i2c write 0x8D failed (%d)\n", __func__, rc);
    }

//====
//MSNUM (A3h) = 0x00

    rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_write(&a_ctrl->i2c_client, 0xA3, 0x0, MSM_CAMERA_I2C_BYTE_DATA);
    if (rc < 0) {
	    pr_err("%s  i2c write 0xA3 failed (%d)\n", __func__, rc);
    }

//====
//DLYCLR (85h) = 0xE0

    rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_write(&a_ctrl->i2c_client, 0x85, 0xE0, MSM_CAMERA_I2C_BYTE_DATA);
    if (rc < 0) {
	    pr_err("%s  i2c write 0x85 failed (%d)\n", __func__, rc);
    }

//====
//wait DLYCLR (85h Bit7 ~ Bit5) = 0
    for (j = 0; j < 20 ; j++)
    {
        rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_read(&a_ctrl->i2c_client, 0x85, &data, MSM_CAMERA_I2C_BYTE_DATA);
        if (rc < 0) {
            pr_err("%s:(%d) i2c read 0x85 failed (%d)\n", __func__, j , rc);
        }
        else
        {
            if((data & 0xE0) == 0)
            {
                pr_info("%s:(%d) ram clean done, break\n", __func__, j);
                break;
            }
            else
                msleep(5);
        }
    }

//=====
//restore
//ms21a (62h)
//ms21b (64h)
//ms21c (66h)
//ms22a (68h)
//ms22c (6Ah)
//ms23p (70h)
//ms22d (6Ch)
//PX12 (76h)

    rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_write(&a_ctrl->i2c_client, 0x62, data_0x62, MSM_CAMERA_I2C_WORD_DATA);
    if (rc < 0) {
	    pr_err("%s i2c write 0x62 failed (%d)\n", __func__, rc);
    }

    rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_write(&a_ctrl->i2c_client, 0x64, data_0x64, MSM_CAMERA_I2C_WORD_DATA);
    if (rc < 0) {
	    pr_err("%s i2c write 0x64 failed (%d)\n", __func__, rc);
    }

    rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_write(&a_ctrl->i2c_client, 0x66, data_0x66, MSM_CAMERA_I2C_WORD_DATA);
    if (rc < 0) {
	    pr_err("%s i2c write 0x66 failed (%d)\n", __func__, rc);
    }

    rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_write(&a_ctrl->i2c_client, 0x68, data_0x68, MSM_CAMERA_I2C_WORD_DATA);
    if (rc < 0) {
	    pr_err("%s i2c write 0x68 failed (%d)\n", __func__, rc);
    }

    rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_write(&a_ctrl->i2c_client, 0x6A, data_0x6A, MSM_CAMERA_I2C_WORD_DATA);
    if (rc < 0) {
	    pr_err("%s i2c write 0x6A failed (%d)\n", __func__, rc);
    }

    rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_write(&a_ctrl->i2c_client, 0x70, data_0x70, MSM_CAMERA_I2C_WORD_DATA);
    if (rc < 0) {
	    pr_err("%s i2c write 0x70 failed (%d)\n", __func__, rc);
    }

    rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_write(&a_ctrl->i2c_client, 0x6C, data_0x6C, MSM_CAMERA_I2C_WORD_DATA);
    if (rc < 0) {
	    pr_err("%s i2c write 0x6C failed (%d)\n", __func__, rc);
    }

    rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_write(&a_ctrl->i2c_client, 0x76, data_0x76, MSM_CAMERA_I2C_WORD_DATA);
    if (rc < 0) {
	    pr_err("%s i2c write 0x76 failed (%d)\n", __func__, rc);
    }

//=====
//Restore
//OFFSET (3Ah)
//ms22e (6Eh)
//MSSET (8Fh)
//SWTCH (83h)
//EQENBL (87h)
//ANA2 (A4h)

    rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_write(&a_ctrl->i2c_client, 0x3a, data_0x3a, MSM_CAMERA_I2C_WORD_DATA);
    if (rc < 0) {
	    pr_err("%s i2c write 0x3a failed (%d)\n", __func__, rc);
    }

    rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_write(&a_ctrl->i2c_client, 0x3e, data_0x6e, MSM_CAMERA_I2C_WORD_DATA);
    if (rc < 0) {
	    pr_err("%s i2c write 0x3e failed (%d)\n", __func__, rc);
    }

    rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_write(&a_ctrl->i2c_client, 0x8f, data_0x8f, MSM_CAMERA_I2C_BYTE_DATA);
    if (rc < 0) {
	    pr_err("%s i2c write 0x8f failed (%d)\n", __func__, rc);
    }

    rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_write(&a_ctrl->i2c_client, 0x83, data_0x83, MSM_CAMERA_I2C_BYTE_DATA);
    if (rc < 0) {
	    pr_err("%s i2c write 0x83 failed (%d)\n", __func__, rc);
    }

    rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_write(&a_ctrl->i2c_client, 0x87, data_0x87, MSM_CAMERA_I2C_BYTE_DATA);
    if (rc < 0) {
	    pr_err("%s i2c write 0x87 failed (%d)\n", __func__, rc);
    }

    rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_write(&a_ctrl->i2c_client, 0xa4, data_0xa4, MSM_CAMERA_I2C_BYTE_DATA);
    if (rc < 0) {
	    pr_err("%s i2c write 0xa4 failed (%d)\n", __func__, rc);
    }

}
//HTC_END
static int32_t lc898212_wrapper_i2c_write(struct msm_actuator_ctrl_t *a_ctrl,
    int16_t next_lens_position, int8_t dir)
{
    int32_t rc = 0;

    //if(a_ctrl->enable_focus_step_log)
        CDBG("%s next_lens_position: %x (%d)\n", __func__, next_lens_position, (int16_t)next_lens_position);

    CDBG("%s addr_type: %d, sid 0x%x (0x%x), \n", __func__, a_ctrl->i2c_client.addr_type, a_ctrl->i2c_client.cci_client->sid, (a_ctrl->i2c_client.cci_client->sid<<1));


    rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_write(&a_ctrl->i2c_client,
            0xa1,
            next_lens_position,
            MSM_CAMERA_I2C_WORD_DATA);
    if (rc < 0) {
        pr_err("%s 0xa1 i2c write failed (%d)\n", __func__, rc);
        return rc;
    }

    rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_write(&a_ctrl->i2c_client,
         0x16,
         (dir > 0) ? 0xfe80 : 0x180,
         MSM_CAMERA_I2C_WORD_DATA);
    /*
    infinity -> macro : dir (1)   0xfe80
    macro -> infinity : dir (-1)  0x180
    */
    if (rc < 0) {
        pr_err("%s 0x16 i2c write failed (%d)\n", __func__, rc);
        return rc;
    }

    rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_write(&a_ctrl->i2c_client,
         0x8a,
         5,
         MSM_CAMERA_I2C_BYTE_DATA);
    if (rc < 0) {
         pr_err("%s 0x8a i2c write failed (%d)\n", __func__, rc);
         return rc;
    }

    return rc;
}

static int32_t lc898212_act_init_focus(struct msm_actuator_ctrl_t *a_ctrl,
    uint16_t size, enum msm_actuator_data_type type,
    struct reg_settings_t *settings)
{
    int32_t rc = 0;
    uint16_t data=0;
    uint16_t step=0;
    uint8_t bias = DEFAULT_BIAS;
    uint8_t offset = DEFAULT_OFFSET;
    uint16_t infinity = DEFAULT_INFINITY;
    struct msm_camera_i2c_reg_setting lc898212_settings = {
        .addr_type = MSM_CAMERA_I2C_BYTE_ADDR,
        .data_type = MSM_CAMERA_I2C_BYTE_DATA,
        .delay = 0,
        .cmd_type = MSM_CAMERA_I2C_COMMAND_WRITE,
    };

    if (a_ctrl->af_OTP_info.VCM_OTP_Read) {
        bias = a_ctrl->af_OTP_info.VCM_Bias;
        offset = a_ctrl->af_OTP_info.VCM_Offset;
        infinity = a_ctrl->af_OTP_info.VCM_Infinity;
    }

    lc898212_settings.reg_setting = lc898212_settings_1;
    lc898212_settings.size = ARRAY_SIZE(lc898212_settings_1),
    rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_write_table(&a_ctrl->i2c_client, &lc898212_settings);
    if (rc < 0) {
        pr_err("%s: lc898212_settings_1 i2c write failed (%d)\n", __func__, rc);
        return rc;
    }
    mdelay(1);
    CDBG("%s: lc898212_settings_1 i2c write done (%d)\n", __func__, rc);

    switch (a_ctrl->af_OTP_info.VCM_Vendor_Id_Version) {
        case 0x11:
            lc898212_settings.reg_setting = lc898212_settings_2_default;
            lc898212_settings.size = ARRAY_SIZE(lc898212_settings_2_default),
            rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_write_table(&a_ctrl->i2c_client, &lc898212_settings);
            break;
        case 0x12:
            lc898212_settings.reg_setting = lc898212_settings_2_0x12;
            lc898212_settings.size = ARRAY_SIZE(lc898212_settings_2_0x12),
            rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_write_table(&a_ctrl->i2c_client, &lc898212_settings);
            break;
        case 0x13:
            lc898212_settings.reg_setting = lc898212_settings_2_0x13;
            lc898212_settings.size = ARRAY_SIZE(lc898212_settings_2_0x13),
            rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_write_table(&a_ctrl->i2c_client, &lc898212_settings);
            break;
        // give it a try
        default:/* The default setting was set to the normal case table to prevent from abnormal behavior while reading otp fail. There is no failing otp data in VCM_Vendor_Id_Version currently.*/
            lc898212_settings.reg_setting = lc898212_settings_2_default;
            lc898212_settings.size = ARRAY_SIZE(lc898212_settings_2_default),
            rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_write_table(&a_ctrl->i2c_client, &lc898212_settings);
            break;
        break;
    }

    if (rc < 0) {
        pr_err("%s: lc898212_settings_2 i2c write failed (%d)\n", __func__, rc);
        return rc;
    }
    pr_info("%s: lc898212_settings_2_%x i2c write done (%d)\n", __func__, a_ctrl->af_OTP_info.VCM_Vendor_Id_Version, rc);

    rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_write(&a_ctrl->i2c_client, 0x28, offset, MSM_CAMERA_I2C_BYTE_DATA); // HTC pg 20130319 lc898212 default value
    if (rc < 0) {
        pr_err("%s 0x28 i2c write failed (%d)\n", __func__, rc);
        return rc;
    }
    rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_write(&a_ctrl->i2c_client, 0x29, bias, MSM_CAMERA_I2C_BYTE_DATA); // HTC pg 20130319 lc898212 default value
    if (rc < 0) {
        pr_err("%s 0x29 i2c write failed (%d)\n", __func__, rc);
        return rc;
    }

    lc898212_settings.reg_setting = lc898212_settings_3;
    lc898212_settings.size = ARRAY_SIZE(lc898212_settings_3),
    rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_write_table(&a_ctrl->i2c_client, &lc898212_settings);
    if (rc < 0) {
        pr_err("%s: lc898212_settings_3 i2c write failed (%d)\n", __func__, rc);
        return rc;
    }
    mdelay(1);
    pr_err("%s: lc898212_settings_3 i2c write done (%d)\n", __func__, rc);

    // read current position
    rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_read(&a_ctrl->i2c_client, 0x3c, &data, MSM_CAMERA_I2C_WORD_DATA);
    if (rc < 0) {
        pr_err("%s: i2c read failed (%d)\n", __func__, rc);
        return rc;
    }
    // move it
    rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_write(&a_ctrl->i2c_client, 0x4, data, MSM_CAMERA_I2C_WORD_DATA);
    if (rc < 0) {
        pr_err("%s: 0x87 i2c write failed (%d)\n", __func__, rc);
        return rc;
    }

    rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_write(&a_ctrl->i2c_client, 0x18, data, MSM_CAMERA_I2C_WORD_DATA);
    if (rc < 0) {
        pr_err("%s: 0x18 i2c read failed (%d)\n", __func__, rc);
        return rc;
    }

    rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_write(&a_ctrl->i2c_client, 0x87, 0x85, MSM_CAMERA_I2C_BYTE_DATA);
    if (rc < 0) {
        pr_err("%s: 0x87 i2c write failed (%d)\n", __func__, rc);
        return rc;
    }

    lc898212_settings.reg_setting = lc898212_settings_4;
    lc898212_settings.size = ARRAY_SIZE(lc898212_settings_4),
    rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_write_table(&a_ctrl->i2c_client, &lc898212_settings);
    if (rc < 0) {
        pr_err("%s: lc898212_settings_4 i2c write failed (%d)\n", __func__, rc);
        return rc;
    }
    pr_err("%s: lc898212_settings_4 i2c write done (%d)\n", __func__, rc);

    // read current position
    rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_read(&a_ctrl->i2c_client, 0x3c, &data, MSM_CAMERA_I2C_WORD_DATA);
    if (rc < 0) {
        pr_err("%s: i2c read failed (%d)\n", __func__, rc);
        return rc;
    }

    rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_write(&a_ctrl->i2c_client, 0x18, data, MSM_CAMERA_I2C_WORD_DATA);
    if (rc < 0) {
        pr_err("%s: 0x18 i2c read failed (%d)\n", __func__, rc);
        return rc;
    }

    rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_write(&a_ctrl->i2c_client, 0xa1, infinity, MSM_CAMERA_I2C_WORD_DATA);
    if (rc < 0) {
        pr_err("%s 0xa1 i2c write failed (%d)\n", __func__, rc);
        return rc;
    }

    step = (signed short)infinity > (signed short)data ? 0x180 : 0xfe80;

    rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_write(&a_ctrl->i2c_client, 0x16, step, MSM_CAMERA_I2C_WORD_DATA);
    if (rc < 0) {
        pr_err("%s 0x16 i2c write failed (%d)\n", __func__, rc);
        return rc;
    }

    rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_write(&a_ctrl->i2c_client, 0x8a, 5, MSM_CAMERA_I2C_BYTE_DATA);
    if (rc < 0) {
        pr_err("%s 0x8a i2c write failed (%d)\n", __func__, rc);
        return rc;
    }

    return rc;

}
/*HTC_END Porting lc898212*/

/*HTC_START Harvey 20130701 - Set otp af value*/
static int32_t msm_actuator_set_af_value(struct msm_actuator_ctrl_t *a_ctrl, af_value_t af_value)
{
    #if 1 // For lc898212
        a_ctrl->af_OTP_info.VCM_OTP_Read = true;
	a_ctrl->af_OTP_info.VCM_Infinity = (af_value.AF_INF_MSB<<8) | af_value.AF_INF_LSB;
	a_ctrl->af_OTP_info.VCM_Bias = af_value.VCM_BIAS;
	a_ctrl->af_OTP_info.VCM_Offset = af_value.VCM_OFFSET;
	a_ctrl->af_OTP_info.VCM_Vendor_Id_Version = af_value.VCM_VENDOR_ID_VERSION;

	pr_info("%s: VCM_Infinity = 0x%x (%d)\n",       __func__, a_ctrl->af_OTP_info.VCM_Infinity,     (int16_t)a_ctrl->af_OTP_info.VCM_Infinity);
	pr_info("%s: VCM_Bias = 0x%x (%d)\n",           __func__, a_ctrl->af_OTP_info.VCM_Bias,         (int16_t)a_ctrl->af_OTP_info.VCM_Bias);
	pr_info("%s: VCM_Offset = 0x%x (%d)\n",         __func__, a_ctrl->af_OTP_info.VCM_Offset,       (int16_t)a_ctrl->af_OTP_info.VCM_Offset);
	pr_info("%s: VCM_Vendor_Id_Version = 0x%x (%d)\n", __func__, a_ctrl->af_OTP_info.VCM_Vendor_Id_Version, a_ctrl->af_OTP_info.VCM_Vendor_Id_Version);

        return 0;
    #else
	uint8_t i;
	int32_t rc = 0;

	strlcpy(a_ctrl->af_OTP_info.act_name, af_value.ACT_NAME, sizeof(a_ctrl->af_OTP_info.act_name));
	
	for(i=0; i< (sizeof(act_func)/sizeof(act_func_t)); i++) {
		if (!strcmp(a_ctrl->af_OTP_info.act_name, act_func[i].act_name)) {	
			rc = act_func[i].set_af_value(a_ctrl, af_value);
		}
	}
	return rc;
    #endif
}
/*HTC_END*/

int32_t msm_actuator_set_ois_mode(struct msm_actuator_ctrl_t *a_ctrl, int ois_mode)
{
	uint8_t i;
	int32_t rc = 0;

	for(i=0; i< (sizeof(act_func)/sizeof(act_func_t)); i++) {
		if (!strcmp(a_ctrl->af_OTP_info.act_name, act_func[i].act_name)) {	
			rc = act_func[i].set_ois_mode(a_ctrl, ois_mode);
		}
	}
	return rc;
}

int32_t msm_actuator_update_ois_tbl(struct msm_actuator_ctrl_t *a_ctrl, struct sensor_actuator_info_t * sensor_actuator_info)
{
	uint8_t i;
	int32_t rc = 0;

	for(i=0; i< (sizeof(act_func)/sizeof(act_func_t)); i++) {
		if (!strcmp(a_ctrl->af_OTP_info.act_name, act_func[i].act_name)) {
			rc = act_func[i].update_ois_tbl(a_ctrl, sensor_actuator_info);
		}
	}
	return rc;
}
/*HTC_END*/

/*HTC Harvey 20130704 - Separate code by different actuator module and dynamic launch*/
int32_t msm_actuator_piezo_set_default_focus(
	struct msm_actuator_ctrl_t *a_ctrl,
	struct msm_actuator_move_params_t *move_params)
{
	int32_t rc = 0;
	struct msm_camera_i2c_reg_setting reg_setting;
	CDBG("Enter\n");

	if (a_ctrl->curr_step_pos != 0) {
		a_ctrl->i2c_tbl_index = 0;
		a_ctrl->func_tbl->actuator_parse_i2c_params(a_ctrl,
			a_ctrl->initial_code, 0, 0);
		a_ctrl->func_tbl->actuator_parse_i2c_params(a_ctrl,
			a_ctrl->initial_code, 0, 0);
		reg_setting.reg_setting = a_ctrl->i2c_reg_tbl;
		reg_setting.data_type = a_ctrl->i2c_data_type;
		reg_setting.size = a_ctrl->i2c_tbl_index;
		rc = a_ctrl->i2c_client.i2c_func_tbl->
			i2c_write_table_w_microdelay(
			&a_ctrl->i2c_client, &reg_setting);
		if (rc < 0) {
			pr_err("%s: i2c write error:%d\n",
				__func__, rc);
			return rc;
		}
		a_ctrl->i2c_tbl_index = 0;
		a_ctrl->curr_step_pos = 0;
	}
	CDBG("Exit\n");
	return rc;
}

static void msm_actuator_parse_i2c_params(struct msm_actuator_ctrl_t *a_ctrl,
	int16_t next_lens_position, uint32_t hw_params, uint16_t delay)
{
	struct msm_actuator_reg_params_t *write_arr = a_ctrl->reg_tbl;
	uint32_t hw_dword = hw_params;
	uint16_t i2c_byte1 = 0, i2c_byte2 = 0;
	uint16_t value = 0;
	uint32_t size = a_ctrl->reg_tbl_size, i = 0;
	struct msm_camera_i2c_reg_array *i2c_tbl = a_ctrl->i2c_reg_tbl;

/*HTC_START, support multiple I2C parser type for actuator modulation*/
#if 1
    struct msm_camera_i2c_seq_reg_setting *i2c_seq_setting = &a_ctrl->i2c_seq_reg_setting;
    if(a_ctrl->act_i2c_select == WRITE_SEQ_TABLE)
    {
        CDBG("Enter\n");
        for (i = 0; i < size; i++) {
		value = (next_lens_position <<
			write_arr[i].data_shift) |
			((hw_dword & write_arr[i].hw_mask) >>
			write_arr[i].hw_shift);
		CDBG("value=(%d << %d) | ((0x%x & 0x%x) >> %d)=0x%x\n", next_lens_position, write_arr[i].data_shift, hw_dword, write_arr[i].hw_mask, write_arr[i].hw_shift, value);

		CDBG("sizeof reg_data %d\n", sizeof(i2c_seq_setting->reg_setting[a_ctrl->i2c_tbl_index].reg_data));
		memset(i2c_seq_setting->reg_setting[a_ctrl->i2c_tbl_index].reg_data, 0, sizeof(i2c_seq_setting->reg_setting[a_ctrl->i2c_tbl_index].reg_data));
		i2c_seq_setting->reg_setting[a_ctrl->i2c_tbl_index].reg_addr = write_arr[i].reg_addr;
		i2c_seq_setting->reg_setting[a_ctrl->i2c_tbl_index].reg_data[0] = (value & 0xFF00) >> 8;
		i2c_seq_setting->reg_setting[a_ctrl->i2c_tbl_index].reg_data[1] = value & 0xFF;
		i2c_seq_setting->reg_setting[a_ctrl->i2c_tbl_index].reg_data_size = 8;
		a_ctrl->i2c_tbl_index++;
		i2c_seq_setting->size = a_ctrl->i2c_tbl_index;
		i2c_seq_setting->delay = delay;
		CDBG("[%d] : [%d] 0x%x; 0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x; %d; %d\n", i, a_ctrl->i2c_tbl_index-1,
			i2c_seq_setting->reg_setting[a_ctrl->i2c_tbl_index-1].reg_addr,
			i2c_seq_setting->reg_setting[a_ctrl->i2c_tbl_index-1].reg_data[0],
			i2c_seq_setting->reg_setting[a_ctrl->i2c_tbl_index-1].reg_data[1],
			i2c_seq_setting->reg_setting[a_ctrl->i2c_tbl_index-1].reg_data[2],
			i2c_seq_setting->reg_setting[a_ctrl->i2c_tbl_index-1].reg_data[3],
			i2c_seq_setting->reg_setting[a_ctrl->i2c_tbl_index-1].reg_data[4],
			i2c_seq_setting->reg_setting[a_ctrl->i2c_tbl_index-1].reg_data[5],
			i2c_seq_setting->reg_setting[a_ctrl->i2c_tbl_index-1].reg_data[6],
			i2c_seq_setting->reg_setting[a_ctrl->i2c_tbl_index-1].reg_data[7],
			i2c_seq_setting->size, i2c_seq_setting->delay);
        }
    }
    else
    {
#endif
/*HTC_END*/
	CDBG("Enter\n");
	for (i = 0; i < size; i++) {
		/* check that the index into i2c_tbl cannot grow larger that
		the allocated size of i2c_tbl */
		if ((a_ctrl->total_steps + 1) < (a_ctrl->i2c_tbl_index)) {
			break;
		}
		if (write_arr[i].reg_write_type == MSM_ACTUATOR_WRITE_DAC) {
			value = (next_lens_position <<
				write_arr[i].data_shift) |
				((hw_dword & write_arr[i].hw_mask) >>
				write_arr[i].hw_shift);

			if (write_arr[i].reg_addr != 0xFFFF) {
				i2c_byte1 = write_arr[i].reg_addr;
				i2c_byte2 = value;
				if (size != (i+1)) {
					i2c_byte2 = value & 0xFF;
					CDBG("byte1:0x%x, byte2:0x%x\n",
						i2c_byte1, i2c_byte2);
					/*HTC_START, Swap the MSB and LSB order*/
					#if 1
					i2c_tbl[size-a_ctrl->i2c_tbl_index-1].
						reg_addr = i2c_byte1;
					i2c_tbl[size-a_ctrl->i2c_tbl_index-1].
						reg_data = i2c_byte2;
					i2c_tbl[size-a_ctrl->i2c_tbl_index-1].
						delay = 0;
					#else
					i2c_tbl[a_ctrl->i2c_tbl_index].
						reg_addr = i2c_byte1;
					i2c_tbl[a_ctrl->i2c_tbl_index].
						reg_data = i2c_byte2;
					i2c_tbl[a_ctrl->i2c_tbl_index].
						delay = 0;
					#endif
                                        /*HTC_END*/
					a_ctrl->i2c_tbl_index++;
					i++;
					i2c_byte1 = write_arr[i].reg_addr;
					i2c_byte2 = (value & 0xFF00) >> 8;
				}
			} else {
				i2c_byte1 = (value & 0xFF00) >> 8;
				i2c_byte2 = value & 0xFF;
			}
		} else {
			i2c_byte1 = write_arr[i].reg_addr;
			i2c_byte2 = (hw_dword & write_arr[i].hw_mask) >>
				write_arr[i].hw_shift;
		}
		CDBG("i2c_byte1:0x%x, i2c_byte2:0x%x\n", i2c_byte1, i2c_byte2);
		/*HTC_START, Swap the MSB and LSB order*/
		#if 1
		i2c_tbl[size-a_ctrl->i2c_tbl_index-1].reg_addr = i2c_byte1;
		i2c_tbl[size-a_ctrl->i2c_tbl_index-1].reg_data = i2c_byte2;
		i2c_tbl[size-a_ctrl->i2c_tbl_index-1].delay = delay;
		#else
		i2c_tbl[a_ctrl->i2c_tbl_index].reg_addr = i2c_byte1;
		i2c_tbl[a_ctrl->i2c_tbl_index].reg_data = i2c_byte2;
		i2c_tbl[a_ctrl->i2c_tbl_index].delay = delay;
		#endif
                /*HTC_END*/
		a_ctrl->i2c_tbl_index++;
	}
/*HTC_START, support multiple I2C parser type for actuator modulation*/
#if 1
    }
#endif
/*HTC_END*/
	CDBG("Exit\n");
}

/*HTC Harvey 20130704 - Separate code by different actuator module and dynamic launch*/
int32_t msm_actuator_init_focus(struct msm_actuator_ctrl_t *a_ctrl,
	uint16_t size, enum msm_actuator_data_type type,
	struct reg_settings_t *settings)
{
	int32_t rc = -EFAULT;
	int32_t i = 0;
	CDBG("Enter\n");

	for (i = 0; i < size; i++) {
		switch (type) {
		case MSM_ACTUATOR_BYTE_DATA:
			rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_write(
				&a_ctrl->i2c_client,
				settings[i].reg_addr,
				settings[i].reg_data, MSM_CAMERA_I2C_BYTE_DATA);
			break;
		case MSM_ACTUATOR_WORD_DATA:
			rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_write(
				&a_ctrl->i2c_client,
				settings[i].reg_addr,
				settings[i].reg_data, MSM_CAMERA_I2C_WORD_DATA);
			break;
		default:
			pr_err("Unsupport data type: %d\n", type);
			break;
		}
		if (rc < 0)
			break;
	}

	a_ctrl->curr_step_pos = 0;
	CDBG("Exit\n");
	return rc;
}

static void msm_actuator_write_focus(
	struct msm_actuator_ctrl_t *a_ctrl,
	uint16_t curr_lens_pos,
	struct damping_params_t *damping_params,
	int8_t sign_direction,
	int16_t code_boundary)
{
/*HTC_START, remove redundant move step*/
#if 0
	int16_t next_lens_pos = 0;
#endif
/*HTC_END*/
	uint16_t damping_code_step = 0;
	uint16_t wait_time = 0;
	CDBG("Enter\n");

	damping_code_step = damping_params->damping_step;
	wait_time = damping_params->damping_delay;

/*HTC_START, remove redundant move step*/
#if 0
	/* Write code based on damping_code_step in a loop */
	for (next_lens_pos =
		curr_lens_pos + (sign_direction * damping_code_step);
		(sign_direction * next_lens_pos) <=
			(sign_direction * code_boundary);
		next_lens_pos =
			(next_lens_pos +
				(sign_direction * damping_code_step))) {
		a_ctrl->func_tbl->actuator_parse_i2c_params(a_ctrl,
			next_lens_pos, damping_params->hw_params, wait_time);
		curr_lens_pos = next_lens_pos;
	}
#endif
/*HTC_END*/

	if (curr_lens_pos != code_boundary) {
            /*HTC_START Porting lc898212*/
            if(a_ctrl->act_i2c_select == WRITE_MULTI_TABLE)
                lc898212_wrapper_i2c_write(a_ctrl, code_boundary, sign_direction);
            else
            /*HTC_END Porting lc898212*/
		a_ctrl->func_tbl->actuator_parse_i2c_params(a_ctrl,
			code_boundary, damping_params->hw_params, wait_time);
	}
	CDBG("Exit\n");
}

/*HTC Harvey 20130704 - Separate code by different actuator module and dynamic launch*/
int32_t msm_actuator_piezo_move_focus(
	struct msm_actuator_ctrl_t *a_ctrl,
	struct msm_actuator_move_params_t *move_params)
{
	int32_t dest_step_position = move_params->dest_step_pos;
	struct damping_params_t ringing_params_kernel;
	int32_t rc = 0;
	int32_t num_steps = move_params->num_steps;
	struct msm_camera_i2c_reg_setting reg_setting;
	CDBG("Enter\n");

	if (copy_from_user(&ringing_params_kernel,
		&(move_params->ringing_params[0]),
		sizeof(struct damping_params_t))) {
		pr_err("copy_from_user failed\n");
		return -EFAULT;
	}

	if (num_steps == 0)
		return rc;

	a_ctrl->i2c_tbl_index = 0;
	a_ctrl->func_tbl->actuator_parse_i2c_params(a_ctrl,
		(num_steps *
		a_ctrl->region_params[0].code_per_step),
		ringing_params_kernel.hw_params, 0);

	reg_setting.reg_setting = a_ctrl->i2c_reg_tbl;
	reg_setting.data_type = a_ctrl->i2c_data_type;
	reg_setting.size = a_ctrl->i2c_tbl_index;
	rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_write_table_w_microdelay(
		&a_ctrl->i2c_client, &reg_setting);
	if (rc < 0) {
		pr_err("i2c write error:%d\n", rc);
		return rc;
	}
	a_ctrl->i2c_tbl_index = 0;
	a_ctrl->curr_step_pos = dest_step_position;
	CDBG("Exit\n");
	return rc;
}

static int32_t msm_actuator_move_focus(
	struct msm_actuator_ctrl_t *a_ctrl,
	struct msm_actuator_move_params_t *move_params)
{
	int32_t rc = 0;
	struct damping_params_t ringing_params_kernel;
	int8_t sign_dir = move_params->sign_dir;
	uint16_t step_boundary = 0;
	uint16_t target_step_pos = 0;
	uint16_t target_lens_pos = 0;
	int16_t dest_step_pos = move_params->dest_step_pos;
	uint16_t curr_lens_pos = 0;
	int dir = move_params->dir;
	int32_t num_steps = move_params->num_steps;
	struct msm_camera_i2c_reg_setting reg_setting;

	curr_lens_pos = a_ctrl->step_position_table[a_ctrl->curr_step_pos];
	move_params->curr_lens_pos = curr_lens_pos;

	if (copy_from_user(&ringing_params_kernel,
		&(move_params->ringing_params[a_ctrl->curr_region_index]),
		sizeof(struct damping_params_t))) {
		pr_err("copy_from_user failed\n");
		return -EFAULT;
	}


	CDBG("called, dir %d, num_steps %d\n", dir, num_steps);

	if (dest_step_pos == a_ctrl->curr_step_pos)
		return rc;

	a_ctrl->i2c_tbl_index = 0;
	CDBG("curr_step_pos =%d dest_step_pos =%d curr_lens_pos=%d\n",
		a_ctrl->curr_step_pos, dest_step_pos, curr_lens_pos);
    /*HTC_START*/
    #ifdef STORE_OTP_INF
    if(g_infinity_pos <= dest_step_pos)   //add for 3 step calibration
    {
        pr_info("%s move focus for dual camera calibration, dest_step_pos=%d\n", __func__, dest_step_pos);
        target_step_pos = dest_step_pos;
        target_lens_pos =
				a_ctrl->step_position_table[target_step_pos];
		CDBG("target_step_pos %d, target_lens_pos %d\n", target_step_pos, target_lens_pos); /*HTC_ADD*/
		a_ctrl->func_tbl->actuator_write_focus(a_ctrl,
					curr_lens_pos,
					&(move_params->
						ringing_params[a_ctrl->
						curr_region_index]),
					sign_dir,
					target_lens_pos);
		a_ctrl->curr_step_pos = dest_step_pos;
    }
    else
    {
    #endif
    /*HTC_END*/
	while (a_ctrl->curr_step_pos != dest_step_pos) {
		step_boundary =
			a_ctrl->region_params[a_ctrl->curr_region_index].
			step_bound[dir];
                CDBG("curr_region_index %d, dest_step_pos %d, step_boundary %d, sign_dir %d\n", a_ctrl->curr_region_index, dest_step_pos, step_boundary, sign_dir); /*HTC_ADD*/
		if ((dest_step_pos * sign_dir) <=
			(step_boundary * sign_dir)) {

			target_step_pos = dest_step_pos;
			target_lens_pos =
				a_ctrl->step_position_table[target_step_pos];
                        CDBG("target_step_pos %d, target_lens_pos %d\n", target_step_pos, target_lens_pos); /*HTC_ADD*/
			a_ctrl->func_tbl->actuator_write_focus(a_ctrl,
					curr_lens_pos,
					&ringing_params_kernel,
					sign_dir,
					target_lens_pos);
			curr_lens_pos = target_lens_pos;

		} else {
			target_step_pos = step_boundary;
			target_lens_pos =
				a_ctrl->step_position_table[target_step_pos];
                        CDBG("target_step_pos %d, target_lens_pos %d\n", target_step_pos, target_lens_pos); /*HTC_ADD*/
			a_ctrl->func_tbl->actuator_write_focus(a_ctrl,
					curr_lens_pos,
					&ringing_params_kernel,
					sign_dir,
					target_lens_pos);
			curr_lens_pos = target_lens_pos;

			a_ctrl->curr_region_index += sign_dir;
                        CDBG("curr_step_pos =%d, curr_lens_pos=%d\n",a_ctrl->curr_step_pos, curr_lens_pos); /*HTC_ADD*/
		}
		a_ctrl->curr_step_pos = target_step_pos;
	}
    /*HTC_START*/
    #ifdef STORE_OTP_INF
    }
    #endif
    /*HTC_END*/
        CDBG("sid = 0x%x (0x%x)\n",a_ctrl->i2c_client.cci_client->sid, a_ctrl->i2c_client.cci_client->sid<<1); /*HTC_ADD*/

/*HTC_START, support multiple I2C access type for actuator modulation*/
    if(a_ctrl->act_i2c_select == WRITE_MULTI_TABLE) //For lc898212
    {}
    else if(a_ctrl->act_i2c_select == WRITE_SEQ_TABLE)
            rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_write_seq_table(
		&a_ctrl->i2c_client,
		&a_ctrl->i2c_seq_reg_setting);
	else
	{
/*HTC_END*/

	move_params->curr_lens_pos = curr_lens_pos;
	reg_setting.reg_setting = a_ctrl->i2c_reg_tbl;
	reg_setting.data_type = a_ctrl->i2c_data_type;
	reg_setting.size = a_ctrl->i2c_tbl_index;
	rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_write_table_w_microdelay(
		&a_ctrl->i2c_client, &reg_setting);
/*HTC_START, support multiple I2C access type for actuator modulation*/
	}
/*HTC_END*/
	if (rc < 0) {
		pr_err("i2c write error:%d\n", rc);
		return rc;
	}
	a_ctrl->i2c_tbl_index = 0;
	CDBG("Exit\n");

	return rc;
}

static int32_t msm_actuator_init_step_table(struct msm_actuator_ctrl_t *a_ctrl,
	struct msm_actuator_set_info_t *set_info)
{
/*HTC_START, Get step position table from user space for actuator modulation*/
#if 1
    int i = 0;
    CDBG("Enter\n");

    if (a_ctrl->step_position_table != NULL)
      kfree(a_ctrl->step_position_table);
    a_ctrl->step_position_table = NULL;

    /* Fill step position table */
    /*HTC_START*/
    #ifdef STORE_OTP_INF //Recrod original VCM infinity 
    a_ctrl->step_position_table =
    kmalloc(sizeof(uint16_t) *(set_info->af_tuning_params.total_steps + 5), GFP_KERNEL);   //add for 3 step calibration
    g_infinity_pos = set_info->af_tuning_params.total_steps + 1;
    pr_info("g_infinity_pos: %d\n", g_infinity_pos);
    #else
    a_ctrl->step_position_table =
    kmalloc(sizeof(uint16_t) *(set_info->af_tuning_params.total_steps + 1), GFP_KERNEL);
    #endif
    /*HTC_END*/

    if (a_ctrl->step_position_table == NULL)
    {
        pr_err("a_ctrl->step_position_table create fail\n");
        return -ENOMEM;
    }

    if (set_info->step_position_table == NULL)
    {
        pr_err("set_info->step_position_table NULL\n");
        return -ENOMEM;
    }

    /*HTC_START*/
    #ifdef STORE_OTP_INF
    for (i = 0; i <= a_ctrl->total_steps + 4; i++)   //add for 3 step calibration
    #else
    for (i = 0; i <= a_ctrl->total_steps; i++)
    #endif
    /*HTC_END*/
    {
        a_ctrl->step_position_table[i] = set_info->step_position_table[i];
        CDBG("step_position_table(%d):%d;",i, a_ctrl->step_position_table[i]);
    }
#else
	int16_t code_per_step = 0;
	int16_t cur_code = 0;
	int16_t step_index = 0, region_index = 0;
	uint16_t step_boundary = 0;
	uint32_t max_code_size = 1;
	uint16_t data_size = set_info->actuator_params.data_size;
	CDBG("Enter\n");

	for (; data_size > 0; data_size--)
		max_code_size *= 2;

	kfree(a_ctrl->step_position_table);
	a_ctrl->step_position_table = NULL;

	/* Fill step position table */
	a_ctrl->step_position_table =
		kmalloc(sizeof(uint16_t) *
		(set_info->af_tuning_params.total_steps + 1), GFP_KERNEL);

	if (a_ctrl->step_position_table == NULL)
		return -ENOMEM;

	cur_code = set_info->af_tuning_params.initial_code;
	a_ctrl->step_position_table[step_index++] = cur_code;
	for (region_index = 0;
		region_index < a_ctrl->region_size;
		region_index++) {
		code_per_step =
			a_ctrl->region_params[region_index].code_per_step;
		step_boundary =
			a_ctrl->region_params[region_index].
			step_bound[MOVE_NEAR];
		for (; step_index <= step_boundary;
			step_index++) {
			cur_code += code_per_step;
			if (cur_code < max_code_size)
				a_ctrl->step_position_table[step_index] =
					cur_code;
			else {
				for (; step_index <
					set_info->af_tuning_params.total_steps;
					step_index++)
					a_ctrl->
						step_position_table[
						step_index] =
						max_code_size;
			}
		}
	}
#endif
/*HTC_END*/
	CDBG("Exit\n");
	return 0;
}

/*HTC Harvey 20130704 - Separate code by different actuator module and dynamic launch*/
int32_t msm_actuator_set_default_focus(
	struct msm_actuator_ctrl_t *a_ctrl,
	struct msm_actuator_move_params_t *move_params)
{
	int32_t rc = 0;
	CDBG("Enter\n");

	if (a_ctrl->curr_step_pos != 0)
		rc = a_ctrl->func_tbl->actuator_move_focus(a_ctrl, move_params);
	CDBG("Exit\n");
	return rc;
}

static int32_t msm_actuator_power_down(struct msm_actuator_ctrl_t *a_ctrl)
{
	int32_t rc = 0;
	CDBG("Enter\n");
	if (a_ctrl->vcm_enable) {
		rc = gpio_direction_output(a_ctrl->vcm_pwd, 0);
		if (!rc)
			gpio_free(a_ctrl->vcm_pwd);
	}

	kfree(a_ctrl->step_position_table);
	a_ctrl->step_position_table = NULL;
	kfree(a_ctrl->i2c_reg_tbl);
	a_ctrl->i2c_reg_tbl = NULL;
/*HTC_START*/
	kfree(a_ctrl->i2c_seq_reg_setting.reg_setting);
	a_ctrl->i2c_seq_reg_setting.reg_setting = NULL;
/*HTC_END*/
	a_ctrl->i2c_tbl_index = 0;
	CDBG("Exit\n");
	return rc;
}

static int32_t msm_actuator_init(struct msm_actuator_ctrl_t *a_ctrl,
	struct msm_actuator_set_info_t *set_info) {
	struct reg_settings_t *init_settings = NULL;
	int32_t rc = -EFAULT;
	uint16_t i = 0;
	struct msm_camera_cci_client *cci_client = NULL;
	CDBG("Enter\n");

/*HTC_START*/
	CDBG("Enter set_info act_type %d\n", set_info->actuator_params.act_type);
/*HTC_END*/
	for (i = 0; i < ARRAY_SIZE(actuators); i++) {
		if (set_info->actuator_params.act_type ==
			actuators[i]->act_type) {
			a_ctrl->func_tbl = &actuators[i]->func_tbl;
			rc = 0;
		}
	}

	if (rc < 0) {
		pr_err("Actuator function table not found\n");
		return rc;
	}

	a_ctrl->region_size = set_info->af_tuning_params.region_size;
	if (a_ctrl->region_size > MAX_ACTUATOR_REGION) {
		pr_err("MAX_ACTUATOR_REGION is exceeded.\n");
		return -EFAULT;
	}
	a_ctrl->pwd_step = set_info->af_tuning_params.pwd_step;
	a_ctrl->total_steps = set_info->af_tuning_params.total_steps;

	if (copy_from_user(&a_ctrl->region_params,
		(void *)set_info->af_tuning_params.region_params,
		a_ctrl->region_size * sizeof(struct region_params_t)))
		return -EFAULT;

	if (a_ctrl->act_device_type == MSM_CAMERA_PLATFORM_DEVICE) {
		cci_client = a_ctrl->i2c_client.cci_client;
		cci_client->sid =
			set_info->actuator_params.i2c_addr >> 1;
		cci_client->retries = 3;
		cci_client->id_map = 0;
		cci_client->cci_i2c_master = a_ctrl->cci_master;
	} else {
		a_ctrl->i2c_client.client->addr =
			set_info->actuator_params.i2c_addr;
	}

	a_ctrl->i2c_data_type = set_info->actuator_params.i2c_data_type;
	a_ctrl->i2c_client.addr_type = set_info->actuator_params.i2c_addr_type;
/*HTC_START*/
	a_ctrl->i2c_seq_reg_setting.addr_type = set_info->actuator_params.i2c_addr_type;
	a_ctrl->act_i2c_select = set_info->act_i2c_select; /*support multiple I2C access type for actuator modulation*/
	CDBG("act_i2c_select : %d\n",a_ctrl->act_i2c_select);
/*HTC_END*/
	if (set_info->actuator_params.reg_tbl_size <=
		MAX_ACTUATOR_REG_TBL_SIZE) {
		a_ctrl->reg_tbl_size = set_info->actuator_params.reg_tbl_size;
	} else {
		a_ctrl->reg_tbl_size = 0;
		pr_err("MAX_ACTUATOR_REG_TBL_SIZE is exceeded.\n");
		return -EFAULT;
	}

	kfree(a_ctrl->i2c_reg_tbl);
/*HTC_START*/
	kfree(a_ctrl->i2c_seq_reg_setting.reg_setting);
/*HTC_END*/

/*HTC_START Harvey 20130704 - Separate code by different actuator module and dynamic launch*/
	a_ctrl->i2c_seq_reg_setting.reg_setting =
		kmalloc(sizeof(struct msm_camera_i2c_seq_reg_array) *
		(set_info->af_tuning_params.total_steps + 1) * a_ctrl->reg_tbl_size, GFP_KERNEL);
	if (!a_ctrl->i2c_seq_reg_setting.reg_setting) {
		pr_err("kmalloc fail\n");
		return -ENOMEM;
	}

	a_ctrl->i2c_reg_tbl =
		kmalloc(sizeof(struct msm_camera_i2c_reg_array) *
		(set_info->af_tuning_params.total_steps + 1) * a_ctrl->reg_tbl_size, GFP_KERNEL);
	if (!a_ctrl->i2c_reg_tbl) {
		pr_err("kmalloc fail\n");
		return -ENOMEM;
	}
/*HTC_END*/

	if (copy_from_user(&a_ctrl->reg_tbl,
		(void *)set_info->actuator_params.reg_tbl_params,
		a_ctrl->reg_tbl_size *
		sizeof(struct msm_actuator_reg_params_t))) {
		kfree(a_ctrl->i2c_reg_tbl);
	/*HTC_START*/
		kfree(a_ctrl->i2c_seq_reg_setting.reg_setting);
		/*To avoid double free*/
		a_ctrl->i2c_reg_tbl = NULL;
		a_ctrl->i2c_seq_reg_setting.reg_setting = NULL;
	/*HTC_END*/
		return -EFAULT;
	}

/*HTC_START Porting lc898212*/
    if(a_ctrl->act_i2c_select == WRITE_MULTI_TABLE)
    {
        rc = lc898212_act_init_focus(a_ctrl,
				set_info->actuator_params.init_setting_size,
				a_ctrl->i2c_data_type,
				init_settings);
    }
    else
/*HTC_END Porting lc898212*/
	if (set_info->actuator_params.init_setting_size) {
		if (a_ctrl->func_tbl->actuator_init_focus) {
			init_settings = kmalloc(sizeof(struct reg_settings_t) *
				(set_info->actuator_params.init_setting_size),
				GFP_KERNEL);
			if (init_settings == NULL) {
				kfree(a_ctrl->i2c_reg_tbl);
			/*HTC_START*/
				kfree(a_ctrl->i2c_seq_reg_setting.reg_setting);
				/*To avoid double free*/
				a_ctrl->i2c_reg_tbl = NULL;
				a_ctrl->i2c_seq_reg_setting.reg_setting = NULL;
			/*HTC_END*/
				pr_err("Error allocating memory for init_settings\n");
				return -EFAULT;
			}
			if (copy_from_user(init_settings,
				(void *)set_info->actuator_params.init_settings,
				set_info->actuator_params.init_setting_size *
				sizeof(struct reg_settings_t))) {
				kfree(init_settings);
				kfree(a_ctrl->i2c_reg_tbl);
			/*HTC_START*/
				kfree(a_ctrl->i2c_seq_reg_setting.reg_setting);
				/*To avoid double free*/
				a_ctrl->i2c_reg_tbl = NULL;
				a_ctrl->i2c_seq_reg_setting.reg_setting = NULL;
			/*HTC_END*/
				pr_err("Error copying init_settings\n");
				return -EFAULT;
			}
			rc = a_ctrl->func_tbl->actuator_init_focus(a_ctrl,
				set_info->actuator_params.init_setting_size,
				a_ctrl->i2c_data_type,
				init_settings);
			kfree(init_settings);
			if (rc < 0) {
				kfree(a_ctrl->i2c_reg_tbl);
			/*HTC_START*/
				kfree(a_ctrl->i2c_seq_reg_setting.reg_setting);
				/*To avoid double free*/
				a_ctrl->i2c_reg_tbl = NULL;
				a_ctrl->i2c_seq_reg_setting.reg_setting = NULL;
			/*HTC_END*/
				pr_err("Error actuator_init_focus\n");
				return -EFAULT;
			}
		}
	}

	a_ctrl->initial_code = set_info->af_tuning_params.initial_code;
	if (a_ctrl->func_tbl->actuator_init_step_table)
		rc = a_ctrl->func_tbl->
			actuator_init_step_table(a_ctrl, set_info);

	a_ctrl->curr_step_pos = 0;
	a_ctrl->curr_region_index = 0;
	
/*HTC_START Harvey 20130628 - Porting OIS*/
#if (defined(CONFIG_TI201_ACT) && defined(CONFIG_ACT_OIS_BINDER))
	if ((a_ctrl->oisbinder_open_init) && (a_ctrl->ois_state != ACTUATOR_OIS_OPEN_INIT)) { /*HTC Harvey 20130725 - Avoid open init OIS twice*/
		a_ctrl->oisbinder_open_init();
		a_ctrl->ois_state = ACTUATOR_OIS_OPEN_INIT;
	}
#endif
/*HTC_END*/

	CDBG("Exit\n");

	return rc;
}

/*HTC_START Harvey 20130628 - Porting OIS*/
static int msm_actuator_stop(struct msm_actuator_ctrl_t *a_ctrl) {
	int rc = 0;
	CDBG("Enter\n");
	if (!a_ctrl) {
		pr_err("failed\n");
		return -EINVAL;
	}
	
#if (defined(CONFIG_TI201_ACT) && defined(CONFIG_ACT_OIS_BINDER))
	if ((a_ctrl->oisbinder_power_down) && (a_ctrl->ois_state == ACTUATOR_OIS_OPEN_INIT)) {
		a_ctrl->oisbinder_power_down();
		a_ctrl->ois_state = ACTUATOR_OIS_POWER_DOWN;
	}
#endif

	CDBG("Exit\n");
	return rc;
}
/*HTC_END*/

/*HTC_START, move damping info to user space for actuator modulation*/
#if 0
/*HTC_START Harvey 20130704 - Separate code by different actuator module and dynamic launch*/
static void msm_actuator_get_ext_info(struct msm_actuator_ctrl_t *a_ctrl)
{
	uint8_t i,j;

	for(i=0; i< (sizeof(act_func)/sizeof(act_func_t)); i++) {
		if (!strcmp(a_ctrl->af_OTP_info.act_name, act_func[i].act_name)) {
			a_ctrl->is_ois_supported = act_func[i].act_ext_info->is_ois_supported;
			a_ctrl->ois_slave_id = act_func[i].act_ext_info->ois_slave_id;
			a_ctrl->small_step_damping = act_func[i].act_ext_info->small_step_damping;
			a_ctrl->medium_step_damping = act_func[i].act_ext_info->medium_step_damping;
			a_ctrl->big_step_damping = act_func[i].act_ext_info->big_step_damping;
			a_ctrl->is_af_infinity_supported = act_func[i].act_ext_info->is_af_infinity_supported;
			for(j=0; j< ARRAY_SIZE(actuators); j++) {
				actuators[j]=act_func[i].act_ext_info->actuators[j];
			}
			break;
		}
	}
}
/*HTC_END*/
#endif
/*HTC_END*/

static int32_t msm_actuator_config(struct msm_actuator_ctrl_t *a_ctrl,
	void __user *argp)
{
	struct msm_actuator_cfg_data *cdata =
		(struct msm_actuator_cfg_data *)argp;
	int32_t rc = 0;
	mutex_lock(a_ctrl->actuator_mutex);
	CDBG("Enter\n");
	switch (cdata->cfgtype) {
/*HTC_START Harvey 20130701 - Set otp af value*/
	case CFG_SET_ACTUATOR_AF_VALUE:
		rc = msm_actuator_set_af_value(a_ctrl, (af_value_t)cdata->cfg.af_value);
		if (rc < 0) {
			pr_err("%s set af value failed %d\n", __func__, rc);
		}
		a_ctrl->prev_ois_mode = -1;
		break;
/*HTC_END*/
	case CFG_GET_ACTUATOR_INFO:
		cdata->is_af_supported = 1;
            /*HTC_START, move damping info to user space for actuator modulation*/
            #if 1
            #if defined(CONFIG_TI201_ACT)
            if(g_support_ois == 1)
            {
                a_ctrl->is_ois_supported = 1;
                a_ctrl->ois_slave_id = 0x48;
            }
            else
            #endif
            {
		a_ctrl->is_ois_supported = 0;
                a_ctrl->ois_slave_id = 0;
            }
            #if defined(CONFIG_TI201_ACT)
            pr_info("%s g_support_ois = %d, a_ctrl->is_ois_supported = %d\n", __func__, g_support_ois, a_ctrl->is_ois_supported);
            #endif
            #else
		/*HTC_START Harvey 20130704 - Separate code by different actuator module and dynamic launch*/
		msm_actuator_get_ext_info(a_ctrl);
		strlcpy(cdata->act_name, a_ctrl->af_OTP_info.act_name, sizeof(cdata->act_name));
		/*HTC_END*/
            #endif
            /*HTC_END*/
/*HTC_START Harvey 20130628 - Porting OIS*/
		#if 1
		//Temp workaround for M8. Return not support OIS to user mode layer.
		pr_info("%s: not return OIS support to user mode layer\n", __func__);
		cdata->is_ois_supported = 0;
		#else
		cdata->is_ois_supported = a_ctrl->is_ois_supported;
		#endif

	#if (defined(CONFIG_TI201_ACT) && defined(CONFIG_ACT_OIS_BINDER))
		if(a_ctrl->is_ois_supported ) { /*HTC Harvey 20130701 - Set otp af value*/
			struct msm_camera_cci_client oisbinder_cci_client;
			
			struct msm_camera_i2c_client oisbinder_i2c_client = {
				.addr_type = MSM_CAMERA_I2C_WORD_ADDR,
				.i2c_func_tbl = &msm_sensor_cci_func_tbl,
				.cci_client = &oisbinder_cci_client,
			};

			/*Just implement for MSM_CAMERA_PLATFORM_DEVICE in here*/
			memset(&oisbinder_cci_client, 0, sizeof(struct msm_camera_cci_client));
			oisbinder_cci_client.sid = a_ctrl->ois_slave_id >> 1;
			oisbinder_cci_client.retries = 3;
			oisbinder_cci_client.id_map = 0;
			oisbinder_cci_client.cci_i2c_master = a_ctrl->cci_master;
			oisbinder_cci_client.cci_subdev = a_ctrl->i2c_client.cci_client->cci_subdev;

			a_ctrl->oisbinder_i2c_add_driver = HtcActOisBinder_i2c_add_driver;
			a_ctrl->oisbinder_open_init = HtcActOisBinder_open_init;
			a_ctrl->oisbinder_power_down = HtcActOisBinder_power_down;
			a_ctrl->oisbinder_act_set_ois_mode = HtcActOisBinder_act_set_ois_mode;
			a_ctrl->oisbinder_mappingTbl_i2c_write = HtcActOisBinder_mappingTbl_i2c_write;
		
			if ((a_ctrl->oisbinder_i2c_add_driver) && (a_ctrl->ois_state == ACTUATOR_OIS_IDLE)) {
				a_ctrl->oisbinder_i2c_add_driver(&oisbinder_i2c_client);
			}
			a_ctrl->ois_state = ACTUATOR_OIS_I2C_ADD_DRIVER;
		}
	#endif
/*HTC_END*/
		cdata->small_step_damping = a_ctrl->small_step_damping;
		cdata->medium_step_damping = a_ctrl->medium_step_damping;
		cdata->big_step_damping = a_ctrl->big_step_damping;
		cdata->is_af_infinity_supported = a_ctrl->is_af_infinity_supported;
		cdata->cfg.cam_name = a_ctrl->cam_name;
		break;

	case CFG_SET_ACTUATOR_INFO:
		rc = msm_actuator_init(a_ctrl, &cdata->cfg.set_info);
		if (rc < 0)
			pr_err("init table failed %d\n", rc);
		break;

	case CFG_SET_DEFAULT_FOCUS:
		rc = a_ctrl->func_tbl->actuator_set_default_focus(a_ctrl,
			&cdata->cfg.move);
		if (rc < 0)
			pr_err("move focus failed %d\n", rc);
		break;

	case CFG_MOVE_FOCUS:
		rc = a_ctrl->func_tbl->actuator_move_focus(a_ctrl,
			&cdata->cfg.move);
		if (rc < 0)
			pr_err("move focus failed %d\n", rc);
		break;

	case CFG_IAF_MOVE_FOCUS:
		if(a_ctrl->func_tbl->actuator_iaf_move_focus == NULL ){
			pr_err("actuator_iaf_move_focus pointer is NULL");
			break;
		}
		rc = a_ctrl->func_tbl->actuator_iaf_move_focus(a_ctrl,&cdata->cfg.move);
		if (rc < 0)
			pr_err("move focus failed %d\n", rc);
		break;

/*HTC_START - For lc898212 VCM sorting*/
	case CFG_GET_VCM_SORTING:
	{
	    pr_info("CFG_GET_VCM_SORTING +\n");
	    lc898212_sorting(a_ctrl, &(cdata->max_diff));
	    pr_info("CFG_GET_VCM_SORTING  cdata->max_diff:0x%x, cdata->vcm_freq:0x%x\n", cdata->max_diff, cdata->vcm_freq);
	    pr_info("CFG_GET_VCM_SORTING -\n");
	}
	    break;
	case CFG_GET_VCM_LOOP_GAIN_SORTING:
	{
	    int i = 0;
	    pr_info("CFG_GET_VCM_LOOP_GAIN_SORTING + vcm_freq:0x%x, vcm_freq_ms22e:0x%x\n", cdata->vcm_freq, cdata->vcm_freq_ms22e);
	    lc898212_loop_gain_sorting(a_ctrl, cdata->gain_G1, cdata->gain_G2, cdata->vcm_freq, cdata->vcm_freq_ms22e);
	    for(i = 0; i < 5; i++)
	        pr_info("CFG_GET_VCM_SORTING gain_G1[%d]:0x%x, gain_G2[%d]:0x%x\n", i, cdata->gain_G1[i], i,  cdata->gain_G2[i]);
	    pr_info("CFG_GET_VCM_LOOP_GAIN_SORTING -\n");
	}
	    break;
/*HTC_END*/
/*HTC_START Harvey 20130628 - Porting OIS*/
	case CFG_ACTUATOR_STOP:
		rc = msm_actuator_stop(a_ctrl);
		if (rc < 0)
			pr_err("stop failed %d\n", rc);
		break;

	case CFG_SET_OIS_MODE:
		if (a_ctrl->is_ois_supported) {
			CDBG("%s prev_ois_mode %d\n", __func__, a_ctrl->prev_ois_mode);
			if (a_ctrl->func_tbl != NULL && a_ctrl->func_tbl->actuator_set_ois_mode != NULL) {
				if(a_ctrl->prev_ois_mode != cdata->cfg.ois_mode) {
					rc = a_ctrl->func_tbl->actuator_set_ois_mode(a_ctrl, (int)cdata->cfg.ois_mode);
					if (rc < 0)
						pr_err("%s set ois mode failed %d\n", __func__, rc);
					else
						a_ctrl->prev_ois_mode = cdata->cfg.ois_mode;
				}
			} else {
				pr_err("%s a_ctrl->func_tbl.actuator_set_ois_mode is NULL\n", __func__);
			}
		} else {
			pr_info("%s ois is not supported\n", __func__);
		}
		break;

	case CFG_UPDATE_OIS_TBL:
		if (a_ctrl->is_ois_supported) {
			if (a_ctrl->func_tbl != NULL && a_ctrl->func_tbl->actuator_update_ois_tbl != NULL) {
				rc = a_ctrl->func_tbl->actuator_update_ois_tbl(a_ctrl, &(cdata->cfg.sensor_actuator_info));
				if (rc < 0)
					pr_err("%s update ois table failed %d\n", __func__, rc);
			} else {
				pr_err("%s a_ctrl->func_tbl.actuator_update_ois_tbl is NULL\n", __func__);
			}
		} else {
			pr_info("%s ois is not supported\n", __func__);
		}
		break;
/*HTC_END*/
	default:
		break;
	}
	mutex_unlock(a_ctrl->actuator_mutex);
	CDBG("Exit\n");
	return rc;
}

static int32_t msm_actuator_get_subdev_id(struct msm_actuator_ctrl_t *a_ctrl,
	void *arg)
{
	uint32_t *subdev_id = (uint32_t *)arg;
	CDBG("Enter\n");
	if (!subdev_id) {
		pr_err("failed\n");
		return -EINVAL;
	}
	if (a_ctrl->act_device_type == MSM_CAMERA_PLATFORM_DEVICE)
		*subdev_id = a_ctrl->pdev->id;
	else
		*subdev_id = a_ctrl->subdev_id;

	CDBG("subdev_id %d\n", *subdev_id);
	CDBG("Exit\n");
	return 0;
}

static struct msm_camera_i2c_fn_t msm_sensor_cci_func_tbl = {
	.i2c_read = msm_camera_cci_i2c_read,
	.i2c_read_seq = msm_camera_cci_i2c_read_seq,
	.i2c_write = msm_camera_cci_i2c_write,
/*HTC_START Harvey 20130628 - Porting OIS*/
	.i2c_write_seq = msm_camera_cci_i2c_write_seq,
/*HTC_END*/
	.i2c_write_table = msm_camera_cci_i2c_write_table,
	.i2c_write_seq_table = msm_camera_cci_i2c_write_seq_table,
	.i2c_write_table_w_microdelay =
		msm_camera_cci_i2c_write_table_w_microdelay,
	.i2c_util = msm_sensor_cci_i2c_util,
};

static struct msm_camera_i2c_fn_t msm_sensor_qup_func_tbl = {
	.i2c_read = msm_camera_qup_i2c_read,
	.i2c_read_seq = msm_camera_qup_i2c_read_seq,
	.i2c_write = msm_camera_qup_i2c_write,
	.i2c_write_table = msm_camera_qup_i2c_write_table,
	.i2c_write_seq_table = msm_camera_qup_i2c_write_seq_table,
	.i2c_write_table_w_microdelay =
		msm_camera_qup_i2c_write_table_w_microdelay,
};

static int msm_actuator_open(struct v4l2_subdev *sd,
	struct v4l2_subdev_fh *fh) {
	int rc = 0;
	struct msm_actuator_ctrl_t *a_ctrl =  v4l2_get_subdevdata(sd);
	CDBG("Enter\n");
	if (!a_ctrl) {
		pr_err("failed\n");
		return -EINVAL;
	}
#if 0  //HTC_START
	if (a_ctrl->act_device_type == MSM_CAMERA_PLATFORM_DEVICE) {
		rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_util(
			&a_ctrl->i2c_client, MSM_CCI_INIT);
		if (rc < 0)
			pr_err("cci_init failed\n");
	}
#endif  //HTC_END
	CDBG("Exit\n");
	return rc;
}

static int msm_actuator_close(struct v4l2_subdev *sd,
	struct v4l2_subdev_fh *fh) {
	int rc = 0;
	struct msm_actuator_ctrl_t *a_ctrl =  v4l2_get_subdevdata(sd);
	CDBG("Enter\n");
	if (!a_ctrl) {
		pr_err("failed\n");
		return -EINVAL;
	}
#if 0  //HTC_START
	if (a_ctrl->act_device_type == MSM_CAMERA_PLATFORM_DEVICE) {
		rc = a_ctrl->i2c_client.i2c_func_tbl->i2c_util(
			&a_ctrl->i2c_client, MSM_CCI_RELEASE);
		if (rc < 0)
			pr_err("cci_init failed\n");
	}
#endif  //HTC_END
	kfree(a_ctrl->i2c_reg_tbl);
	a_ctrl->i2c_reg_tbl = NULL;
/*HTC_START*/
	kfree(a_ctrl->i2c_seq_reg_setting.reg_setting);
	a_ctrl->i2c_seq_reg_setting.reg_setting = NULL;
/*HTC_END*/

	CDBG("Exit\n");
	return rc;
}

static const struct v4l2_subdev_internal_ops msm_actuator_internal_ops = {
	.open = msm_actuator_open,
	.close = msm_actuator_close,
};

static long msm_actuator_subdev_ioctl(struct v4l2_subdev *sd,
			unsigned int cmd, void *arg)
{
	struct msm_actuator_ctrl_t *a_ctrl = v4l2_get_subdevdata(sd);
	void __user *argp = (void __user *)arg;
	CDBG("Enter\n");
	CDBG("%s:%d a_ctrl %p argp %p\n", __func__, __LINE__, a_ctrl, argp);
	switch (cmd) {
	case VIDIOC_MSM_SENSOR_GET_SUBDEV_ID:
		return msm_actuator_get_subdev_id(a_ctrl, argp);
	case VIDIOC_MSM_ACTUATOR_CFG:
		return msm_actuator_config(a_ctrl, argp);
	case MSM_SD_SHUTDOWN:
		msm_actuator_close(sd, NULL);
		return 0;
	default:
		return -ENOIOCTLCMD;
	}
}

static int32_t msm_actuator_power_up(struct msm_actuator_ctrl_t *a_ctrl)
{
	int rc = 0;
	CDBG("%s called\n", __func__);

	CDBG("vcm info: %x %x\n", a_ctrl->vcm_pwd,
		a_ctrl->vcm_enable);
	if (a_ctrl->vcm_enable) {
		rc = gpio_request(a_ctrl->vcm_pwd, "msm_actuator");
		if (!rc) {
			CDBG("Enable VCM PWD\n");
			gpio_direction_output(a_ctrl->vcm_pwd, 1);
		}
	}
	CDBG("Exit\n");
	return rc;
}

static int32_t msm_actuator_power(struct v4l2_subdev *sd, int on)
{
	int rc = 0;
	struct msm_actuator_ctrl_t *a_ctrl = v4l2_get_subdevdata(sd);
	CDBG("Enter\n");
	mutex_lock(a_ctrl->actuator_mutex);
	if (on)
		rc = msm_actuator_power_up(a_ctrl);
	else
		rc = msm_actuator_power_down(a_ctrl);
	mutex_unlock(a_ctrl->actuator_mutex);
	CDBG("Exit\n");
	return rc;
}

static struct v4l2_subdev_core_ops msm_actuator_subdev_core_ops = {
	.ioctl = msm_actuator_subdev_ioctl,
	.s_power = msm_actuator_power,
};

static struct v4l2_subdev_ops msm_actuator_subdev_ops = {
	.core = &msm_actuator_subdev_core_ops,
};

static const struct i2c_device_id msm_actuator_i2c_id[] = {
	{"qcom,actuator", (kernel_ulong_t)NULL},
	{ }
};

static int32_t msm_actuator_i2c_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	int rc = 0;
	struct msm_actuator_ctrl_t *act_ctrl_t = NULL;
	CDBG("Enter\n");

	if (client == NULL) {
		pr_err("msm_actuator_i2c_probe: client is null\n");
		rc = -EINVAL;
		goto probe_failure;
	}

	act_ctrl_t = kzalloc(sizeof(struct msm_actuator_ctrl_t),
		GFP_KERNEL);
	if (!act_ctrl_t) {
		pr_err("%s:%d failed no memory\n", __func__, __LINE__);
		return -ENOMEM;
	}

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		pr_err("i2c_check_functionality failed\n");
		goto probe_failure;
	}

	CDBG("client = %x\n", (unsigned int) client);

	rc = of_property_read_u32(client->dev.of_node, "cell-index",
		&act_ctrl_t->subdev_id);
	CDBG("cell-index %d, rc %d\n", act_ctrl_t->subdev_id, rc);
	if (rc < 0) {
		pr_err("failed rc %d\n", rc);
		return rc;
	}

	act_ctrl_t->i2c_driver = &msm_actuator_i2c_driver;
	act_ctrl_t->i2c_client.client = client;
	act_ctrl_t->curr_step_pos = 0,
	act_ctrl_t->curr_region_index = 0,
	/* Set device type as I2C */
	act_ctrl_t->act_device_type = MSM_CAMERA_I2C_DEVICE;
	act_ctrl_t->i2c_client.i2c_func_tbl = &msm_sensor_qup_func_tbl;
	act_ctrl_t->act_v4l2_subdev_ops = &msm_actuator_subdev_ops;
	act_ctrl_t->actuator_mutex = &msm_actuator_mutex;

	act_ctrl_t->cam_name = act_ctrl_t->subdev_id;
	CDBG("act_ctrl_t->cam_name: %d", act_ctrl_t->cam_name);
	/* Assign name for sub device */
	snprintf(act_ctrl_t->msm_sd.sd.name, sizeof(act_ctrl_t->msm_sd.sd.name),
		"%s", act_ctrl_t->i2c_driver->driver.name);

	/* Initialize sub device */
	v4l2_i2c_subdev_init(&act_ctrl_t->msm_sd.sd,
		act_ctrl_t->i2c_client.client,
		act_ctrl_t->act_v4l2_subdev_ops);
	v4l2_set_subdevdata(&act_ctrl_t->msm_sd.sd, act_ctrl_t);
	act_ctrl_t->msm_sd.sd.internal_ops = &msm_actuator_internal_ops;
	act_ctrl_t->msm_sd.sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	media_entity_init(&act_ctrl_t->msm_sd.sd.entity, 0, NULL, 0);
	act_ctrl_t->msm_sd.sd.entity.type = MEDIA_ENT_T_V4L2_SUBDEV;
	act_ctrl_t->msm_sd.sd.entity.group_id = MSM_CAMERA_SUBDEV_ACTUATOR;
	act_ctrl_t->msm_sd.close_seq = MSM_SD_CLOSE_2ND_CATEGORY | 0x2;
	msm_sd_register(&act_ctrl_t->msm_sd);
	pr_info("msm_actuator_i2c_probe: succeeded\n");
	CDBG("Exit\n");

probe_failure:
	return rc;
}

static int32_t msm_actuator_platform_probe(struct platform_device *pdev)
{
	int32_t rc = 0;
	struct msm_camera_cci_client *cci_client = NULL;
	struct msm_actuator_ctrl_t *msm_actuator_t = NULL;
	CDBG("Enter\n");

	if (!pdev->dev.of_node) {
		pr_err("of_node NULL\n");
		return -EINVAL;
	}

	msm_actuator_t = kzalloc(sizeof(struct msm_actuator_ctrl_t),
		GFP_KERNEL);
	if (!msm_actuator_t) {
		pr_err("%s:%d failed no memory\n", __func__, __LINE__);
		return -ENOMEM;
	}
	rc = of_property_read_u32((&pdev->dev)->of_node, "cell-index",
		&pdev->id);
	CDBG("cell-index %d, rc %d\n", pdev->id, rc);
	if (rc < 0) {
		pr_err("failed rc %d\n", rc);
		return rc;
	}

	rc = of_property_read_u32((&pdev->dev)->of_node, "qcom,cci-master",
		&msm_actuator_t->cci_master);
	CDBG("qcom,cci-master %d, rc %d\n", msm_actuator_t->cci_master, rc);
	if (rc < 0) {
		pr_err("failed rc %d\n", rc);
		return rc;
	}

	msm_actuator_t->act_v4l2_subdev_ops = &msm_actuator_subdev_ops;
	msm_actuator_t->actuator_mutex = &msm_actuator_mutex;
	msm_actuator_t->cam_name = pdev->id;

	/* Set platform device handle */
	msm_actuator_t->pdev = pdev;
	/* Set device type as platform device */
	msm_actuator_t->act_device_type = MSM_CAMERA_PLATFORM_DEVICE;
	msm_actuator_t->i2c_client.i2c_func_tbl = &msm_sensor_cci_func_tbl;
	msm_actuator_t->i2c_client.cci_client = kzalloc(sizeof(
		struct msm_camera_cci_client), GFP_KERNEL);
	if (!msm_actuator_t->i2c_client.cci_client) {
		pr_err("failed no memory\n");
		return -ENOMEM;
	}

	cci_client = msm_actuator_t->i2c_client.cci_client;
	cci_client->cci_subdev = msm_cci_get_subdev();
	cci_client->cci_i2c_master = MASTER_MAX;
	v4l2_subdev_init(&msm_actuator_t->msm_sd.sd,
		msm_actuator_t->act_v4l2_subdev_ops);
	v4l2_set_subdevdata(&msm_actuator_t->msm_sd.sd, msm_actuator_t);
	msm_actuator_t->msm_sd.sd.internal_ops = &msm_actuator_internal_ops;
	msm_actuator_t->msm_sd.sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	snprintf(msm_actuator_t->msm_sd.sd.name,
		ARRAY_SIZE(msm_actuator_t->msm_sd.sd.name), "msm_actuator");
	media_entity_init(&msm_actuator_t->msm_sd.sd.entity, 0, NULL, 0);
	msm_actuator_t->msm_sd.sd.entity.type = MEDIA_ENT_T_V4L2_SUBDEV;
	msm_actuator_t->msm_sd.sd.entity.group_id = MSM_CAMERA_SUBDEV_ACTUATOR;
	msm_actuator_t->msm_sd.close_seq = MSM_SD_CLOSE_2ND_CATEGORY | 0x2;
	msm_sd_register(&msm_actuator_t->msm_sd);

/*HTC_START Harvey 20130628 - Porting OIS*/
	msm_actuator_t->ois_state = ACTUATOR_OIS_IDLE;
/*HTC_END*/

	msm_actuator_t->small_step_damping = 0;
	msm_actuator_t->medium_step_damping = 0;
	msm_actuator_t->big_step_damping = 0;
	msm_actuator_t->is_af_infinity_supported = 1;

	CDBG("Exit\n");
	return rc;
}

static const struct of_device_id msm_actuator_i2c_dt_match[] = {
	{.compatible = "qcom,actuator"},
	{}
};

MODULE_DEVICE_TABLE(of, msm_actuator_i2c_dt_match);

static struct i2c_driver msm_actuator_i2c_driver = {
	.id_table = msm_actuator_i2c_id,
	.probe  = msm_actuator_i2c_probe,
	.remove = __exit_p(msm_actuator_i2c_remove),
	.driver = {
		.name = "qcom,actuator",
		.owner = THIS_MODULE,
		.of_match_table = msm_actuator_i2c_dt_match,
	},
};

static const struct of_device_id msm_actuator_dt_match[] = {
	{.compatible = "qcom,actuator", .data = NULL},
	{}
};

MODULE_DEVICE_TABLE(of, msm_actuator_dt_match);

static struct platform_driver msm_actuator_platform_driver = {
	.driver = {
		.name = "qcom,actuator",
		.owner = THIS_MODULE,
		.of_match_table = msm_actuator_dt_match,
	},
};

static int __init msm_actuator_init_module(void)
{
	int32_t rc = 0;
	CDBG("Enter\n");
	rc = platform_driver_probe(&msm_actuator_platform_driver,
		msm_actuator_platform_probe);
	if (!rc)
		return rc;
	CDBG("%s:%d rc %d\n", __func__, __LINE__, rc);
	return i2c_add_driver(&msm_actuator_i2c_driver);
}

static struct msm_actuator msm_vcm_actuator_table = {
	.act_type = ACTUATOR_VCM,
	.func_tbl = {
		.actuator_init_step_table = msm_actuator_init_step_table,
		.actuator_move_focus = msm_actuator_move_focus,
		.actuator_write_focus = msm_actuator_write_focus,
		.actuator_set_default_focus = msm_actuator_set_default_focus,
		.actuator_init_focus = msm_actuator_init_focus,
		.actuator_parse_i2c_params = msm_actuator_parse_i2c_params,
/*HTC_START Harvey 20130628 - Porting OIS*/
		.actuator_set_ois_mode = msm_actuator_set_ois_mode,
		.actuator_update_ois_tbl = msm_actuator_update_ois_tbl,
/*HTC_END*/
	},
};

static struct msm_actuator msm_piezo_actuator_table = {
	.act_type = ACTUATOR_PIEZO,
	.func_tbl = {
		.actuator_init_step_table = NULL,
		.actuator_move_focus = msm_actuator_piezo_move_focus,
		.actuator_write_focus = NULL,
		.actuator_set_default_focus =
			msm_actuator_piezo_set_default_focus,
		.actuator_init_focus = msm_actuator_init_focus,
		.actuator_parse_i2c_params = msm_actuator_parse_i2c_params,
/*HTC_START Harvey 20130628 - Porting OIS*/
		.actuator_set_ois_mode = msm_actuator_set_ois_mode,
		.actuator_update_ois_tbl = msm_actuator_update_ois_tbl,
/*HTC_END*/
	},
};

module_init(msm_actuator_init_module);
MODULE_DESCRIPTION("MSM ACTUATOR");
MODULE_LICENSE("GPL v2");
