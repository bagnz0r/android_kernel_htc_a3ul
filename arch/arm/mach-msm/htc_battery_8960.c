/* arch/arm/mach-msm/htc_battery_8960.c
 *
 * Copyright (C) 2011 HTC Corporation.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/debugfs.h>
#include <linux/wakelock.h>
#include <linux/gpio.h>
#include <mach/board.h>
#include <asm/mach-types.h>
#include <mach/devices_cmdline.h>
#include <mach/devices_dtb.h>
#include <mach/htc_battery_core.h>
#include <mach/htc_battery_8960.h>
#include <linux/workqueue.h>
#include <linux/slab.h>
#include <linux/reboot.h>
#include <linux/miscdevice.h>
#include <linux/pmic8058-xoadc.h>
#include <mach/mpp.h>
#include <linux/android_alarm.h>
#include <linux/suspend.h>
#if defined(CONFIG_FB)
#include <linux/notifier.h>
#include <linux/fb.h>
#elif defined(CONFIG_HAS_EARLYSUSPEND)
#include <linux/earlysuspend.h>
#endif

#include <mach/htc_gauge.h>
#include <mach/htc_charger.h>
#include <mach/htc_battery_cell.h>


/* disable charging reason */
#define HTC_BATT_CHG_DIS_BIT_EOC	(1)
#define HTC_BATT_CHG_DIS_BIT_ID		(1<<1)
#define HTC_BATT_CHG_DIS_BIT_TMP	(1<<2)
#define HTC_BATT_CHG_DIS_BIT_OVP	(1<<3)
#define HTC_BATT_CHG_DIS_BIT_TMR	(1<<4)
#define HTC_BATT_CHG_DIS_BIT_MFG	(1<<5)
#define HTC_BATT_CHG_DIS_BIT_USR_TMR	(1<<6)
#define HTC_BATT_CHG_DIS_BIT_STOP_SWOLLEN	(1<<7)

static int chg_dis_reason;
static int chg_dis_active_mask = HTC_BATT_CHG_DIS_BIT_ID
								| HTC_BATT_CHG_DIS_BIT_MFG
								| HTC_BATT_CHG_DIS_BIT_STOP_SWOLLEN
								| HTC_BATT_CHG_DIS_BIT_TMP
								| HTC_BATT_CHG_DIS_BIT_TMR
								| HTC_BATT_CHG_DIS_BIT_USR_TMR;
static int chg_dis_control_mask = HTC_BATT_CHG_DIS_BIT_ID
								| HTC_BATT_CHG_DIS_BIT_MFG
								| HTC_BATT_CHG_DIS_BIT_STOP_SWOLLEN
								| HTC_BATT_CHG_DIS_BIT_USR_TMR;
/* disable pwrsrc reason */
#define HTC_BATT_PWRSRC_DIS_BIT_MFG		(1)
#define HTC_BATT_PWRSRC_DIS_BIT_API		(1<<1)
static int pwrsrc_dis_reason;

static int need_sw_stimer;
/* ext charger's safety timer */
static unsigned long sw_stimer_counter;
static int sw_stimer_fault;
#define HTC_SAFETY_TIME_16_HR_IN_MS		(16*60*60*1000)


/* for sprint disable charging cmd */
static int chg_dis_user_timer;
/* for charger disable charging by temperature fault */
static int charger_dis_temp_fault;
/* is charger under rating */
static int charger_under_rating;
/* is safety timer of charger timeout */
static int charger_safety_timeout;
/* is battery fully charged with charging stopped */
static int batt_full_eoc_stop;

/* for limited charge */
static int chg_limit_reason;
static int chg_limit_active_mask;
#ifdef CONFIG_DUTY_CYCLE_LIMIT
static int chg_limit_timer_sub_mask;
#endif

/* for suspend high frequency (5min) */
#define SUSPEND_HIGHFREQ_CHECK_BIT_TALK		(1)
#define SUSPEND_HIGHFREQ_CHECK_BIT_SEARCH	(1<<1)
#define SUSPEND_HIGHFREQ_CHECK_BIT_MUSIC	(1<<3)
static int suspend_highfreq_check_reason;

/* for batt_context_state */
#define CONTEXT_STATE_BIT_TALK			(1)
#define CONTEXT_STATE_BIT_SEARCH		(1<<1)
#define CONTEXT_STATE_BIT_NAVIGATION		(1<<2)
#define CONTEXT_STATE_BIT_MUSIC			(1<<3)
static int context_state;

/* for htc_batt_info.state */
#define STATE_WORKQUEUE_PENDING			(1)
#define STATE_EARLY_SUSPEND			(1<<1)
#define STATE_PREPARE			(1<<2)
#define STATE_SUSPEND			(1<<3)

#define BATT_SUSPEND_CHECK_TIME				(3600)
#define BATT_SUSPEND_HIGHFREQ_CHECK_TIME	(300)
#define BATT_TIMER_CHECK_TIME				(360)
#define BATT_TIMER_UPDATE_TIME				(60)

/* for htc_extension */
#define HTC_EXT_UNKNOWN_USB_CHARGER		(1<<0)
#define HTC_EXT_CHG_UNDER_RATING		(1<<1)
#define HTC_EXT_CHG_SAFTY_TIMEOUT		(1<<2)
#define HTC_EXT_CHG_FULL_EOC_STOP		(1<<3)

#ifdef CONFIG_ARCH_MSM8X60_LTE
/* MATT: fix me: check style warning */
/* extern int __ref cpu_down(unsigned int cpu); */
/* extern int board_mfg_mode(void); */
#endif

static void mbat_in_func(struct work_struct *work);
struct delayed_work mbat_in_struct;
//static DECLARE_DELAYED_WORK(mbat_in_struct, mbat_in_func);
static struct kset *htc_batt_kset;

#define BATT_REMOVED_SHUTDOWN_DELAY_MS (50)
#define BATT_CRITICAL_VOL_SHUTDOWN_DELAY_MS (1000)
static void shutdown_worker(struct work_struct *work);
struct delayed_work shutdown_work;
//static DECLARE_DELAYED_WORK(shutdown_work, shutdown_worker);

/* battery voltage alarm */
#define BATT_CRITICAL_LOW_VOLTAGE		(3000)
static int critical_shutdown = 0;
/*
 * critical_alarm_level = {0, 1, 2}, correspond
 * alarm voltage(level) = critical_alarm_vol_ptr[critical_alarm_level]
 */
static int critical_alarm_level;
static int critical_alarm_level_set;
struct wake_lock voltage_alarm_wake_lock;
struct wake_lock batt_shutdown_wake_lock;

#ifdef CONFIG_HAS_EARLYSUSPEND
static struct early_suspend early_suspend;
#endif
#ifdef CONFIG_HTC_BATT_ALARM
static int screen_state;

/* To disable holding wakelock when AC in for suspend test */
static int ac_suspend_flag;
#endif
static int htc_ext_5v_output_now;
static int htc_ext_5v_output_old;

/* static int prev_charging_src; */
static int latest_chg_src = CHARGER_BATTERY;

struct htc_battery_info {
	int device_id;

	/* lock to protect the battery info */
	struct mutex info_lock;

	spinlock_t batt_lock;
	int is_open;

	/* threshold voltage to drop battery level aggressively */
	int critical_low_voltage_mv;
	/* voltage alarm threshold voltage */
	int *critical_alarm_vol_ptr;
	int critical_alarm_vol_cols;
	int force_shutdown_batt_vol;
	int overload_vol_thr_mv;
	int overload_curr_thr_ma;
	int smooth_chg_full_delay_min;
	int decreased_batt_level_check;
	struct kobject batt_timer_kobj;
	struct kobject batt_cable_kobj;

	struct wake_lock vbus_wake_lock;
	char debug_log[DEBUG_LOG_LENGTH];

	struct battery_info_reply rep;
	struct mpp_config_data *mpp_config;
	struct battery_adc_reply adc_data;
	int adc_vref[ADC_REPLY_ARRAY_SIZE];

	int guage_driver;
	int charger;
	/* gauge/charger interface */
	struct htc_gauge *igauge;
	struct htc_charger *icharger;
	struct htc_battery_cell *bcell;
	int state;
	unsigned int htc_extension;	/* for htc in-house sw */
#if defined(CONFIG_FB)
	struct notifier_block fb_notif;
	struct workqueue_struct *batt_fb_wq;
	struct delayed_work work_fb;
#endif
};
static struct htc_battery_info htc_batt_info;

struct htc_battery_timer {
	struct mutex schedule_lock;
	unsigned long batt_system_jiffies;
	unsigned long batt_suspend_ms;
	unsigned long total_time_ms;	/* since last do batt_work */
	unsigned int batt_alarm_status;
#ifdef CONFIG_HTC_BATT_ALARM
	unsigned int batt_critical_alarm_counter;
#endif
	unsigned int batt_alarm_enabled;
	unsigned int alarm_timer_flag;
	unsigned int time_out;
	struct work_struct batt_work;
	struct delayed_work unknown_usb_detect_work;
	struct alarm batt_check_wakeup_alarm;
	struct timer_list batt_timer;
	struct workqueue_struct *batt_wq;
	struct wake_lock battery_lock;
	struct wake_lock unknown_usb_detect_lock;
};
static struct htc_battery_timer htc_batt_timer;

/* cable detect */
struct mutex cable_notifier_lock; /* synchronize notifier func call */
static void cable_status_notifier_func(int online);
static struct t_cable_status_notifier cable_status_notifier = {
	.name = "htc_battery_8960",
	.func = cable_status_notifier_func,
};

static int htc_battery_initial;
static int htc_full_level_flag;
static int htc_battery_set_charging(int ctl);

#ifdef CONFIG_HTC_BATT_ALARM
static int battery_vol_alarm_mode;
static struct battery_vol_alarm alarm_data;
struct mutex batt_set_alarm_lock;
#endif

#if defined(CONFIG_FB)
static int fb_notifier_callback(struct notifier_block *self,
                                 unsigned long event, void *data);
#endif

struct dec_level_by_current_ua {
	int threshold_ua;
	int dec_level;
};
static struct dec_level_by_current_ua dec_level_curr_table[] = {
							{900000, 2},
							{600000, 4},
							{0, 6},
};

static const int DEC_LEVEL_CURR_TABLE_SIZE = sizeof(dec_level_curr_table) / sizeof (dec_level_curr_table[0]);

#ifdef CONFIG_DUTY_CYCLE_LIMIT
enum {
	LIMIT_CHG_TIMER_STATE_NONE = 0,
	LIMIT_CHG_TIMER_STATE_ON = 1,
	LIMIT_CHG_TIMER_STATE_OFF = 2,
};

static uint limit_chg_timer_state = 0;
struct delayed_work limit_chg_timer_work;

static int limit_charge_timer_ma = 0; /* HTC designed */
module_param(limit_charge_timer_ma, int, 0644);

static int limit_charge_timer_on = 0; /* HTC designed */
module_param(limit_charge_timer_on, int, 0644);

static int limit_charge_timer_off = 0; /* HTC designed */
module_param(limit_charge_timer_off, int, 0644);
#endif

/* htc gauge convenience function */
int htc_gauge_get_battery_voltage(int *result)
{
	if (htc_batt_info.igauge && htc_batt_info.igauge->get_battery_voltage)
		return htc_batt_info.igauge->get_battery_voltage(result);
	pr_warn("[BATT] interface doesn't exist\n");
	return -EINVAL;
}
EXPORT_SYMBOL(htc_gauge_get_battery_voltage);

int htc_gauge_set_chg_ovp(int is_ovp)
{
	if (htc_batt_info.igauge && htc_batt_info.igauge->set_chg_ovp)
		return htc_batt_info.igauge->set_chg_ovp(is_ovp);
	pr_warn("[BATT] interface doesn't exist\n");
	return -EINVAL;
}
EXPORT_SYMBOL(htc_gauge_set_chg_ovp);

/* For touch panel, touch panel may loss wireless charger notification when system boot up */
int htc_is_wireless_charger(void)
{
	if (htc_battery_initial)
		return (htc_batt_info.rep.charging_source == CHARGER_WIRELESS) ? 1 : 0;
	else
		return -1;
}

/* matt add */
int htc_batt_schedule_batt_info_update(void)
{
	if (htc_batt_info.state & STATE_WORKQUEUE_PENDING) {
		htc_batt_info.state &= ~STATE_WORKQUEUE_PENDING;
		pr_debug("[BATT] %s(): Clear flag, htc_batt_info.state=0x%x\n",
				__func__, htc_batt_info.state);
	}

	/*  MATT: use spin_lock? */
	wake_lock(&htc_batt_timer.battery_lock);
	queue_work(htc_batt_timer.batt_wq, &htc_batt_timer.batt_work);
	return 0;
}

/* generic voltage alarm handle */
static void batt_lower_voltage_alarm_handler(int status)
{
	wake_lock(&voltage_alarm_wake_lock);
	if (status) {
		if (htc_batt_info.igauge->enable_lower_voltage_alarm)
			htc_batt_info.igauge->enable_lower_voltage_alarm(0);
		BATT_LOG("voltage_alarm level=%d (%d mV) triggered.",
			critical_alarm_level,
			htc_batt_info.critical_alarm_vol_ptr[critical_alarm_level]);
		if (critical_alarm_level == 0)
			critical_shutdown = 1;
		critical_alarm_level--;
		htc_batt_schedule_batt_info_update();
	} else {
		pr_info("[BATT] voltage_alarm level=%d (%d mV) raised back.\n",
			critical_alarm_level,
			htc_batt_info.critical_alarm_vol_ptr[critical_alarm_level]);
	}
	wake_unlock(&voltage_alarm_wake_lock);
}

#define UNKNOWN_USB_DETECT_DELAY_MS	(5000)
static void unknown_usb_detect_worker(struct work_struct *work)
{
	mutex_lock(&cable_notifier_lock);
	pr_info("[BATT] %s\n", __func__);
	if (latest_chg_src == CHARGER_DETECTING)
	{
		htc_charger_event_notify(HTC_CHARGER_EVENT_SRC_UNKNOWN_USB);
	}
	mutex_unlock(&cable_notifier_lock);
	wake_unlock(&htc_batt_timer.unknown_usb_detect_lock);
}

/* TODO: synchornize this function */
int htc_gauge_event_notify(enum htc_gauge_event event)
{
	pr_info("[BATT] %s gauge event=%d\n", __func__, event);

	switch (event) {
	case HTC_GAUGE_EVENT_READY:
		if (!htc_batt_info.igauge) {
			pr_err("[BATT]err: htc_gauge is not hooked.\n");
			break;
		}
		mutex_lock(&htc_batt_info.info_lock);
		htc_batt_info.igauge->ready = 1;

		if (htc_batt_info.icharger && htc_batt_info.icharger->ready
						&& htc_batt_info.rep.cable_ready)
			htc_batt_info.rep.batt_state = 1;
		if (htc_batt_info.rep.batt_state)
			htc_batt_schedule_batt_info_update();

		/* register batt alarm */
		if (htc_batt_info.igauge && htc_batt_info.critical_alarm_vol_cols) {
			if (htc_batt_info.igauge->register_lower_voltage_alarm_notifier)
				htc_batt_info.igauge->register_lower_voltage_alarm_notifier(
									batt_lower_voltage_alarm_handler);
			if (htc_batt_info.igauge->set_lower_voltage_alarm_threshold)
				htc_batt_info.igauge->set_lower_voltage_alarm_threshold(
					htc_batt_info.critical_alarm_vol_ptr[critical_alarm_level]);
			if (htc_batt_info.igauge->enable_lower_voltage_alarm)
				htc_batt_info.igauge->enable_lower_voltage_alarm(1);
		}

		mutex_unlock(&htc_batt_info.info_lock);
		break;
	case HTC_GAUGE_EVENT_TEMP_ZONE_CHANGE:
		if (htc_batt_info.state & STATE_PREPARE) {
			htc_batt_info.state |= STATE_WORKQUEUE_PENDING;
			pr_info("[BATT] %s(): Skip due to htc_batt_info.state=0x%x\n",
				__func__, htc_batt_info.state);
		} else {
			pr_debug("[BATT] %s(): Run, htc_batt_info.state=0x%x\n",
					__func__, htc_batt_info.state);
			htc_batt_schedule_batt_info_update();
		}
		break;
	case HTC_GAUGE_EVENT_EOC:
	case HTC_GAUGE_EVENT_OVERLOAD:
		htc_batt_schedule_batt_info_update();
		break;
	case HTC_GAUGE_EVENT_LOW_VOLTAGE_ALARM:
		batt_lower_voltage_alarm_handler(1);
		break;
	case HTC_GAUGE_EVENT_BATT_REMOVED:
		if (!(get_kernel_flag() & KERNEL_FLAG_TEST_PWR_SUPPLY)) {
		wake_lock(&batt_shutdown_wake_lock);
		schedule_delayed_work(&shutdown_work,
			msecs_to_jiffies(BATT_REMOVED_SHUTDOWN_DELAY_MS));
		}
		break;
	case HTC_GAUGE_EVENT_EOC_STOP_CHG:
		sw_stimer_counter = 0;
		htc_batt_schedule_batt_info_update();
		break;
	default:
		pr_info("[BATT] unsupported gauge event(%d)\n", event);
		break;
	}
	return 0;
}

/* TODO: synchornize this function and gauge_event_notify:
 * define call condition: in interrupt context or not. or separate it:
 * idea1: move cable event to cable_status_handler directly.
 * idea2: move those need mutex protect code to work queue worker.
 * idea3: queuing all event handler here. or queuing all caller function
 *        to prevent it calls from interrupt handler. */
int htc_charger_event_notify(enum htc_charger_event event)
{
	/* MATT TODO: check we need to queue a work here */
	pr_info("[BATT] %s charger event=%d\n", __func__, event);
	switch (event) {
	case HTC_CHARGER_EVENT_BATT_UEVENT_CHANGE :
		htc_battery_update_batt_uevent();
		break;
	case HTC_CHARGER_EVENT_VBUS_IN:
		/* latest_chg_src = CHARGER_USB; */
		/* htc_batt_info.rep.charging_source = CHARGER_USB; */
		break;
	case HTC_CHARGER_EVENT_SRC_INTERNAL:
		htc_ext_5v_output_now = 1;
		BATT_LOG("%s htc_ext_5v_output_now:%d", __func__, htc_ext_5v_output_now);
		htc_batt_schedule_batt_info_update();
		break;
	case HTC_CHARGER_EVENT_SRC_CLEAR:
		latest_chg_src = CHARGER_BATTERY;
		htc_ext_5v_output_now = 0;
		BATT_LOG("%s htc_ext_5v_output_now:%d", __func__, htc_ext_5v_output_now);
		htc_batt_schedule_batt_info_update();
		break;
	case HTC_CHARGER_EVENT_VBUS_OUT:
	case HTC_CHARGER_EVENT_SRC_NONE: /* synchorized call from cable notifier */
		latest_chg_src = CHARGER_BATTERY;
		/* prev_charging_src = htc_batt_info.rep.charging_source;
		htc_batt_info.rep.charging_source = CHARGER_BATTERY; */
		if (delayed_work_pending(&htc_batt_timer.unknown_usb_detect_work)) {
			cancel_delayed_work_sync(&htc_batt_timer.unknown_usb_detect_work);
			wake_unlock(&htc_batt_timer.unknown_usb_detect_lock);
		}
		htc_batt_schedule_batt_info_update();
		break;
	case HTC_CHARGER_EVENT_SRC_USB: /* synchorized call from cable notifier */
		latest_chg_src = CHARGER_USB;
		/* prev_charging_src = htc_batt_info.rep.charging_source;
		htc_batt_info.rep.charging_source = CHARGER_USB; */
		htc_batt_schedule_batt_info_update();
		break;
	case HTC_CHARGER_EVENT_SRC_AC: /* synchforized call form cable notifier */
		latest_chg_src = CHARGER_AC;
		/* prev_charging_src = htc_batt_info.rep.charging_source;
		htc_batt_info.rep.charging_source = CHARGER_AC; */
		htc_batt_schedule_batt_info_update();
		break;
	case HTC_CHARGER_EVENT_SRC_WIRELESS: /* synchorized call from cable notifier */
		latest_chg_src = CHARGER_WIRELESS;
		/* prev_charging_src = htc_batt_info.rep.charging_source;
		htc_batt_info.rep.charging_source = CHARGER_USB; */
		htc_batt_schedule_batt_info_update();
		break;
	case HTC_CHARGER_EVENT_SRC_DETECTING: /* synchorized call from cable notifier */
		latest_chg_src = CHARGER_DETECTING;
		/* prev_charging_src = htc_batt_info.rep.charging_source;
		htc_batt_info.rep.charging_source = CHARGER_DETECTING; */
		htc_batt_schedule_batt_info_update();
		wake_lock(&htc_batt_timer.unknown_usb_detect_lock);
		queue_delayed_work(htc_batt_timer.batt_wq,
				&htc_batt_timer.unknown_usb_detect_work,
				round_jiffies_relative(msecs_to_jiffies(
								UNKNOWN_USB_DETECT_DELAY_MS)));
		break;
	case HTC_CHARGER_EVENT_SRC_UNKNOWN_USB: /* synchorized call from cable notifier */
		/* for  ATS tesing evironment, if detect unknown
		charger, we reply AC if set flag 6 4000000*/
		if (get_kernel_flag() & KERNEL_FLAG_ENABLE_FAST_CHARGE)
			latest_chg_src = CHARGER_AC;
		else
			latest_chg_src = CHARGER_UNKNOWN_USB;
		/* prev_charging_src = htc_batt_info.rep.charging_source;
		htc_batt_info.rep.charging_source = CHARGER_UNKNOWN_USB; */
		htc_batt_schedule_batt_info_update();
		break;
	case HTC_CHARGER_EVENT_OVP:
	case HTC_CHARGER_EVENT_OVP_RESOLVE:
	case HTC_CHARGER_EVENT_SRC_UNDER_RATING:
	case HTC_CHARGER_EVENT_SAFETY_TIMEOUT:
		htc_batt_schedule_batt_info_update();
		break;
	case HTC_CHARGER_EVENT_SRC_MHL_AC:
		latest_chg_src = CHARGER_MHL_AC;
		htc_batt_schedule_batt_info_update();
		break;
	case HTC_CHARGER_EVENT_READY:
		if (!htc_batt_info.icharger) {
			pr_err("[BATT]err: htc_charger is not hooked.\n");
				/* MATT TODO check if we allow batt_state set to 1 here */
			break;
		}
		mutex_lock(&htc_batt_info.info_lock);
		htc_batt_info.icharger->ready = 1;

		if (htc_batt_info.igauge && htc_batt_info.igauge->ready
						&& htc_batt_info.rep.cable_ready)
			htc_batt_info.rep.batt_state = 1;
		if (htc_batt_info.rep.batt_state)
			htc_batt_schedule_batt_info_update();

		mutex_unlock(&htc_batt_info.info_lock);
		break;
	case HTC_CHARGER_EVENT_SRC_CABLE_INSERT_NOTIFY:
		latest_chg_src = CHARGER_NOTIFY;
		htc_batt_schedule_batt_info_update();
		break;
	default:
		pr_info("[BATT] unsupported charger event(%d)\n", event);
		break;
	}
	return 0;
}


/* MATT porting */
#if 0
#ifdef CONFIG_HTC_BATT_ALARM
static int batt_set_voltage_alarm(unsigned long lower_threshold,
			unsigned long upper_threshold)
#else
static int batt_alarm_config(unsigned long lower_threshold,
			unsigned long upper_threshold)
#endif
{
	int rc = 0;

	BATT_LOG("%s(lw = %lu, up = %lu)", __func__,
		lower_threshold, upper_threshold);
	rc = pm8058_batt_alarm_state_set(0, 0);
	if (rc) {
		BATT_ERR("state_set disabled failed, rc=%d", rc);
		goto done;
	}

	rc = pm8058_batt_alarm_threshold_set(lower_threshold, upper_threshold);
	if (rc) {
		BATT_ERR("threshold_set failed, rc=%d!", rc);
		goto done;
	}

#ifdef CONFIG_HTC_BATT_ALARM
	rc = pm8058_batt_alarm_state_set(1, 0);
	if (rc) {
		BATT_ERR("state_set enabled failed, rc=%d", rc);
		goto done;
	}
#endif

done:
	return rc;
}
#ifdef CONFIG_HTC_BATT_ALARM
static int batt_clear_voltage_alarm(void)
{
	int rc = pm8058_batt_alarm_state_set(0, 0);
	BATT_LOG("disable voltage alarm");
	if (rc)
		BATT_ERR("state_set disabled failed, rc=%d", rc);
	return rc;
}

static int batt_set_voltage_alarm_mode(int mode)
{
	int rc = 0;


	BATT_LOG("%s , mode:%d\n", __func__, mode);


	mutex_lock(&batt_set_alarm_lock);
	/*if (battery_vol_alarm_mode != BATT_ALARM_DISABLE_MODE &&
		mode != BATT_ALARM_DISABLE_MODE)
		BATT_ERR("%s:Warning:set mode to %d from non-disable mode(%d)",
			__func__, mode, battery_vol_alarm_mode); */
	switch (mode) {
	case BATT_ALARM_DISABLE_MODE:
		rc = batt_clear_voltage_alarm();
		break;
	case BATT_ALARM_CRITICAL_MODE:
		rc = batt_set_voltage_alarm(BATT_CRITICAL_LOW_VOLTAGE,
			alarm_data.upper_threshold);
		break;
	default:
	case BATT_ALARM_NORMAL_MODE:
		rc = batt_set_voltage_alarm(alarm_data.lower_threshold,
			alarm_data.upper_threshold);
		break;
	}
	if (!rc)
		battery_vol_alarm_mode = mode;
	else {
		battery_vol_alarm_mode = BATT_ALARM_DISABLE_MODE;
		batt_clear_voltage_alarm();
	}
	mutex_unlock(&batt_set_alarm_lock);
	return rc;
}
#endif

static int battery_alarm_notifier_func(struct notifier_block *nfb,
					unsigned long value, void *data);
static struct notifier_block battery_alarm_notifier = {
	.notifier_call = battery_alarm_notifier_func,
};

static int battery_alarm_notifier_func(struct notifier_block *nfb,
					unsigned long status, void *data)
{

#ifdef CONFIG_HTC_BATT_ALARM
	BATT_LOG("%s \n", __func__);

	if (battery_vol_alarm_mode == BATT_ALARM_CRITICAL_MODE) {
		BATT_LOG("%s(): CRITICAL_MODE counter = %d", __func__,
			htc_batt_timer.batt_critical_alarm_counter + 1);
		if (++htc_batt_timer.batt_critical_alarm_counter >= 3) {
			BATT_LOG("%s: 3V voltage alarm is triggered.", __func__);
			htc_batt_info.rep.level = 1;
			/* htc_battery_core_update(BATTERY_SUPPLY); */
			htc_battery_core_update_changed();
		}
		batt_set_voltage_alarm_mode(BATT_ALARM_CRITICAL_MODE);
	} else if (battery_vol_alarm_mode == BATT_ALARM_NORMAL_MODE) {
		htc_batt_timer.batt_alarm_status++;
		BATT_LOG("%s: NORMAL_MODE batt alarm status = %u", __func__,
			htc_batt_timer.batt_alarm_status);
	} else { /* BATT_ALARM_DISABLE_MODE */
		BATT_ERR("%s:Warning: batt alarm triggerred in disable mode ", __func__);
	}
#else
	htc_batt_timer.batt_alarm_status++;
	BATT_LOG("%s: batt alarm status %u", __func__, htc_batt_timer.batt_alarm_status);
#endif
	return 0;
}
#endif

/* MATT porting */
#if 0
static void update_wake_lock(int status)
{
#ifdef CONFIG_HTC_BATT_ALARM
	/* hold an wakelock when charger connected until disconnected
		except for AC under test mode(ac_suspend_flag=1). */
	if (status != CHARGER_BATTERY && !ac_suspend_flag)
		wake_lock(&htc_batt_info.vbus_wake_lock);
	else if (status == CHARGER_USB && ac_suspend_flag)
		/* For suspend test, not hold wake lock when AC in */
		wake_lock(&htc_batt_info.vbus_wake_lock);
	else
		/* give userspace some time to see the uevent and update
		   LED state or whatnot...*/
		   wake_lock_timeout(&htc_batt_info.vbus_wake_lock, HZ * 5);
#else
	if (status == CHARGER_USB)
		wake_lock(&htc_batt_info.vbus_wake_lock);
	else
		/* give userspace some time to see the uevent and update
		   LED state or whatnot...*/
		wake_lock_timeout(&htc_batt_info.vbus_wake_lock, HZ * 5);
#endif
}
#endif

static void cable_status_notifier_func(enum usb_connect_type online)
{
	static int first_update = 1;
	mutex_lock(&cable_notifier_lock);
	htc_batt_info.rep.cable_ready = 1;

	if (htc_batt_info.igauge && htc_batt_info.icharger
					&& !(htc_batt_info.rep.batt_state))
		if(htc_batt_info.igauge->ready
				&& htc_batt_info.icharger->ready)
			htc_batt_info.rep.batt_state = 1;

	BATT_LOG("%s(%d)", __func__, online);
	//if (online == htc_batt_info.rep.charging_source && !first_update) {
	if (online == latest_chg_src && !first_update) {
		BATT_LOG("%s: charger type (%u) same return.",
			__func__, online);
		mutex_unlock(&cable_notifier_lock);
		return;
	}
	first_update = 0;

	switch (online) {
	case CONNECT_TYPE_USB:
		BATT_LOG("USB charger");
		htc_charger_event_notify(HTC_CHARGER_EVENT_SRC_USB);
		/* radio_set_cable_status(CHARGER_USB); */
		break;
	case CONNECT_TYPE_AC:
		BATT_LOG("5V AC charger");
		htc_charger_event_notify(HTC_CHARGER_EVENT_SRC_AC);
		/* radio_set_cable_status(CHARGER_AC); */
		break;
	case CONNECT_TYPE_WIRELESS:
		BATT_LOG("wireless charger");
		htc_charger_event_notify(HTC_CHARGER_EVENT_SRC_WIRELESS);
		/* radio_set_cable_status(CHARGER_WIRELESS); */
		break;
	case CONNECT_TYPE_UNKNOWN:
		BATT_LOG("unknown type");
		htc_charger_event_notify(HTC_CHARGER_EVENT_SRC_DETECTING);
		break;
	case CONNECT_TYPE_INTERNAL:
		BATT_LOG("delivers power to VBUS from battery (not supported)");
		/* pass mhl dongle info into ext charger, for output 5v purpose */
		htc_charger_event_notify(HTC_CHARGER_EVENT_SRC_INTERNAL);
		break;
	case CONNECT_TYPE_CLEAR:
		BATT_LOG("stop 5V VBUS from battery (not supported)");
		htc_charger_event_notify(HTC_CHARGER_EVENT_SRC_CLEAR);
		break;

	case CONNECT_TYPE_NONE:
		BATT_LOG("No cable exists");
		htc_charger_event_notify(HTC_CHARGER_EVENT_SRC_NONE);
		/* radio_set_cable_status(CHARGER_BATTERY); */
		break;
	case CONNECT_TYPE_MHL_AC:
		BATT_LOG("mhl_ac");
		htc_charger_event_notify(HTC_CHARGER_EVENT_SRC_MHL_AC);
		break;
	case CONNECT_TYPE_NOTIFY:
		BATT_LOG("cable insert notify");
		htc_charger_event_notify(HTC_CHARGER_EVENT_SRC_CABLE_INSERT_NOTIFY);
		break;
	default:
		BATT_LOG("unsupported connect_type=%d", online);
		htc_charger_event_notify(HTC_CHARGER_EVENT_SRC_NONE);
		/* radio_set_cable_status(CHARGER_BATTERY); */
		break;
	}
#if 0 /* MATT check porting */
	htc_batt_timer.alarm_timer_flag =
			(unsigned int)htc_batt_info.rep.charging_source;

	update_wake_lock(htc_batt_info.rep.charging_source);
#endif
	mutex_unlock(&cable_notifier_lock);
}

static int htc_battery_set_charging(int ctl)
{
	int rc = 0;
/* MATT porting
	if (htc_batt_info.charger == SWITCH_CHARGER_TPS65200)
		rc = tps_set_charger_ctrl(ctl);
*/
	return rc;
}

struct mutex chg_limit_lock;
static void set_limit_charge_with_reason(bool enable, int reason)
{
	int prev_chg_limit_reason;
#ifdef CONFIG_DUTY_CYCLE_LIMIT
	int chg_limit_current;
#endif
	mutex_lock(&chg_limit_lock);
	prev_chg_limit_reason = chg_limit_reason;
	if (chg_limit_active_mask & reason) {
		if (enable)
			chg_limit_reason |= reason;
		else
			chg_limit_reason &= ~reason;

		if (prev_chg_limit_reason ^ chg_limit_reason) {
			BATT_LOG("chg_limit_reason:0x%x->0x%d",
							prev_chg_limit_reason, chg_limit_reason);
			if (!!prev_chg_limit_reason != !!chg_limit_reason &&
					htc_batt_info.icharger &&
					htc_batt_info.icharger->set_limit_charge_enable) {
#ifdef CONFIG_DUTY_CYCLE_LIMIT
				chg_limit_current = limit_charge_timer_on != 0 ? limit_charge_timer_ma : 0;
				htc_batt_info.icharger->set_limit_charge_enable(chg_limit_reason,
						chg_limit_timer_sub_mask, chg_limit_current);
#else
				htc_batt_info.icharger->set_limit_charge_enable(!!chg_limit_reason);
#endif
			}
		}
	}
	mutex_unlock(&chg_limit_lock);
}

#ifdef CONFIG_DUTY_CYCLE_LIMIT
static void limit_chg_timer_worker(struct work_struct *work)
{
	mutex_lock(&chg_limit_lock);
	pr_info("%s: limit_chg_timer_state = %d\n", __func__, limit_chg_timer_state);
	switch (limit_chg_timer_state) {
	case LIMIT_CHG_TIMER_STATE_ON:
		if (limit_charge_timer_off) {
			limit_chg_timer_state = LIMIT_CHG_TIMER_STATE_OFF;
			schedule_delayed_work(&limit_chg_timer_work,
					round_jiffies_relative(msecs_to_jiffies
						(limit_charge_timer_off * 1000)));
			htc_batt_info.icharger->set_charger_enable(0);
		}
		break;
	case LIMIT_CHG_TIMER_STATE_OFF:
		if (limit_charge_timer_on) {
			limit_chg_timer_state = LIMIT_CHG_TIMER_STATE_ON;
			schedule_delayed_work(&limit_chg_timer_work,
					round_jiffies_relative(msecs_to_jiffies
						(limit_charge_timer_on * 1000)));
		}
		/* go through */
	case LIMIT_CHG_TIMER_STATE_NONE:
	default:
		htc_batt_info.icharger->set_charger_enable(!!htc_batt_info.rep.charging_enabled);
	}
	mutex_unlock(&chg_limit_lock);
}

static void batt_update_limited_charge_timer(int charging_enabled)
{
	bool is_schedule_timer = 0;

	if (limit_charge_timer_ma == 0 || limit_charge_timer_on == 0)
		return;

	mutex_lock(&chg_limit_lock);
	if ((charging_enabled != HTC_PWR_SOURCE_TYPE_BATT) &&
			!!(chg_limit_reason & chg_limit_timer_sub_mask)) {
		if (limit_chg_timer_state == LIMIT_CHG_TIMER_STATE_NONE) {
			limit_chg_timer_state = LIMIT_CHG_TIMER_STATE_OFF;
			is_schedule_timer = 1;
		}
	} else if (limit_chg_timer_state != LIMIT_CHG_TIMER_STATE_NONE){
		limit_chg_timer_state = LIMIT_CHG_TIMER_STATE_NONE;
		is_schedule_timer = 1;
	}

	if (is_schedule_timer)
		schedule_delayed_work(&limit_chg_timer_work, 0);
	mutex_unlock(&chg_limit_lock);
}
#endif

static void __context_event_handler(enum batt_context_event event)
{
	pr_info("[BATT] handle context event(%d)\n", event);

	switch (event) {
	case EVENT_TALK_START:
		set_limit_charge_with_reason(true, HTC_BATT_CHG_LIMIT_BIT_TALK);
		suspend_highfreq_check_reason |= SUSPEND_HIGHFREQ_CHECK_BIT_TALK;
		break;
	case EVENT_TALK_STOP:
		set_limit_charge_with_reason(false, HTC_BATT_CHG_LIMIT_BIT_TALK);
		suspend_highfreq_check_reason &= ~SUSPEND_HIGHFREQ_CHECK_BIT_TALK;
		break;
	case EVENT_NAVIGATION_START:
		set_limit_charge_with_reason(true, HTC_BATT_CHG_LIMIT_BIT_NAVI);
		break;
	case EVENT_NAVIGATION_STOP:
		set_limit_charge_with_reason(false, HTC_BATT_CHG_LIMIT_BIT_NAVI);
		break;
	case EVENT_NETWORK_SEARCH_START:
		suspend_highfreq_check_reason |= SUSPEND_HIGHFREQ_CHECK_BIT_SEARCH;
		break;
	case EVENT_NETWORK_SEARCH_STOP:
		suspend_highfreq_check_reason &= ~SUSPEND_HIGHFREQ_CHECK_BIT_SEARCH;
		break;
	case EVENT_MUSIC_START:
		suspend_highfreq_check_reason |= SUSPEND_HIGHFREQ_CHECK_BIT_MUSIC;
		break;
	case EVENT_MUSIC_STOP:
		suspend_highfreq_check_reason &= ~SUSPEND_HIGHFREQ_CHECK_BIT_MUSIC;
		break;
	default:
		pr_warn("unsupported context event (%d)\n", event);
		return;
	}

	htc_batt_schedule_batt_info_update();
}

struct mutex context_event_handler_lock; /* synchroniz context_event_handler */
static int htc_batt_context_event_handler(enum batt_context_event event)
{
	int prev_context_state;

	mutex_lock(&context_event_handler_lock);
	prev_context_state = context_state;
	/* STEP.1: check if state not changed then return */
	switch (event) {
	case EVENT_TALK_START:
		if (context_state & CONTEXT_STATE_BIT_TALK)
			goto exit;
		context_state |= CONTEXT_STATE_BIT_TALK;
		break;
	case EVENT_TALK_STOP:
		if (!(context_state & CONTEXT_STATE_BIT_TALK))
			goto exit;
		context_state &= ~CONTEXT_STATE_BIT_TALK;
		break;
	case EVENT_NETWORK_SEARCH_START:
		if (context_state & CONTEXT_STATE_BIT_SEARCH)
			goto exit;
		context_state |= CONTEXT_STATE_BIT_SEARCH;
		break;
	case EVENT_NETWORK_SEARCH_STOP:
		if (!(context_state & CONTEXT_STATE_BIT_SEARCH))
			goto exit;
		context_state &= ~CONTEXT_STATE_BIT_SEARCH;
		break;
	case EVENT_NAVIGATION_START:
		if (context_state & CONTEXT_STATE_BIT_NAVIGATION)
			goto exit;
		context_state |= CONTEXT_STATE_BIT_NAVIGATION;
		break;
	case EVENT_NAVIGATION_STOP:
		if (!(context_state & CONTEXT_STATE_BIT_NAVIGATION))
			goto exit;
		context_state &= ~CONTEXT_STATE_BIT_NAVIGATION;
		break;
	case EVENT_MUSIC_START:
		if (context_state & CONTEXT_STATE_BIT_MUSIC)
			goto exit;
		context_state |= CONTEXT_STATE_BIT_MUSIC;
		break;
	case EVENT_MUSIC_STOP:
		if (!(context_state & CONTEXT_STATE_BIT_MUSIC))
			goto exit;
		context_state &= ~CONTEXT_STATE_BIT_MUSIC;
		break;
	default:
		pr_warn("unsupported context event (%d)\n", event);
		goto exit;
	}
	BATT_LOG("context_state: 0x%x -> 0x%x", prev_context_state, context_state);

	/* STEP.2: handle incoming event */
	__context_event_handler(event);

exit:
	mutex_unlock(&context_event_handler_lock);
	return 0;
}

static int htc_batt_charger_control(enum charger_control_flag control)
{
	int ret = 0;

	BATT_LOG("%s: user switch charger to mode: %u", __func__, control);

	switch (control) {
		case STOP_CHARGER:
			chg_dis_user_timer = 1;
			break;
		case ENABLE_CHARGER:
			chg_dis_user_timer = 0;
			break;
		case DISABLE_PWRSRC:
			pwrsrc_dis_reason |= HTC_BATT_PWRSRC_DIS_BIT_API;
			break;
		case ENABLE_PWRSRC:
			pwrsrc_dis_reason &= ~HTC_BATT_PWRSRC_DIS_BIT_API;
			break;

		case ENABLE_LIMIT_CHARGER:
		case DISABLE_LIMIT_CHARGER:
			BATT_LOG("%s: skip charger_contorl(%d)", __func__, control);
			return ret;
			break;

		default:
			BATT_LOG("%s: unsupported charger_contorl(%d)", __func__, control);
			ret =  -1;
			break;

	}

	htc_batt_schedule_batt_info_update();

	return ret;
}

static void htc_batt_set_full_level(int percent)
{
	if (percent < 0)
		htc_batt_info.rep.full_level = 0;
	else if (100 < percent)
		htc_batt_info.rep.full_level = 100;
	else
		htc_batt_info.rep.full_level = percent;

	BATT_LOG(" set full_level constraint as %d.", percent);

	return;
}

static void htc_batt_set_full_level_dis_batt_chg(int percent)
{
	if (percent < 0)
		htc_batt_info.rep.full_level_dis_batt_chg = 0;
	else if (100 < percent)
		htc_batt_info.rep.full_level_dis_batt_chg = 100;
	else
		htc_batt_info.rep.full_level_dis_batt_chg = percent;

	BATT_LOG(" set full_level_dis_batt_chg constraint as %d.", percent);

	return;
}

static void htc_batt_trigger_store_battery_data(int triggle_flag)
{
	if (triggle_flag == 1)
	{
		if (htc_batt_info.igauge &&
				htc_batt_info.igauge->store_battery_data) {
			htc_batt_info.igauge->store_battery_data();
		}
	}
	return;
}

static void htc_batt_store_battery_ui_soc(int soc_ui)
{
	if (soc_ui <= 0 || soc_ui > 100)
		return;

	if (htc_batt_info.igauge &&
			htc_batt_info.igauge->store_battery_ui_soc) {
		htc_batt_info.igauge->store_battery_ui_soc(soc_ui);
	}
	return;
}

static void htc_batt_get_battery_ui_soc(int *soc_ui)
{
	int temp_soc;

	if (htc_batt_info.igauge &&
			htc_batt_info.igauge->get_battery_ui_soc) {
			temp_soc = htc_batt_info.igauge->get_battery_ui_soc();

			/* Only use stored_soc value between 1%~100% due to default eMMC misc is empty*/
			if (temp_soc > 0 && temp_soc <= 100)
				*soc_ui = temp_soc;
	}

	return;
}

static int htc_battery_get_rt_attr(enum htc_batt_rt_attr attr, int *val)
{
	int ret = -EINVAL;
	switch (attr) {
	case HTC_BATT_RT_VOLTAGE:
		if (htc_batt_info.igauge->get_battery_voltage)
			ret = htc_batt_info.igauge->get_battery_voltage(val);
		break;
	case HTC_BATT_RT_CURRENT:
		if (htc_batt_info.igauge->get_battery_current)
			ret = htc_batt_info.igauge->get_battery_current(val);
		break;
	case HTC_BATT_RT_TEMPERATURE:
		if (htc_batt_info.igauge->get_battery_temperature)
			ret = htc_batt_info.igauge->get_battery_temperature(val);
		break;
	case HTC_BATT_RT_VOLTAGE_UV:
		if (htc_batt_info.igauge->get_battery_voltage) {
			ret = htc_batt_info.igauge->get_battery_voltage(val);
			*val *= 1000;
		}
		break;
	default:
		break;
	}
	return ret;
}

static ssize_t htc_battery_show_batt_attr(struct device_attribute *attr,
					char *buf)
{
	int len = 0;

	/* collect htc_battery vars */
	len += scnprintf(buf + len, PAGE_SIZE - len,
			"charging_source: %d;\n"
			"charging_enabled: %d;\n"
			"overload: %d;\n"
			"Percentage(%%): %d;\n"
			"Percentage_raw(%%): %d;\n"
			"htc_extension: 0x%x;\n",
			htc_batt_info.rep.charging_source,
			htc_batt_info.rep.charging_enabled,
			htc_batt_info.rep.overload,
			htc_batt_info.rep.level,
			htc_batt_info.rep.level_raw,
			htc_batt_info.htc_extension
			);

	/* collect gague vars */
	if (htc_batt_info.igauge) {
#if 0
		if (htc_batt_info.igauge->name)
			len += scnprintf(buf + len, PAGE_SIZE - len,
				"gauge: %s;\n", htc_batt_info.igauge->name);
#endif
		if (htc_batt_info.igauge->get_attr_text)
			len += htc_batt_info.igauge->get_attr_text(buf + len,
						PAGE_SIZE - len);
	}

	/* collect charger vars */
	if (htc_batt_info.icharger) {
#if 0
		if (htc_batt_info.icharger->name)
			len += scnprintf(buf + len, PAGE_SIZE - len,
				"charger: %s;\n", htc_batt_info.icharger->name);
#endif
		if (htc_batt_info.icharger->get_attr_text)
			len += htc_batt_info.icharger->get_attr_text(buf + len,
						PAGE_SIZE - len);
	}
	return len;
}

static ssize_t htc_battery_show_cc_attr(struct device_attribute *attr,
					char *buf)
{
	int len = 0, cc_uah = 0;

	/* collect gague vars */
	if (htc_batt_info.igauge) {
		if (htc_batt_info.igauge->get_battery_cc) {
			htc_batt_info.igauge->get_battery_cc(&cc_uah);
			len += scnprintf(buf + len, PAGE_SIZE - len,
				"cc:%d\n", cc_uah);
		}
	}

	return len;
}

static int htc_batt_set_max_input_current(int target_ma)
{
		if(htc_batt_info.icharger && htc_batt_info.icharger->max_input_current) {
			htc_batt_info.icharger->max_input_current(target_ma);
			return 0;
		}
		else
			return -1;
}

static ssize_t htc_battery_show_htc_extension_attr(struct device_attribute *attr,
					char *buf)
{
	int len = 0;

	len += scnprintf(buf + len, PAGE_SIZE - len,"%d\n",
								htc_batt_info.htc_extension);

	return len;
}

static int htc_batt_open(struct inode *inode, struct file *filp)
{
	int ret = 0;

	BATT_LOG("%s: open misc device driver.", __func__);
	spin_lock(&htc_batt_info.batt_lock);

	if (!htc_batt_info.is_open)
		htc_batt_info.is_open = 1;
	else
		ret = -EBUSY;

	spin_unlock(&htc_batt_info.batt_lock);

#ifdef CONFIG_ARCH_MSM8X60_LTE
	/*Always disable cpu1 for off-mode charging.
	 * For 8x60_LTE projects only */
	if (board_mfg_mode() == 5)
		cpu_down(1);

#endif

	return ret;
}

static int htc_batt_release(struct inode *inode, struct file *filp)
{
	BATT_LOG("%s: release misc device driver.", __func__);
	spin_lock(&htc_batt_info.batt_lock);
	htc_batt_info.is_open = 0;
	spin_unlock(&htc_batt_info.batt_lock);

	return 0;
}

static int htc_batt_get_battery_info(struct battery_info_reply *htc_batt_update)
{
	htc_batt_update->batt_vol = htc_batt_info.rep.batt_vol;
	htc_batt_update->batt_id = htc_batt_info.rep.batt_id;
	htc_batt_update->batt_temp = htc_batt_info.rep.batt_temp;
/* MATT porting */
	htc_batt_update->batt_current = htc_batt_info.rep.batt_current;
#if 0
	/* report the net current injection into battery no
	 * matter charging is enable or not (may negative) */
	htc_batt_update->batt_current = htc_batt_info.rep.batt_current -
			htc_batt_info.rep.batt_discharg_current;
	htc_batt_update->batt_discharg_current =
				htc_batt_info.rep.batt_discharg_current;
#endif
	htc_batt_update->level = htc_batt_info.rep.level;
	htc_batt_update->level_raw = htc_batt_info.rep.level_raw;
	htc_batt_update->charging_source =
				htc_batt_info.rep.charging_source;
	htc_batt_update->charging_enabled =
				htc_batt_info.rep.charging_enabled;
	htc_batt_update->full_bat = htc_batt_info.rep.full_bat;
	htc_batt_update->full_level = htc_batt_info.rep.full_level;
	htc_batt_update->over_vchg = htc_batt_info.rep.over_vchg;
	htc_batt_update->temp_fault = htc_batt_info.rep.temp_fault;
	htc_batt_update->batt_state = htc_batt_info.rep.batt_state;
	htc_batt_update->cable_ready = htc_batt_info.rep.cable_ready;
	htc_batt_update->overload = htc_batt_info.rep.overload;
	return 0;
}

static int htc_batt_get_chg_status(enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		if (htc_batt_info.icharger &&
				htc_batt_info.icharger->get_charge_type)
			return htc_batt_info.icharger->get_charge_type();
		else
			break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_MAX:
		if (htc_batt_info.icharger &&
				htc_batt_info.icharger->get_chg_usb_iusbmax)
			return htc_batt_info.icharger->get_chg_usb_iusbmax();
		else
			break;
	case POWER_SUPPLY_PROP_VOLTAGE_MIN:
		if (htc_batt_info.icharger &&
				htc_batt_info.icharger->get_chg_vinmin)
			return htc_batt_info.icharger->get_chg_vinmin();
		else
			break;
	case POWER_SUPPLY_PROP_INPUT_VOLTAGE_REGULATION:
		if (htc_batt_info.icharger &&
				htc_batt_info.icharger->get_input_voltage_regulation)
			return htc_batt_info.icharger->get_input_voltage_regulation();
		else
			break;
	default:
		break;
	}
	pr_info("%s: functoin doesn't exist! psp=%d\n", __func__, psp);
	return 0;
}

static int
htc_batt_set_chg_property(enum power_supply_property psp, int val)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_INPUT_CURRENT_MAX:
		if (htc_batt_info.icharger &&
				htc_batt_info.icharger->set_chg_iusbmax)
			return htc_batt_info.icharger->set_chg_iusbmax(val);
		else
			break;
	case POWER_SUPPLY_PROP_VOLTAGE_MIN:
		if (htc_batt_info.icharger &&
				htc_batt_info.icharger->set_chg_vin_min)
			return htc_batt_info.icharger->set_chg_vin_min(val);
		else
			break;
	default:
		break;
	}
	pr_info("%s: functoin doesn't exist! psp=%d\n", __func__, psp);
	return 0;
}

static void batt_set_check_timer(u32 seconds)
{
	pr_debug("[BATT] %s(%u sec)\n", __func__, seconds);
	mod_timer(&htc_batt_timer.batt_timer,
			jiffies + msecs_to_jiffies(seconds * 1000));
}


u32 htc_batt_getmidvalue(int32_t *value)
{
	int i, j, n, len;
	len = ADC_REPLY_ARRAY_SIZE;
	for (i = 0; i < len - 1; i++) {
		for (j = i + 1; j < len; j++) {
			if (value[i] > value[j]) {
				n = value[i];
				value[i] = value[j];
				value[j] = n;
			}
		}
	}

	return value[len / 2];
}

/* MATT porting */
#if 0
static int32_t htc_batt_get_battery_adc(void)
{
	int ret = 0;
	u32 vref = 0;
	u32 battid_adc = 0;
	struct battery_adc_reply adc;

	/* Read battery voltage adc data. */
	ret = pm8058_htc_config_mpp_and_adc_read(
			adc.adc_voltage,
			ADC_REPLY_ARRAY_SIZE,
			CHANNEL_ADC_BATT_AMON,
			htc_batt_info.mpp_config->vol[XOADC_MPP],
			htc_batt_info.mpp_config->vol[PM_MPP_AIN_AMUX]);
	if (ret)
		goto get_adc_failed;
	/* Read battery current adc data. */
	ret = pm8058_htc_config_mpp_and_adc_read(
			adc.adc_current,
			ADC_REPLY_ARRAY_SIZE,
			CHANNEL_ADC_BATT_AMON,
			htc_batt_info.mpp_config->curr[XOADC_MPP],
			htc_batt_info.mpp_config->curr[PM_MPP_AIN_AMUX]);
	if (ret)
		goto get_adc_failed;
	/* Read battery temperature adc data. */
	ret = pm8058_htc_config_mpp_and_adc_read(
			adc.adc_temperature,
			ADC_REPLY_ARRAY_SIZE,
			CHANNEL_ADC_BATT_AMON,
			htc_batt_info.mpp_config->temp[XOADC_MPP],
			htc_batt_info.mpp_config->temp[PM_MPP_AIN_AMUX]);
	if (ret)
		goto get_adc_failed;
	/* Read battery id adc data. */
	ret = pm8058_htc_config_mpp_and_adc_read(
			adc.adc_battid,
			ADC_REPLY_ARRAY_SIZE,
			CHANNEL_ADC_BATT_AMON,
			htc_batt_info.mpp_config->battid[XOADC_MPP],
			htc_batt_info.mpp_config->battid[PM_MPP_AIN_AMUX]);

	vref = htc_batt_getmidvalue(adc.adc_voltage);
	battid_adc = htc_batt_getmidvalue(adc.adc_battid);

	BATT_LOG("%s , vref:%d, battid_adc:%d, battid:%d\n", __func__,  vref, battid_adc, battid_adc * 1000 / vref);

	if (ret)
		goto get_adc_failed;

	memcpy(&htc_batt_info.adc_data, &adc,
		sizeof(struct battery_adc_reply));

get_adc_failed:
	return ret;
}
#endif

static void batt_regular_timer_handler(unsigned long data)
{
	if (htc_batt_info.state & STATE_PREPARE) {
		htc_batt_info.state |= STATE_WORKQUEUE_PENDING;
		pr_info("[BATT] %s(): Skip due to htc_batt_info.state=0x%x\n",
				__func__, htc_batt_info.state);
	} else {
		htc_batt_info.state &= ~STATE_WORKQUEUE_PENDING;
		pr_debug("[BATT] %s(): Run, htc_batt_info.state=0x%x\n",
				__func__, htc_batt_info.state);
		htc_batt_schedule_batt_info_update();
	}
	/*
	wake_lock(&htc_batt_timer.battery_lock);
	queue_work(htc_batt_timer.batt_wq, &htc_batt_timer.batt_work);
	*/
}

static void batt_check_alarm_handler(struct alarm *alarm)
{
	BATT_LOG("alarm handler, but do nothing.");
	return;
}

static int bounding_fullly_charged_level(int upperbd, int current_level)
{
	static int pingpong = 1;
	int lowerbd;
	int is_input_chg_off_by_bounding = 0;

	lowerbd = upperbd - 5; /* 5% range */

	if (lowerbd < 0)
		lowerbd = 0;

	if (pingpong == 1 && upperbd <= current_level) {
		pr_info("MFG: lowerbd=%d, upperbd=%d, current=%d,"
				" pingpong:1->0 turn off\n", lowerbd, upperbd, current_level);
		is_input_chg_off_by_bounding = 1;
		pingpong = 0;
	} else if (pingpong == 0 && lowerbd < current_level) {
		pr_info("MFG: lowerbd=%d, upperbd=%d, current=%d,"
				" toward 0, turn off\n", lowerbd, upperbd, current_level);
		is_input_chg_off_by_bounding = 1;
	} else if (pingpong == 0 && current_level <= lowerbd) {
		pr_info("MFG: lowerbd=%d, upperbd=%d, current=%d,"
				" pingpong:0->1 turn on\n", lowerbd, upperbd, current_level);
		pingpong = 1;
	} else {
		pr_info("MFG: lowerbd=%d, upperbd=%d, current=%d,"
				" toward %d, turn on\n", lowerbd, upperbd, current_level, pingpong);
	}
	return is_input_chg_off_by_bounding;
}

static int bounding_fullly_charged_level_dis_batt_chg(int upperbd, int current_level)
{
	static int pingpong = 1;
	int lowerbd;
	int is_batt_chg_off_by_bounding = 0;

	lowerbd = upperbd - 5; /* 5% range */

	if (lowerbd < 0)
		lowerbd = 0;

	if (pingpong == 1 && upperbd <= current_level) {
		pr_info("[BATT] %s: lowerbd=%d, upperbd=%d, current=%d,"
				" pingpong:1->0 turn off\n", __func__, lowerbd, upperbd, current_level);
		is_batt_chg_off_by_bounding = 1;
		pingpong = 0;
	} else if (pingpong == 0 && lowerbd < current_level) {
		pr_info("[BATT] %s: lowerbd=%d, upperbd=%d, current=%d,"
				" toward 0, turn off\n", __func__, lowerbd, upperbd, current_level);
		is_batt_chg_off_by_bounding = 1;
	} else if (pingpong == 0 && current_level <= lowerbd) {
		pr_info("[BATT] %s: lowerbd=%d, upperbd=%d, current=%d,"
				" pingpong:0->1 turn on\n", __func__, lowerbd, upperbd, current_level);
		pingpong = 1;
	} else {
		pr_info("[BATT] %s: lowerbd=%d, upperbd=%d, current=%d,"
				" toward %d, turn on\n", __func__, lowerbd, upperbd, current_level, pingpong);
	}
	return is_batt_chg_off_by_bounding;
}

static inline int is_bounding_fully_charged_level(void)
{
	if (0 < htc_batt_info.rep.full_level &&
			htc_batt_info.rep.full_level < 100)
		return bounding_fullly_charged_level(
				htc_batt_info.rep.full_level, htc_batt_info.rep.level);
	return 0;
}

static inline int is_bounding_fully_charged_level_dis_batt_chg(void)
{
	if (0 < htc_batt_info.rep.full_level_dis_batt_chg &&
			htc_batt_info.rep.full_level_dis_batt_chg < 100)
		return bounding_fullly_charged_level_dis_batt_chg(
				htc_batt_info.rep.full_level_dis_batt_chg, htc_batt_info.rep.level);
	return 0;
}

static void batt_update_info_from_charger(void)
{
	if (!htc_batt_info.icharger) {
		BATT_LOG("warn: charger interface is not hooked.");
		return;
	}

	if (htc_batt_info.icharger->is_batt_temp_fault_disable_chg)
		htc_batt_info.icharger->is_batt_temp_fault_disable_chg(
				&charger_dis_temp_fault);

	if (htc_batt_info.icharger->is_under_rating)
		htc_batt_info.icharger->is_under_rating(
				&charger_under_rating);

	if (htc_batt_info.icharger->is_safty_timer_timeout)
		htc_batt_info.icharger->is_safty_timer_timeout(
				&charger_safety_timeout);

	if (htc_batt_info.icharger->is_battery_full_eoc_stop)
		htc_batt_info.icharger->is_battery_full_eoc_stop(
				&batt_full_eoc_stop);
}

static void batt_update_info_from_gauge(void)
{
	if (!htc_batt_info.igauge) {
		BATT_LOG("warn: gauge interface is not hooked.");
		return;
	}

	/* STEP 1: read basic battery info */
	/* get voltage */
	if (htc_batt_info.igauge->get_battery_voltage)
		htc_batt_info.igauge->get_battery_voltage(
				&htc_batt_info.rep.batt_vol);
	/* get current */
	if (htc_batt_info.igauge->get_battery_current)
		htc_batt_info.igauge->get_battery_current(
				&htc_batt_info.rep.batt_current);
	/* get temperature */
	if (htc_batt_info.igauge->get_battery_temperature)
		htc_batt_info.igauge->get_battery_temperature(
				&htc_batt_info.rep.batt_temp);
	/* get temperature fault */
	if (htc_batt_info.igauge->is_battery_temp_fault)
		htc_batt_info.igauge->is_battery_temp_fault(
				&htc_batt_info.rep.temp_fault);
	/* get batt_id */
	if (htc_batt_info.igauge->get_battery_id)
		htc_batt_info.igauge->get_battery_id(
				&htc_batt_info.rep.batt_id);

	/* battery id check and cell assginment
	htc_batt_info.bcell = htc_battery_cell_find(batt_id_raw);
	htc_battery_cell_set_cur_cell(htc_batt_info.bcell);
	htc_batt_info.rep.batt_id = htc_batt_info.bcell->id;
	*/
	/* MATT: review the definition */
	if (htc_battery_cell_get_cur_cell())
		htc_batt_info.rep.full_bat = htc_battery_cell_get_cur_cell()->capacity;

	htc_batt_info.igauge->get_battery_soc(
		&htc_batt_info.rep.level_raw);
	htc_batt_info.rep.level = htc_batt_info.rep.level_raw;
	/* get charger ovp state */
	if (htc_batt_info.icharger->is_ovp)
		htc_batt_info.icharger->is_ovp(&htc_batt_info.rep.over_vchg);

	if (htc_batt_info.igauge->check_soc_for_sw_ocv)
		htc_batt_info.igauge->check_soc_for_sw_ocv();
}

inline static int is_voltage_critical_low(int voltage_mv)
{
	return (voltage_mv < htc_batt_info.critical_low_voltage_mv) ? 1 : 0;
}

#define CHG_ONE_PERCENT_LIMIT_PERIOD_MS	(1000 * 60)
static void batt_check_overload(unsigned long time_since_last_update_ms)
{
	static unsigned int overload_count;
	static unsigned long time_accumulation;
	int is_full = 0;

	if(htc_batt_info.igauge && htc_batt_info.igauge->is_battery_full)
		htc_batt_info.igauge->is_battery_full(&is_full);

	pr_debug("[BATT] Chk overload by CS=%d V=%d I=%d count=%d overload=%d "
			"is_full=%d\n",
			htc_batt_info.rep.charging_source, htc_batt_info.rep.batt_vol,
			htc_batt_info.rep.batt_current, overload_count, htc_batt_info.rep.overload,
			is_full);
	if ((htc_batt_info.rep.charging_source > 0) &&
			(!is_full) &&
			((htc_batt_info.rep.batt_current / 1000) >
				htc_batt_info.overload_curr_thr_ma)) {
		time_accumulation += time_since_last_update_ms;
		if (time_accumulation >= CHG_ONE_PERCENT_LIMIT_PERIOD_MS) {
			if (overload_count++ < 3) {
				htc_batt_info.rep.overload = 0;
			} else
				htc_batt_info.rep.overload = 1;

			time_accumulation = 0;
		}
	} else { /* Cable is removed */
			overload_count = 0;
			time_accumulation = 0;
			htc_batt_info.rep.overload = 0;
	}
}

static void batt_check_critical_low_level(int *dec_level, int batt_current)
{
	int	i;

	for(i = 0; i < DEC_LEVEL_CURR_TABLE_SIZE; i++) {
		if (batt_current > dec_level_curr_table[i].threshold_ua) {
			*dec_level = dec_level_curr_table[i].dec_level;

			pr_debug("%s: i=%d, dec_level=%d, threshold_ua=%d\n",
				__func__, i, *dec_level, dec_level_curr_table[i].threshold_ua);
			break;
		}
	}
}

static void adjust_store_level(int *store_level, int drop_raw, int drop_ui, int prev) {
	int store = *store_level;
	/* To calculate next store_vale between UI and Raw level*/
	store += drop_raw - drop_ui;
	if (store >= 0)
		htc_batt_info.rep.level = prev - drop_ui;
	else {
		htc_batt_info.rep.level = prev;
		store += drop_ui;
	}
	*store_level = store;
}

#define DISCHG_UPDATE_PERIOD_MS			(1000 * 60)
#define ONE_PERCENT_LIMIT_PERIOD_MS		(1000 * (60 + 10))
#define FIVE_PERCENT_LIMIT_PERIOD_MS	(1000 * (300 + 10))
#define ONE_MINUTES_MS					(1000 * (60 + 10))
#define FOURTY_MINUTES_MS				(1000 * (2400 + 10))
#define SIXTY_MINUTES_MS				(1000 * (3600 + 10))
static void batt_level_adjust(unsigned long time_since_last_update_ms)
{
	static int first = 1;
	static int critical_low_enter = 0;
	static int store_level = 0;
	static int pre_ten_digit, ten_digit;
	static bool stored_level_flag = false;
	static bool allow_drop_one_percent_flag = false;
	int prev_raw_level, drop_raw_level;
	int prev_level;
	int is_full = 0, dec_level = 0;
	int dropping_level;
	static unsigned long time_accumulated_level_change = 0;
	const struct battery_info_reply *prev_batt_info_rep =
						htc_battery_core_get_batt_info_rep();

	/* FIXME
	if (prev_batt_info_rep->batt_state) */
	if (!first) {
		prev_level = prev_batt_info_rep->level;
		prev_raw_level = prev_batt_info_rep->level_raw;
	} else {
		prev_level = htc_batt_info.rep.level;
		prev_raw_level = htc_batt_info.rep.level_raw;
		pre_ten_digit = htc_batt_info.rep.level / 10;
	}
	drop_raw_level = prev_raw_level - htc_batt_info.rep.level_raw;
	time_accumulated_level_change += time_since_last_update_ms;

	if ((prev_batt_info_rep->charging_source > 0) &&
		htc_batt_info.rep.charging_source == 0 && prev_level == 100) {
		BATT_LOG("%s: Cable plug out when level 100, reset timer.",__func__);
		time_accumulated_level_change = 0;
		htc_batt_info.rep.level = prev_level;
		return;
	}

	/* In discharging case, to store the very first difference
	 * between UI and Raw level.
	 */
	if ((htc_batt_info.rep.charging_source == 0)
			&& (stored_level_flag == false)) {
		store_level = prev_level - prev_raw_level;
		BATT_LOG("%s: Cable plug out, to store difference between"
			" UI & SOC. store_level:%d, prev_level:%d, prev_raw_level:%d"
			,__func__, store_level, prev_level, prev_raw_level);
		stored_level_flag = true;
	} else if (htc_batt_info.rep.charging_source > 0)
		stored_level_flag = false;

	/* Let it enter charging state to prevent reporting 100% directly
	   if soc is 100% if cable is just inserted or boot up with cable in. */
	if (!prev_batt_info_rep->charging_enabled &&
			!((prev_batt_info_rep->charging_source == 0) &&
				htc_batt_info.rep.charging_source > 0)) {
		/* battery discharging - level smoothen algorithm:
		 * Rule: always report 1% before report 0%
		 * IF VBATT < CRITICAL_LOW_VOLTAGE THEN
		 *		drop level by 6%
		 * ELSE
		 * - level cannot drop over 2% in 1 minute (here use 60 + 10 sec).
		 * - level cannot drop over 5% in 5 minute (here use 300 + 10 sec).
		 * - level cannot drop over 3% in 60 minute.
		 * - level cannot increase while discharging.
		 */
		if (time_accumulated_level_change < DISCHG_UPDATE_PERIOD_MS) {
			/* level should keep the previous one */
			BATT_LOG("%s: total_time since last batt level update = %lu ms.",
			__func__, time_accumulated_level_change);
			htc_batt_info.rep.level = prev_level;
			store_level += drop_raw_level;
			return;
		}

		if (is_voltage_critical_low(htc_batt_info.rep.batt_vol)) {
			critical_low_enter = 1;
			/* batt voltage is under critical low condition */
			if (htc_batt_info.decreased_batt_level_check)
				batt_check_critical_low_level(&dec_level,
					htc_batt_info.rep.batt_current);
			else
				dec_level = 6;

			htc_batt_info.rep.level =
					(prev_level - dec_level > 0) ? (prev_level - dec_level) :	0;

			pr_info("[BATT] battery level force decreses %d%% from %d%%"
					" (soc=%d)on critical low (%d mV)(%d mA)\n", dec_level, prev_level,
						htc_batt_info.rep.level, htc_batt_info.critical_low_voltage_mv,
						htc_batt_info.rep.batt_current);
		} else {
			/* Always allows to drop stored 1% below 20% */
			/* Allows to drop stored 1% when UI - Raw > 10 */
			if ((htc_batt_info.rep.level_raw < 20) ||
					(prev_level - prev_raw_level > 10))
				allow_drop_one_percent_flag = true;

			/* Preset the UI Level as Pre-UI */
			htc_batt_info.rep.level = prev_level;

			if (time_since_last_update_ms <= ONE_PERCENT_LIMIT_PERIOD_MS) {
				if (1 <= drop_raw_level) {
					adjust_store_level(&store_level, drop_raw_level, 1, prev_level);
					pr_info("[BATT] remap: normal soc drop = %d%% in %lu ms."
							" UI only allow -1%%, store_level:%d, ui:%d%%\n",
							drop_raw_level, time_since_last_update_ms,
							store_level, htc_batt_info.rep.level);
				}
			} else if ((chg_limit_reason & HTC_BATT_CHG_LIMIT_BIT_TALK) &&
				(time_since_last_update_ms <= FIVE_PERCENT_LIMIT_PERIOD_MS)) {
				if (5 < drop_raw_level) {
					adjust_store_level(&store_level, drop_raw_level, 5, prev_level);
				} else if (1 <= drop_raw_level && drop_raw_level <= 5) {
					adjust_store_level(&store_level, drop_raw_level, 1, prev_level);
				}
				pr_info("[BATT] remap: phone soc drop = %d%% in %lu ms."
						" UI only allow -1%% or -5%%, store_level:%d, ui:%d%%\n",
						drop_raw_level, time_since_last_update_ms,
						store_level, htc_batt_info.rep.level);
			} else {
				if (1 <= drop_raw_level) {
					if ((ONE_MINUTES_MS < time_since_last_update_ms) &&
							(time_since_last_update_ms <= FOURTY_MINUTES_MS)) {
						adjust_store_level(&store_level, drop_raw_level, 1, prev_level);
					} else if ((FOURTY_MINUTES_MS < time_since_last_update_ms) &&
							(time_since_last_update_ms <= SIXTY_MINUTES_MS)) {
						if (2 <= drop_raw_level) {
							adjust_store_level(&store_level, drop_raw_level, 2, prev_level);
						} else { // drop_raw_level == 1
							adjust_store_level(&store_level, drop_raw_level, 1, prev_level);
						}
					} else if (SIXTY_MINUTES_MS < time_since_last_update_ms) {
						if (3 <= drop_raw_level) {
							adjust_store_level(&store_level, drop_raw_level, 3, prev_level);
						} else if (drop_raw_level == 2) {
							adjust_store_level(&store_level, drop_raw_level, 2, prev_level);
						} else { // drop_raw_level == 1
							adjust_store_level(&store_level, drop_raw_level, 1, prev_level);
						}
					}
					pr_info("[BATT] remap: suspend soc drop: %d%% in %lu ms."
							" UI only allow -1%% to -3%%, store_level:%d, ui:%d%%\n",
							drop_raw_level, time_since_last_update_ms,
							store_level, htc_batt_info.rep.level);
				}
			}

			if ((allow_drop_one_percent_flag == false)
					&& (drop_raw_level == 0)) {
				htc_batt_info.rep.level = prev_level;
				pr_info("[BATT] remap: no soc drop and no additional 1%%,"
						" ui:%d%%\n", htc_batt_info.rep.level);
			} else if ((allow_drop_one_percent_flag == true)
					&& (drop_raw_level == 0)
					&& (store_level > 0)) {
				store_level--;
				htc_batt_info.rep.level = prev_level - 1;
				allow_drop_one_percent_flag = false;
				pr_info("[BATT] remap: drop additional 1%%. store_level:%d,"
						" ui:%d%%\n", store_level
						, htc_batt_info.rep.level);
			} else if (drop_raw_level < 0) {
				/* soc increased in discharging state:
				 * do not allow level increase. */
				if (critical_low_enter) {
					pr_warn("[BATT] remap: level increase because of"
							" exit critical_low!\n");
				}
				store_level += drop_raw_level;
				htc_batt_info.rep.level = prev_level;
				pr_info("[BATT] remap: soc increased. store_level:%d,"
						" ui:%d%%\n", store_level, htc_batt_info.rep.level);
			}

			/* Allow to minus additional 1% in every 10% */
			ten_digit = htc_batt_info.rep.level / 10;
			if (htc_batt_info.rep.level != 100) {
				/* In every 10% */
				if ((pre_ten_digit != 10) && (pre_ten_digit > ten_digit)) {
					allow_drop_one_percent_flag = true;
					pr_info("[BATT] remap: allow to drop additional 1%% at next"
							" level:%d%%.\n", htc_batt_info.rep.level - 1);
				}
			}
			pre_ten_digit = ten_digit;

			if (critical_low_enter) {
				critical_low_enter = 0;
				pr_warn("[BATT] exit critical_low without charge!\n");
			}

			/* To reduce the difference between UI & SOC
			 * while in low temperature condition & no drop_raw_level */
			if (htc_batt_info.rep.batt_temp < 0 &&
				drop_raw_level == 0 &&
				store_level >= 2) {
				dropping_level = prev_level - htc_batt_info.rep.level;
				if((dropping_level == 1) || (dropping_level == 0)) {
					store_level = store_level - (2 - dropping_level);
					htc_batt_info.rep.level = htc_batt_info.rep.level -
						(2 - dropping_level);
				}
				pr_info("[BATT] remap: enter low temperature section, "
						"store_level:%d%%, dropping_level:%d%%, "
						"prev_level:%d%%, level:%d%%.\n"
						, store_level, prev_level, dropping_level
						, htc_batt_info.rep.level);
			}
		}
		/* always report 1% before report 0% in discharging stage
		    for entering quick boot off first rather than real off */
		if ((htc_batt_info.rep.level == 0) && (prev_level > 1)) {
			htc_batt_info.rep.level = 1;
			pr_info("[BATT] battery level forcely report %d%%"
					" since prev_level=%d%%\n",
					htc_batt_info.rep.level, prev_level);
		}
	} else {
		/* battery charging - level smoothen algorithm:
		 * IF batt is not full THEN
		 *		- restrict level less then 100
		 * ELSE
		 *		- set level = 100
		 */
		if (htc_batt_info.igauge && htc_batt_info.igauge->is_battery_full) {
			htc_batt_info.igauge->is_battery_full(&is_full);
			if (is_full != 0) {
				if (htc_batt_info.smooth_chg_full_delay_min
						&& prev_level < 100) {
					/* keep prev level while time interval is less than 180s */
					if (time_accumulated_level_change <
							(htc_batt_info.smooth_chg_full_delay_min
							* CHG_ONE_PERCENT_LIMIT_PERIOD_MS)) {
						htc_batt_info.rep.level = prev_level;
					} else {
						htc_batt_info.rep.level = prev_level + 1;
					}
				} else {
					htc_batt_info.rep.level = 100; /* update to 100% */
				}
			} else {
				if (prev_level > htc_batt_info.rep.level) {
					/* Keep pre_level because overloading case didn't happen */
					if (!htc_batt_info.rep.overload) {
						pr_info("[BATT] pre_level=%d, new_level=%d, "
							"level drop but overloading doesn't happen!\n",
								prev_level, htc_batt_info.rep.level);
						htc_batt_info.rep.level = prev_level;
					}
				}
				else if (99 < htc_batt_info.rep.level && prev_level < 100)
					htc_batt_info.rep.level = 99; /* restrict at 99% */
				else if (prev_level < htc_batt_info.rep.level) {
					if(time_since_last_update_ms >
							CHG_ONE_PERCENT_LIMIT_PERIOD_MS) {
						/* Let UI level increase at most 2% per minute, but
						     avoid level directly jumping to 100% from 98%      */
						if ((htc_batt_info.rep.level - prev_level) > 1
								&& prev_level < 98)
							htc_batt_info.rep.level = prev_level + 2;
						else
							htc_batt_info.rep.level = prev_level + 1;
					} else
						htc_batt_info.rep.level = prev_level;

					if (htc_batt_info.rep.level > 100)
						htc_batt_info.rep.level = 100;
				}
				else {
					pr_info("[BATT] pre_level=%d, new_level=%d, "
						"level would use raw level!\n",
						prev_level, htc_batt_info.rep.level);
				}
			}
		}
		critical_low_enter = 0;
		allow_drop_one_percent_flag = false;
	}

	/* To store the UI SOC into emmc while executing power off */
	htc_batt_store_battery_ui_soc(htc_batt_info.rep.level);

	/* To get the UI SOC storing in emmc while booting up at the first time*/
	if (first)
		htc_batt_get_battery_ui_soc(&htc_batt_info.rep.level);

	if (htc_batt_info.rep.level != prev_level)
		time_accumulated_level_change = 0;

	first = 0;
}


static void batt_update_limited_charge(void)
{
	if (htc_batt_info.state & STATE_EARLY_SUSPEND) {
		/* screen OFF */
		if ((!(chg_limit_reason & HTC_BATT_CHG_LIMIT_BIT_THRML)) &&
				htc_batt_info.rep.batt_temp > 450) {
			set_limit_charge_with_reason(true, HTC_BATT_CHG_LIMIT_BIT_THRML);
		} else if ((chg_limit_reason & HTC_BATT_CHG_LIMIT_BIT_THRML) &&
				htc_batt_info.rep.batt_temp <= 430) {
			set_limit_charge_with_reason(false, HTC_BATT_CHG_LIMIT_BIT_THRML);
		} else {
			/* keep */
		}
	} else {
		/* screen ON */
		if ((!(chg_limit_reason & HTC_BATT_CHG_LIMIT_BIT_THRML)) &&
				htc_batt_info.rep.batt_temp > 390) {
			set_limit_charge_with_reason(true, HTC_BATT_CHG_LIMIT_BIT_THRML);
		} else if ((chg_limit_reason & HTC_BATT_CHG_LIMIT_BIT_THRML) &&
				htc_batt_info.rep.batt_temp <= 370) {
			set_limit_charge_with_reason(false, HTC_BATT_CHG_LIMIT_BIT_THRML);
		} else {
			/* keep */
		}
	}
}

static void sw_safety_timer_check(unsigned long time_since_last_update_ms)
{

	pr_info("%s: %lu ms", __func__, time_since_last_update_ms);

	if(latest_chg_src == HTC_PWR_SOURCE_TYPE_BATT)
	{
		sw_stimer_fault = 0;
		sw_stimer_counter = 0;
	}

	/* it stop charging now, cleaer ext charger safety timer as well */
	if(!htc_batt_info.rep.charging_enabled)
		sw_stimer_counter = 0;

	/* if in charging, sum up time stamp. If not, clear the sum up */
	if((latest_chg_src == HTC_PWR_SOURCE_TYPE_AC) || (latest_chg_src == HTC_PWR_SOURCE_TYPE_9VAC))
	{
		pr_info("%s enter\n", __func__);

		/* safety timer is hit but not cable out yet; ignore below counting */
		if(sw_stimer_fault)
		{
			pr_info("%s safety timer expired\n", __func__);
			return;
		}

		sw_stimer_counter +=  time_since_last_update_ms;

		/* see if safety timer 16 hours is hit */
		if(sw_stimer_counter >= HTC_SAFETY_TIME_16_HR_IN_MS)
		{
			pr_info("%s sw_stimer_counter expired, count:%lu ms", __func__, sw_stimer_counter);

			/* stop charging by set fault bit to 1*/
			sw_stimer_fault = 1;

			/* reset timer */
			sw_stimer_counter = 0;
		}
		else
		{
			pr_debug("%s  sw_stimer_counter left: %lu ms", __func__, HTC_SAFETY_TIME_16_HR_IN_MS - sw_stimer_counter);
		}
	}

}

void update_htc_extension_state(void)
{
	/* check UNKONWN_USB_CHARGER */
	if (HTC_PWR_SOURCE_TYPE_UNKNOWN_USB == htc_batt_info.rep.charging_source)
		htc_batt_info.htc_extension |= HTC_EXT_UNKNOWN_USB_CHARGER;
	else
		htc_batt_info.htc_extension &= ~HTC_EXT_UNKNOWN_USB_CHARGER;
	/* check CHARGER_UNDER_RATING */
	if (charger_under_rating &&
		HTC_PWR_SOURCE_TYPE_AC == htc_batt_info.rep.charging_source)
		htc_batt_info.htc_extension |= HTC_EXT_CHG_UNDER_RATING;
	else
		htc_batt_info.htc_extension &= ~HTC_EXT_CHG_UNDER_RATING;
	/* check CHARGER_SAFTY_TIMEOUT */
	if (charger_safety_timeout || sw_stimer_fault)
		htc_batt_info.htc_extension |= HTC_EXT_CHG_SAFTY_TIMEOUT;
	else
		htc_batt_info.htc_extension &= ~HTC_EXT_CHG_SAFTY_TIMEOUT;

	if (batt_full_eoc_stop != 0)
		htc_batt_info.htc_extension |= HTC_EXT_CHG_FULL_EOC_STOP;
	else
		htc_batt_info.htc_extension &= ~HTC_EXT_CHG_FULL_EOC_STOP;
}

static void batt_worker(struct work_struct *work)
{
	static int first = 1;
	static int prev_pwrsrc_enabled = 1;
	static int prev_charging_enabled = 0;
	int charging_enabled = prev_charging_enabled;
	int pwrsrc_enabled = prev_pwrsrc_enabled;
	int prev_chg_src;
	unsigned long time_since_last_update_ms;
	unsigned long cur_jiffies;

	/* STEP 1: print out and reset total_time since last update */
	cur_jiffies = jiffies;
	time_since_last_update_ms = htc_batt_timer.total_time_ms +
		((cur_jiffies - htc_batt_timer.batt_system_jiffies) * MSEC_PER_SEC / HZ);
	BATT_LOG("%s: total_time since last batt update = %lu ms.",
				__func__, time_since_last_update_ms);
	htc_batt_timer.total_time_ms = 0; /* reset total time */
	htc_batt_timer.batt_system_jiffies = cur_jiffies;

	/* STEP 2: setup next batt uptate timer (can put in the last step)*/
	/* MATT move del timer here. TODO: move into set_check timer */
	del_timer_sync(&htc_batt_timer.batt_timer);
	batt_set_check_timer(htc_batt_timer.time_out);


	htc_batt_timer.batt_alarm_status = 0;
#ifdef CONFIG_HTC_BATT_ALARM
	htc_batt_timer.batt_critical_alarm_counter = 0;
#endif

	/* STEP 3: update charging_source */
	prev_chg_src = htc_batt_info.rep.charging_source;
	htc_batt_info.rep.charging_source = latest_chg_src;

	/* STEP 4: fresh battery information from gauge/charger */
	batt_update_info_from_gauge();
	batt_update_info_from_charger();

	/* STEP: battery level smoothen adjustment */
	batt_level_adjust(time_since_last_update_ms);

	/* STEP: force level=0 to trigger userspace shutdown */
	if (critical_shutdown ||
		(htc_batt_info.force_shutdown_batt_vol &&
		htc_batt_info.rep.batt_vol < htc_batt_info.force_shutdown_batt_vol)) {
		BATT_LOG("critical shutdown (set level=0 to force shutdown)");
		htc_batt_info.rep.level = 0;
		critical_shutdown = 0;
		wake_lock(&batt_shutdown_wake_lock);
		schedule_delayed_work(&shutdown_work,
				msecs_to_jiffies(BATT_CRITICAL_VOL_SHUTDOWN_DELAY_MS));
	}
	/* STEP: Set voltage alarm again if level is increased after charging */
	if (critical_alarm_level < 0 && prev_chg_src > 0 &&
			htc_batt_info.rep.charging_source == HTC_PWR_SOURCE_TYPE_BATT) {
		pr_info("[BATT] critical_alarm_level: %d -> %d\n",
				critical_alarm_level, htc_batt_info.critical_alarm_vol_cols - 1);
		critical_alarm_level= htc_batt_info.critical_alarm_vol_cols - 1;
		critical_alarm_level_set = critical_alarm_level + 1;
	}

	/* STEP: Update limited charge */
	batt_update_limited_charge();

	/* STEP: Check if overloading is happeneed during charging */
	batt_check_overload(time_since_last_update_ms);

	/* check ext charger's safety timer */
	if (need_sw_stimer)
	{
		sw_safety_timer_check(time_since_last_update_ms);
	}

	pr_debug("[BATT] context_state=0x%x, suspend_highfreq_check_reason=0x%x\n",
			context_state, suspend_highfreq_check_reason);

	/* STEP 4.1 determine if need to change 5v output , for mhl dongle or usb host */
	if (htc_batt_info.icharger &&
			htc_batt_info.icharger->enable_5v_output)
	{
		if(htc_ext_5v_output_old != htc_ext_5v_output_now)
		{
			htc_batt_info.icharger->enable_5v_output(htc_ext_5v_output_now);
			htc_ext_5v_output_old = htc_ext_5v_output_now;
		}
		pr_info("[BATT] enable_5v_output: %d\n", htc_ext_5v_output_now);
	}

	/* STEP: update htc_extension state */
	update_htc_extension_state();


	/* STEP 5: set the charger contorl depends on current status
	   batt id, batt temp, batt eoc, full_level
	   if charging source exist, determine charging_enable */
	if (htc_batt_info.rep.charging_source > 0) {
		/*  STEP 4.1. check and update chg_dis_reason */
		if (htc_batt_info.rep.batt_id == HTC_BATTERY_CELL_ID_UNKNOWN)
			chg_dis_reason |= HTC_BATT_CHG_DIS_BIT_ID; /* for disable charger */
		else
			chg_dis_reason &= ~HTC_BATT_CHG_DIS_BIT_ID;


		if (charger_safety_timeout || sw_stimer_fault)
			chg_dis_reason |= HTC_BATT_CHG_DIS_BIT_TMR;
		else
			chg_dis_reason &= ~HTC_BATT_CHG_DIS_BIT_TMR;


		if (charger_dis_temp_fault)
			chg_dis_reason |= HTC_BATT_CHG_DIS_BIT_TMP;
		else
			chg_dis_reason &= ~HTC_BATT_CHG_DIS_BIT_TMP;

		if (chg_dis_user_timer)
			chg_dis_reason |= HTC_BATT_CHG_DIS_BIT_USR_TMR;
		else
			chg_dis_reason &= ~HTC_BATT_CHG_DIS_BIT_USR_TMR;

		if (is_bounding_fully_charged_level()) {
			chg_dis_reason |= HTC_BATT_CHG_DIS_BIT_MFG;
			pwrsrc_dis_reason |= HTC_BATT_PWRSRC_DIS_BIT_MFG;
		} else {
			chg_dis_reason &= ~HTC_BATT_CHG_DIS_BIT_MFG;
			pwrsrc_dis_reason &= ~HTC_BATT_PWRSRC_DIS_BIT_MFG;
		}

		if (is_bounding_fully_charged_level_dis_batt_chg())
			chg_dis_reason |= HTC_BATT_CHG_DIS_BIT_STOP_SWOLLEN;
		else
			chg_dis_reason &= ~HTC_BATT_CHG_DIS_BIT_STOP_SWOLLEN;

		if (htc_batt_info.rep.over_vchg)
			chg_dis_reason |= HTC_BATT_CHG_DIS_BIT_OVP;
		else
			chg_dis_reason &= ~HTC_BATT_CHG_DIS_BIT_OVP;

		/* STEP 5.2.1 determin pwrsrc_eanbled for charger control */
		if (pwrsrc_dis_reason)
			pwrsrc_enabled = 0;
		else
			pwrsrc_enabled = 1;

		/* STEP 5.2.2 determin charging_eanbled for charger control */
		if (chg_dis_reason & chg_dis_control_mask)
			charging_enabled = HTC_PWR_SOURCE_TYPE_BATT;
		else
			charging_enabled = htc_batt_info.rep.charging_source;

		/* STEP 5.2.3 determin charging_eanbled for userspace */
		if (chg_dis_reason & chg_dis_active_mask)
			htc_batt_info.rep.charging_enabled = HTC_PWR_SOURCE_TYPE_BATT;
		else
			htc_batt_info.rep.charging_enabled =
										htc_batt_info.rep.charging_source;

		/* STEP 5.3. control charger if state changed */
		pr_info("[BATT] prev_chg_src=%d, prev_chg_en=%d,"
				" chg_dis_reason/control/active=0x%x/0x%x/0x%x,"
				" chg_limit_reason=0x%x,"
				" pwrsrc_dis_reason=0x%x, prev_pwrsrc_enabled=%d,"
				" context_state=0x%x,"
				" htc_extension=0x%x\n",
					prev_chg_src, prev_charging_enabled,
					chg_dis_reason,
					chg_dis_reason & chg_dis_control_mask,
					chg_dis_reason & chg_dis_active_mask,
					chg_limit_reason,
					pwrsrc_dis_reason, prev_pwrsrc_enabled,
					context_state,
					htc_batt_info.htc_extension);
		if (charging_enabled != prev_charging_enabled ||
				prev_chg_src != htc_batt_info.rep.charging_source ||
				first ||
				pwrsrc_enabled != prev_pwrsrc_enabled) {
			/* re-config charger when state changes */
			if (prev_chg_src != htc_batt_info.rep.charging_source ||
					first) {
				BATT_LOG("set_pwrsrc_and_charger_enable(%d, %d, %d)",
							htc_batt_info.rep.charging_source,
							charging_enabled,
							pwrsrc_enabled);
				if (htc_batt_info.icharger &&
						htc_batt_info.icharger->set_pwrsrc_and_charger_enable)
					htc_batt_info.icharger->set_pwrsrc_and_charger_enable(
								htc_batt_info.rep.charging_source,
								charging_enabled,
								pwrsrc_enabled);
			} else {
				if (pwrsrc_enabled != prev_pwrsrc_enabled) {
					BATT_LOG("set_pwrsrc_enable(%d)", pwrsrc_enabled);
					if (htc_batt_info.icharger &&
						htc_batt_info.icharger->set_pwrsrc_enable)
						htc_batt_info.icharger->set_pwrsrc_enable(
													pwrsrc_enabled);
				}
				if (charging_enabled != prev_charging_enabled) {
					BATT_LOG("set_charger_enable(%d)", charging_enabled);
					if (htc_batt_info.icharger &&
						htc_batt_info.icharger->set_charger_enable)
						htc_batt_info.icharger->set_charger_enable(
													charging_enabled);
				}
			}
		}
	} else {
		/* TODO: check if we need to enable batfet while unplugged */
		if (prev_chg_src != htc_batt_info.rep.charging_source || first) {
			chg_dis_reason = 0; /* reset on charger out */
			charging_enabled = 0; /* disable batfet */
			pwrsrc_enabled = 0; /* disable power source for MHL initialize */
			BATT_LOG("set_pwrsrc_and_charger_enable(%d, %d, %d)",
						HTC_PWR_SOURCE_TYPE_BATT,
						charging_enabled,
						pwrsrc_enabled);
			if (htc_batt_info.icharger &&
						htc_batt_info.icharger->set_pwrsrc_and_charger_enable)
				htc_batt_info.icharger->set_pwrsrc_and_charger_enable(
								HTC_PWR_SOURCE_TYPE_BATT,
								charging_enabled, pwrsrc_enabled);
			/* update to userspace charging_enabled state */
			htc_batt_info.rep.charging_enabled =
								htc_batt_info.rep.charging_source;
		}
	}

#ifdef CONFIG_DUTY_CYCLE_LIMIT
	batt_update_limited_charge_timer(charging_enabled);
#endif

	/* MATT:TODO STEP: get back charger status */
	if (htc_batt_info.icharger) {
		/*
		 pm8921 FSM is not changed immediately
		htc_batt_info.icharger->get_charging_enabled(
				&htc_batt_info.rep.charging_enabled);
		*/
		htc_batt_info.icharger->dump_all();
	}

	/* use notify to set batt_state=1
	htc_batt_info.rep.batt_state = 1; */

	/* STEP: update change to userspace via batt_core */
	htc_battery_core_update_changed();

	/* STEP: check and set voltage alarm */
	if (0 <= critical_alarm_level &&
					critical_alarm_level < critical_alarm_level_set) {
		critical_alarm_level_set = critical_alarm_level;
		pr_info("[BATT] set voltage alarm level=%d\n", critical_alarm_level);
		htc_batt_info.igauge->set_lower_voltage_alarm_threshold(
					htc_batt_info.critical_alarm_vol_ptr[critical_alarm_level]);
		if (htc_batt_info.igauge->enable_lower_voltage_alarm)
			htc_batt_info.igauge->enable_lower_voltage_alarm(1);
	}

	first = 0;
	prev_charging_enabled = charging_enabled;
	prev_pwrsrc_enabled = pwrsrc_enabled;

	wake_unlock(&htc_batt_timer.battery_lock);
	pr_info("[BATT] %s: done\n", __func__);
	return;
}

static long htc_batt_ioctl(struct file *filp,
			unsigned int cmd, unsigned long arg)
{
	int ret = 0;

	wake_lock(&htc_batt_timer.battery_lock);

	switch (cmd) {
	case HTC_BATT_IOCTL_READ_SOURCE: {
		if (copy_to_user((void __user *)arg,
			&htc_batt_info.rep.charging_source, sizeof(u32)))
			ret = -EFAULT;
		break;
	}
	case HTC_BATT_IOCTL_SET_BATT_ALARM: {
		u32 time_out = 0;
		if (copy_from_user(&time_out, (void *)arg, sizeof(u32))) {
			ret = -EFAULT;
			break;
		}

		htc_batt_timer.time_out = time_out;
		if (!htc_battery_initial) {
			htc_battery_initial = 1;
			batt_set_check_timer(htc_batt_timer.time_out);
		}
		break;
	}
	case HTC_BATT_IOCTL_GET_ADC_VREF: {
		if (copy_to_user((void __user *)arg, &htc_batt_info.adc_vref,
				sizeof(htc_batt_info.adc_vref))) {
			BATT_ERR("copy_to_user failed!");
			ret = -EFAULT;
		}
		break;
	}
	case HTC_BATT_IOCTL_GET_ADC_ALL: {
		if (copy_to_user((void __user *)arg, &htc_batt_info.adc_data,
					sizeof(struct battery_adc_reply))) {
			BATT_ERR("copy_to_user failed!");
			ret = -EFAULT;
		}
		break;
	}
	case HTC_BATT_IOCTL_CHARGER_CONTROL: {
		u32 charger_mode = 0;
		if (copy_from_user(&charger_mode, (void *)arg, sizeof(u32))) {
			ret = -EFAULT;
			break;
		}
		BATT_LOG("do charger control = %u", charger_mode);
		htc_battery_set_charging(charger_mode);
		break;
	}
	case HTC_BATT_IOCTL_UPDATE_BATT_INFO: {
		mutex_lock(&htc_batt_info.info_lock);
		if (copy_from_user(&htc_batt_info.rep, (void *)arg,
					sizeof(struct battery_info_reply))) {
			BATT_ERR("copy_from_user failed!");
			ret = -EFAULT;
			mutex_unlock(&htc_batt_info.info_lock);
			break;
		}
		mutex_unlock(&htc_batt_info.info_lock);

		BATT_LOG("ioctl: battery level update: %u",
			htc_batt_info.rep.level);

#ifdef CONFIG_HTC_BATT_ALARM
		/* Set a 3V voltage alarm when screen is on */
		if (screen_state == 1) {
			if (battery_vol_alarm_mode !=
				BATT_ALARM_CRITICAL_MODE)
				batt_set_voltage_alarm_mode(
					BATT_ALARM_CRITICAL_MODE);
		}
#endif
		htc_battery_core_update_changed();
		break;
	}
	case HTC_BATT_IOCTL_BATT_DEBUG_LOG:
		if (copy_from_user(htc_batt_info.debug_log, (void *)arg,
					DEBUG_LOG_LENGTH)) {
			BATT_ERR("copy debug log from user failed!");
			ret = -EFAULT;
		}
		break;
	case HTC_BATT_IOCTL_SET_VOLTAGE_ALARM: {
#ifdef CONFIG_HTC_BATT_ALARM
#else
		struct battery_vol_alarm alarm_data;

#endif
		if (copy_from_user(&alarm_data, (void *)arg,
					sizeof(struct battery_vol_alarm))) {
			BATT_ERR("user set batt alarm failed!");
			ret = -EFAULT;
			break;
		}

		htc_batt_timer.batt_alarm_status = 0;
		htc_batt_timer.batt_alarm_enabled = alarm_data.enable;

/* MATT porting
#ifdef CONFIG_HTC_BATT_ALARM
#else
		ret = batt_alarm_config(alarm_data.lower_threshold,
				alarm_data.upper_threshold);
		if (ret)
			BATT_ERR("batt alarm config failed!");
#endif
*/

		BATT_LOG("Set lower threshold: %d, upper threshold: %d, "
			"Enabled:%u.", alarm_data.lower_threshold,
			alarm_data.upper_threshold, alarm_data.enable);
		break;
	}
	case HTC_BATT_IOCTL_SET_ALARM_TIMER_FLAG: {
		/* alarm flag could be reset by cable. */
		unsigned int flag;
		if (copy_from_user(&flag, (void *)arg, sizeof(unsigned int))) {
			BATT_ERR("Set timer type into alarm failed!");
			ret = -EFAULT;
			break;
		}
		htc_batt_timer.alarm_timer_flag = flag;
		BATT_LOG("Set alarm timer flag:%u", flag);
		break;
	}
	default:
		BATT_ERR("%s: no matched ioctl cmd", __func__);
		break;
	}

	wake_unlock(&htc_batt_timer.battery_lock);

	return ret;
}

/* shutdown worker */
static void shutdown_worker(struct work_struct *work)
{
	BATT_LOG("shutdown device");
	kernel_power_off();
	wake_unlock(&batt_shutdown_wake_lock);
}

/*  MBAT_IN interrupt handler	*/
static void mbat_in_func(struct work_struct *work)
{
#if defined(CONFIG_MACH_RUBY) || defined(CONFIG_MACH_HOLIDAY) || defined(CONFIG_MACH_VIGOR)
	/* add sw debounce */
#define LTE_GPIO_MBAT_IN (61)
	if (gpio_get_value(LTE_GPIO_MBAT_IN) == 0) {
		pr_info("re-enable MBAT_IN irq!! due to false alarm\n");
		enable_irq(MSM_GPIO_TO_INT(LTE_GPIO_MBAT_IN));
		return;
	}
#endif

	BATT_LOG("shut down device due to MBAT_IN interrupt");
	htc_battery_set_charging(0);
	machine_power_off();

}
/* MATT porting */
#if 0
static irqreturn_t mbat_int_handler(int irq, void *data)
{
	struct htc_battery_platform_data *pdata = data;

	disable_irq_nosync(pdata->gpio_mbat_in);

	schedule_delayed_work(&mbat_in_struct, msecs_to_jiffies(50));

	return IRQ_HANDLED;
}
/*  MBAT_IN interrupt handler end   */
#endif

const struct file_operations htc_batt_fops = {
	.owner = THIS_MODULE,
	.open = htc_batt_open,
	.release = htc_batt_release,
	.unlocked_ioctl = htc_batt_ioctl,
};

static struct miscdevice htc_batt_device_node = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "htc_batt",
	.fops = &htc_batt_fops,
};

static void htc_batt_kobject_release(struct kobject *kobj)
{
	printk(KERN_ERR "htc_batt_kobject_release.\n");
	return;
}

static struct kobj_type htc_batt_ktype = {
	.release = htc_batt_kobject_release,
};

#if defined(CONFIG_FB)
static int fb_notifier_callback(struct notifier_block *self,
				unsigned long event, void *data)
{
	struct fb_event *evdata = data;
	int *blank;

	if (evdata && evdata->data && event == FB_EVENT_BLANK) {
		blank = evdata->data;
		switch (*blank) {
			case FB_BLANK_UNBLANK:
				htc_batt_info.state &= ~STATE_EARLY_SUSPEND;
				BATT_LOG("%s-> display is On", __func__);
				htc_batt_schedule_batt_info_update();
				break;
			case FB_BLANK_POWERDOWN:
			case FB_BLANK_HSYNC_SUSPEND:
			case FB_BLANK_VSYNC_SUSPEND:
			case FB_BLANK_NORMAL:
				htc_batt_info.state |= STATE_EARLY_SUSPEND;
				BATT_LOG("%s-> display is Off", __func__);
				htc_batt_schedule_batt_info_update();
				break;
		}
	}

	return 0;
}
#elif defined(CONFIG_HAS_EARLYSUSPEND)
static void htc_battery_early_suspend(struct early_suspend *h)
{
	htc_batt_info.state |= STATE_EARLY_SUSPEND;
#ifdef CONFIG_HTC_BATT_ALARM
	screen_state = 0;
	batt_set_voltage_alarm_mode(BATT_ALARM_DISABLE_MODE);
#endif
	htc_batt_schedule_batt_info_update();
	return;
}

static void htc_battery_late_resume(struct early_suspend *h)
{
	htc_batt_info.state &= ~STATE_EARLY_SUSPEND;
#ifdef CONFIG_HTC_BATT_ALARM
	screen_state = 1;
	batt_set_voltage_alarm_mode(BATT_ALARM_CRITICAL_MODE);
#endif
	htc_batt_schedule_batt_info_update();
}
#endif /* CONFIG_HAS_EARLYSUSPEND */

#define CHECH_TIME_TOLERANCE_MS	(1000)
static int htc_battery_prepare(struct device *dev)
{
	ktime_t interval;
	ktime_t slack = ktime_set(0, 0);
	ktime_t next_alarm;
	struct timespec xtime;
	unsigned long cur_jiffies;
	s64 next_alarm_sec = 0;
	int check_time = 0;

	htc_batt_info.state |= STATE_PREPARE;
	xtime = CURRENT_TIME;
	cur_jiffies = jiffies;
	htc_batt_timer.total_time_ms += (cur_jiffies -
			htc_batt_timer.batt_system_jiffies) * MSEC_PER_SEC / HZ;
	htc_batt_timer.batt_system_jiffies = cur_jiffies;
	htc_batt_timer.batt_suspend_ms = xtime.tv_sec * MSEC_PER_SEC +
					xtime.tv_nsec / NSEC_PER_MSEC;

	if (suspend_highfreq_check_reason)
		check_time = BATT_SUSPEND_HIGHFREQ_CHECK_TIME;
	else
		check_time = BATT_SUSPEND_CHECK_TIME;

	interval = ktime_set(check_time - htc_batt_timer.total_time_ms / 1000, 0);
	next_alarm_sec = div_s64(interval.tv64, NSEC_PER_SEC);
	/* check if alarm is over time or in 1 second near future */
	if (next_alarm_sec <= 1) {
		BATT_LOG("%s: passing time:%lu ms, trigger batt_work immediately."
			"(suspend_highfreq_check_reason=0x%x)", __func__,
			htc_batt_timer.total_time_ms,
			suspend_highfreq_check_reason);
		htc_batt_schedule_batt_info_update();
		/* interval = ktime_set(check_time, 0);
		next_alarm = ktime_add(alarm_get_elapsed_realtime(), interval);
		alarm_start_range(&htc_batt_timer.batt_check_wakeup_alarm,
					next_alarm, ktime_add(next_alarm, slack));
		*/
		return -EBUSY;
	}

	BATT_LOG("%s: passing time:%lu ms, alarm will be triggered after %lld sec."
		"(suspend_highfreq_check_reason=0x%x, htc_batt_info.state=0x%x)",
		__func__, htc_batt_timer.total_time_ms, next_alarm_sec,
		suspend_highfreq_check_reason, htc_batt_info.state);

	next_alarm = ktime_add(alarm_get_elapsed_realtime(), interval);
	alarm_start_range(&htc_batt_timer.batt_check_wakeup_alarm,
				next_alarm, ktime_add(next_alarm, slack));

	return 0;
}

static void htc_battery_complete(struct device *dev)
{
	unsigned long resume_ms;
	unsigned long sr_time_period_ms;
	unsigned long check_time;
	struct timespec xtime;

	htc_batt_info.state &= ~STATE_PREPARE;
	xtime = CURRENT_TIME;
	htc_batt_timer.batt_system_jiffies = jiffies;
	resume_ms = xtime.tv_sec * MSEC_PER_SEC + xtime.tv_nsec / NSEC_PER_MSEC;
	sr_time_period_ms = resume_ms - htc_batt_timer.batt_suspend_ms;
	htc_batt_timer.total_time_ms += sr_time_period_ms;

	BATT_LOG("%s: sr_time_period=%lu ms; total passing time=%lu ms."
			"htc_batt_info.state=0x%x",
			__func__, sr_time_period_ms, htc_batt_timer.total_time_ms,
			htc_batt_info.state);

	if (suspend_highfreq_check_reason)
		check_time = BATT_SUSPEND_HIGHFREQ_CHECK_TIME * MSEC_PER_SEC;
	else
		check_time = BATT_SUSPEND_CHECK_TIME * MSEC_PER_SEC;

	check_time -= CHECH_TIME_TOLERANCE_MS;

	/*
	 * When kernel resumes, battery driver should check total time to
	 * decide if do battery information update or just ignore.
	 */
	if (htc_batt_timer.total_time_ms >= check_time ||
			(htc_batt_info.state & STATE_WORKQUEUE_PENDING)) {
		htc_batt_info.state &= ~STATE_WORKQUEUE_PENDING;
		BATT_LOG("trigger batt_work while resume."
				"(suspend_highfreq_check_reason=0x%x, "
				"htc_batt_info.state=0x%x)",
				suspend_highfreq_check_reason, htc_batt_info.state);
		htc_batt_schedule_batt_info_update();
	}

	return;
}

static struct dev_pm_ops htc_battery_8960_pm_ops = {
	.prepare = htc_battery_prepare,
	.complete = htc_battery_complete,
};

#if defined(CONFIG_FB)
static void htc_battery_fb_register(struct work_struct *work)
{
	int ret = 0;

	BATT_LOG("%s in", __func__);
	htc_batt_info.fb_notif.notifier_call = fb_notifier_callback;
	ret = fb_register_client(&htc_batt_info.fb_notif);
	if (ret)
		BATT_ERR("[warning]:Unable to register fb_notifier: %d\n", ret);
}
#endif

static int htc_battery_probe(struct platform_device *pdev)
{
	int i, rc = 0;
	struct htc_battery_platform_data *pdata = pdev->dev.platform_data;
	struct htc_battery_core *htc_battery_core_ptr;

	pr_info("[BATT] %s() in\n", __func__);

	htc_battery_core_ptr = kmalloc(sizeof(struct htc_battery_core),
					GFP_KERNEL);
	if (!htc_battery_core_ptr) {
		BATT_ERR("%s: kmalloc failed for htc_battery_core_ptr.",
				__func__);
		return -ENOMEM;
	}

	memset(htc_battery_core_ptr, 0, sizeof(struct htc_battery_core));
	INIT_DELAYED_WORK(&mbat_in_struct, mbat_in_func);
	INIT_DELAYED_WORK(&shutdown_work, shutdown_worker);
/* MATT porting */
#if 0
	if (pdata->gpio_mbat_in_trigger_level == MBAT_IN_HIGH_TRIGGER)
		rc = request_irq(pdata->gpio_mbat_in,
				mbat_int_handler, IRQF_TRIGGER_HIGH,
				"mbat_in", pdata);
	else if (pdata->gpio_mbat_in_trigger_level == MBAT_IN_LOW_TRIGGER)
		rc = request_irq(pdata->gpio_mbat_in,
				mbat_int_handler, IRQF_TRIGGER_LOW,
				"mbat_in", pdata);
	if (rc)
		BATT_ERR("request mbat_in irq failed!");
	else
		set_irq_wake(pdata->gpio_mbat_in, 1);
#endif

	htc_battery_core_ptr->func_get_batt_rt_attr = htc_battery_get_rt_attr;
	htc_battery_core_ptr->func_show_batt_attr = htc_battery_show_batt_attr;
	htc_battery_core_ptr->func_show_cc_attr = htc_battery_show_cc_attr;
	htc_battery_core_ptr->func_show_htc_extension_attr =
										htc_battery_show_htc_extension_attr;
	htc_battery_core_ptr->func_get_battery_info = htc_batt_get_battery_info;
	htc_battery_core_ptr->func_charger_control = htc_batt_charger_control;
	htc_battery_core_ptr->func_set_full_level = htc_batt_set_full_level;
	htc_battery_core_ptr->func_set_max_input_current = htc_batt_set_max_input_current;
	htc_battery_core_ptr->func_set_full_level_dis_batt_chg =
											htc_batt_set_full_level_dis_batt_chg;
	htc_battery_core_ptr->func_context_event_handler =
											htc_batt_context_event_handler;
	htc_battery_core_ptr->func_notify_pnpmgr_charging_enabled =
										pdata->notify_pnpmgr_charging_enabled;
	htc_battery_core_ptr->func_get_chg_status = htc_batt_get_chg_status;
	htc_battery_core_ptr->func_set_chg_property = htc_batt_set_chg_property;
	htc_battery_core_ptr->func_trigger_store_battery_data =
											htc_batt_trigger_store_battery_data;

	htc_battery_core_register(&pdev->dev, htc_battery_core_ptr);

	htc_batt_info.device_id = pdev->id;
/* MATT porting */
#if 0
	htc_batt_info.guage_driver = pdata->guage_driver;
	htc_batt_info.charger = pdata->charger;
#endif

	htc_batt_info.is_open = 0;

	for (i = 0; i < ADC_REPLY_ARRAY_SIZE; i++)
		htc_batt_info.adc_vref[i] = 66;

	/* MATT add */
	htc_batt_info.critical_low_voltage_mv = pdata->critical_low_voltage_mv;
	if (pdata->critical_alarm_vol_ptr) {
		htc_batt_info.critical_alarm_vol_ptr = pdata->critical_alarm_vol_ptr;
		htc_batt_info.critical_alarm_vol_cols = pdata->critical_alarm_vol_cols;
		critical_alarm_level_set = htc_batt_info.critical_alarm_vol_cols - 1;
		critical_alarm_level = critical_alarm_level_set;
	}
	if (pdata->force_shutdown_batt_vol)
		htc_batt_info.force_shutdown_batt_vol = pdata->force_shutdown_batt_vol;
	htc_batt_info.overload_vol_thr_mv = pdata->overload_vol_thr_mv;
	htc_batt_info.overload_curr_thr_ma = pdata->overload_curr_thr_ma;
	htc_batt_info.smooth_chg_full_delay_min = pdata->smooth_chg_full_delay_min;
	htc_batt_info.decreased_batt_level_check = pdata->decreased_batt_level_check;
	chg_limit_active_mask = pdata->chg_limit_active_mask;
#ifdef CONFIG_DUTY_CYCLE_LIMIT
	chg_limit_timer_sub_mask = pdata->chg_limit_timer_sub_mask;
#endif
	if (pdata->igauge.name)
	htc_batt_info.igauge = &pdata->igauge;
	if (pdata->icharger.name)
	htc_batt_info.icharger = &pdata->icharger;

/* MATT porting */
#if 0
	htc_batt_info.mpp_config = &pdata->mpp_data;
#endif

	INIT_WORK(&htc_batt_timer.batt_work, batt_worker);
	INIT_DELAYED_WORK(&htc_batt_timer.unknown_usb_detect_work,
							unknown_usb_detect_worker);
	init_timer(&htc_batt_timer.batt_timer);
	htc_batt_timer.batt_timer.function = batt_regular_timer_handler;
	alarm_init(&htc_batt_timer.batt_check_wakeup_alarm,
			ANDROID_ALARM_ELAPSED_REALTIME_WAKEUP,
			batt_check_alarm_handler);
	htc_batt_timer.batt_wq = create_singlethread_workqueue("batt_timer");

#ifdef CONFIG_DUTY_CYCLE_LIMIT
	INIT_DELAYED_WORK(&limit_chg_timer_work, limit_chg_timer_worker);
#endif

	rc = misc_register(&htc_batt_device_node);
	if (rc) {
		BATT_ERR("Unable to register misc device %d",
			MISC_DYNAMIC_MINOR);
		goto fail;
	}

	htc_batt_kset = kset_create_and_add("event_to_daemon", NULL,
			kobject_get(&htc_batt_device_node.this_device->kobj));
	if (!htc_batt_kset) {
		rc = -ENOMEM;
		goto fail;
	}

	htc_batt_info.batt_timer_kobj.kset = htc_batt_kset;
	rc = kobject_init_and_add(&htc_batt_info.batt_timer_kobj,
				&htc_batt_ktype, NULL, "htc_batt_timer");
	if (rc) {
		BATT_ERR("init kobject htc_batt_timer failed.");
		kobject_put(&htc_batt_info.batt_timer_kobj);
		goto fail;
	}

	htc_batt_info.batt_cable_kobj.kset = htc_batt_kset;
	rc = kobject_init_and_add(&htc_batt_info.batt_cable_kobj,
				&htc_batt_ktype, NULL, "htc_cable_detect");
	if (rc) {
		BATT_ERR("init kobject htc_cable_timer failed.");
		kobject_put(&htc_batt_info.batt_timer_kobj);
		goto fail;
	}

	/* FIXME: workaround 8960 cable detect status is not correct
	 * at register_notfier call */
	if (htc_batt_info.icharger &&
			htc_batt_info.icharger->charger_change_notifier_register)
		htc_batt_info.icharger->charger_change_notifier_register(
											&cable_status_notifier);

	/* check if need software safety tiemr */
	if (htc_batt_info.icharger && (htc_batt_info.icharger->sw_safetytimer) &&
			!(get_kernel_flag() & KERNEL_FLAG_KEEP_CHARG_ON) &&
			!(get_kernel_flag() & KERNEL_FLAG_PA_RECHARG_TEST))
		{
			need_sw_stimer = 1;
			chg_dis_active_mask |= HTC_BATT_CHG_DIS_BIT_TMR;
			chg_dis_control_mask |= HTC_BATT_CHG_DIS_BIT_TMR;

		}

	/* remove all limit charging if KEEP_CHARG_ON flag is on */
	if((get_kernel_flag() & KERNEL_FLAG_KEEP_CHARG_ON) || (get_kernel_flag() & KERNEL_FLAG_PA_RECHARG_TEST))
	{
		chg_limit_active_mask = 0;
	}
#ifdef CONFIG_DUTY_CYCLE_LIMIT
	chg_limit_timer_sub_mask &= chg_limit_active_mask;
#endif

/* MATT porting
	if (pdata->charger == SWITCH_CHARGER_TPS65200)
		tps_register_notifier(&tps_int_notifier);
*/
/* MATT porting
	pm8058_batt_alarm_register_notifier(&battery_alarm_notifier);
*/
/* MATT porting
	rc = pm8058_htc_config_mpp_and_adc_read(
				htc_batt_info.adc_vref,
				ADC_REPLY_ARRAY_SIZE,
				CHANNEL_ADC_VCHG,
				0, 0);
	if (rc) {
		BATT_ERR("Get Vref ADC value failed!");
		goto fail;
	}

	BATT_LOG("vref ADC: %d %d %d %d %d", htc_batt_info.adc_vref[0],
					htc_batt_info.adc_vref[1],
					htc_batt_info.adc_vref[2],
					htc_batt_info.adc_vref[3],
					htc_batt_info.adc_vref[4]);

	rc = htc_batt_get_battery_adc();
	if (rc) {
		BATT_ERR("Get first battery ADC value failed!");
		goto fail;
	}
*/
#if defined(CONFIG_FB)
	htc_batt_info.batt_fb_wq = create_singlethread_workqueue("HTC_BATTERY_FB");
	if (!htc_batt_info.batt_fb_wq) {
		BATT_ERR("allocate batt_fb_wq failed\n");
		rc = -ENOMEM;
		goto fail;
	}
	INIT_DELAYED_WORK(&htc_batt_info.work_fb, htc_battery_fb_register);
	queue_delayed_work(htc_batt_info.batt_fb_wq, &htc_batt_info.work_fb, msecs_to_jiffies(30000));
#elif defined(CONFIG_HAS_EARLYSUSPEND)
	early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN - 1;
	early_suspend.suspend = htc_battery_early_suspend;
	early_suspend.resume = htc_battery_late_resume;
	register_early_suspend(&early_suspend);
#endif

/* MATT add + */
htc_batt_timer.time_out = BATT_TIMER_UPDATE_TIME;
batt_set_check_timer(htc_batt_timer.time_out);
/* MATT add - */
	BATT_LOG("htc_battery_probe(): finish");

fail:
	kfree(htc_battery_core_ptr);
	return rc;
}

static struct platform_driver htc_battery_driver = {
	.probe	= htc_battery_probe,
	.driver	= {
		.name	= "htc_battery",
		.owner	= THIS_MODULE,
		.pm = &htc_battery_8960_pm_ops,
	},
};

static int __init htc_battery_init(void)
{
	htc_battery_initial = 0;
	htc_ext_5v_output_old = 0;
	htc_ext_5v_output_now = 0;
	htc_full_level_flag = 0;
	htc_batt_info.force_shutdown_batt_vol = 0;
	spin_lock_init(&htc_batt_info.batt_lock);
	wake_lock_init(&htc_batt_info.vbus_wake_lock, WAKE_LOCK_SUSPEND,
			"vbus_present");
	wake_lock_init(&htc_batt_timer.battery_lock, WAKE_LOCK_SUSPEND,
			"htc_battery_8960");
	wake_lock_init(&htc_batt_timer.unknown_usb_detect_lock,
			WAKE_LOCK_SUSPEND, "unknown_usb_detect");
	wake_lock_init(&voltage_alarm_wake_lock, WAKE_LOCK_SUSPEND,
			"htc_voltage_alarm");
	wake_lock_init(&batt_shutdown_wake_lock, WAKE_LOCK_SUSPEND,
			"batt_shutdown");
	mutex_init(&htc_batt_info.info_lock);
	mutex_init(&htc_batt_timer.schedule_lock);
	mutex_init(&cable_notifier_lock);
	mutex_init(&chg_limit_lock);
	mutex_init(&context_event_handler_lock);
#ifdef CONFIG_HTC_BATT_ALARM
	mutex_init(&batt_set_alarm_lock);
#endif

	/* cable_detect_register_notifier(&cable_status_notifier); */

	platform_driver_register(&htc_battery_driver);

	/* init battery parameters. */
	htc_batt_info.rep.batt_vol = 3700;
	htc_batt_info.rep.batt_id = 1;
	htc_batt_info.rep.batt_temp = 250;
	htc_batt_info.rep.level = 33;
	htc_batt_info.rep.level_raw = 33;
	htc_batt_info.rep.full_bat = 1579999;
	htc_batt_info.rep.full_level = 100;
	htc_batt_info.rep.batt_state = 0;
	htc_batt_info.rep.cable_ready = 0;
	htc_batt_info.rep.temp_fault = -1;
	htc_batt_info.rep.overload = 0;
	htc_batt_timer.total_time_ms = 0;
	htc_batt_timer.batt_system_jiffies = jiffies;
	htc_batt_timer.batt_alarm_status = 0;
	htc_batt_timer.alarm_timer_flag = 0;

#ifdef CONFIG_HTC_BATT_ALARM
	battery_vol_alarm_mode = BATT_ALARM_NORMAL_MODE;
	screen_state = 1;
	alarm_data.lower_threshold = 2800;
	alarm_data.upper_threshold = 4400;
#endif

	return 0;
}

module_init(htc_battery_init);
MODULE_DESCRIPTION("HTC Battery Driver");
MODULE_LICENSE("GPL");

