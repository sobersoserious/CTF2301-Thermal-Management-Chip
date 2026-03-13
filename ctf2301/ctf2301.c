/**
 * @file   ctf2301.c
 * @author zgs
 * @brief  热管理芯片驱动
 * @version 1.0
 * @date   2026-2-6
 * 
 * @copyright Copyright (c) 2026
 * 
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/device.h>
#include <linux/types.h>

#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <asm/uaccess.h>
#include <linux/cdev.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <linux/kfifo.h>
#include <linux/poll.h>
#include <linux/version.h> 
#include <linux/mod_devicetable.h> 
#include <linux/module.h>

#include <linux/i2c.h>
#include <linux/platform_device.h>

#include <linux/hrtimer.h>
#include <linux/ktime.h>

#include "ctf2301.h"

#define CTF2301_NAME   "ctf2301"

struct ctf2301_dev {
    struct i2c_client *client;
    struct mutex lock;

    dev_t devid;
    struct cdev cdev;
    struct class *class;
    struct device *device;

    s16 local_temp;
    s16 remote_temp;
    u16 rotational_speed;

    struct delayed_work poll_work;
    wait_queue_head_t wq;

    bool data_ready;

    u16 poll_interval_ms;
};

static int ctf2301_write_reg(struct ctf2301_dev *ctf_dev, u8 reg, u8 value)
{
    struct device *dev = &ctf_dev->client->dev;
    int ret;
    u8 buf[2] = {reg, value};

    ret = i2c_master_send(ctf_dev->client, buf, 2);
    if (ret < 0) {
        dev_err(dev, "i2c_master_send error\n");
        return ret;
    }
    if (ret != 2) {
        dev_err(dev, "i2c_master_send count error\n");
        return -EIO;
    }

    return 0;
}

static int ctf2301_read_reg(struct ctf2301_dev *ctf_dev, u8 reg, u8 *val)
{
    struct device *dev = &ctf_dev->client->dev;
    int ret;
    struct i2c_msg msgs[2];
    u8 reg_addr = reg;
    u8 data;

    msgs[0].addr = ctf_dev->client->addr;
    msgs[0].flags = 0;
    msgs[0].len = 1;
    msgs[0].buf = &reg_addr;

    msgs[1].addr = ctf_dev->client->addr;
    msgs[1].flags = I2C_M_RD;
    msgs[1].len = 1;
    msgs[1].buf = &data;

    ret = i2c_transfer(ctf_dev->client->adapter, msgs, ARRAY_SIZE(msgs));
    if (ret < 0) {
        dev_err(dev, "read reg 0x%02x failed: %d\n", reg, ret);
        return ret;
    }
    if (ret != ARRAY_SIZE(msgs)) {
        dev_err(dev, "short read from reg 0x%02x\n", reg);
        return -EIO;
    }

    *val = data;
    return 0;
}

static ssize_t mode_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct ctf2301_dev *ctf_dev = dev_get_drvdata(dev);
    u8 val;
    int ret;

    mutex_lock(&ctf_dev->lock);
    ret = ctf2301_read_reg(ctf_dev, CTF2301_PWM_TACH_CONFIG, &val);
    mutex_unlock(&ctf_dev->lock);

    if (ret < 0)
        return ret;

    return sysfs_emit(buf, "%s\n", (val & BIT(5)) ? "manual" : "auto");
}

static ssize_t mode_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    struct ctf2301_dev *ctf_dev = dev_get_drvdata(dev);
    u8 val;
    int ret;
    bool manual;

    if (sysfs_streq(buf, "manual"))
        manual = true;
    else if (sysfs_streq(buf, "auto"))
        manual = false;
    else
        return -EINVAL;

    mutex_lock(&ctf_dev->lock);
    ret = ctf2301_read_reg(ctf_dev, CTF2301_PWM_TACH_CONFIG, &val);
    if (ret < 0)
        goto unlock;

    if (manual)
        val |= BIT(5);
    else
        val &= ~BIT(5);

    ret = ctf2301_write_reg(ctf_dev, CTF2301_PWM_TACH_CONFIG, val);
unlock:
    mutex_unlock(&ctf_dev->lock);

    return ret < 0 ? ret : count;
}

static ssize_t pwm_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct ctf2301_dev *ctf_dev = dev_get_drvdata(dev);
    u8 val;
    int ret;

    mutex_lock(&ctf_dev->lock);
    ret = ctf2301_read_reg(ctf_dev, CTF2301_PWM_VALUE, &val);
    mutex_unlock(&ctf_dev->lock);

    if (ret < 0)
        return ret;

    return sysfs_emit(buf, "%u\n", val);
}

static ssize_t pwm_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    struct ctf2301_dev *ctf_dev = dev_get_drvdata(dev);
    u8 pwm_tach, pwm_val;
    unsigned long val;
    int ret;

    ret = kstrtoul(buf, 10, &val);
    if (ret < 0)
        return ret;

    if (val > 255)
        return -EINVAL;

    pwm_val = val;

    mutex_lock(&ctf_dev->lock);
    ret = ctf2301_read_reg(ctf_dev, CTF2301_PWM_TACH_CONFIG, &pwm_tach);
    if (ret < 0)
        goto unlock;

    if (!(pwm_tach & BIT(5))) {
        ret = -EPERM;
        goto unlock;
    }

    ret = ctf2301_write_reg(ctf_dev, CTF2301_PWM_VALUE, pwm_val);
unlock:
    mutex_unlock(&ctf_dev->lock);

    return ret < 0 ? ret : count;
}

static ssize_t poll_interval_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct ctf2301_dev *ctf_dev = dev_get_drvdata(dev);

    return sysfs_emit(buf, "%u\n", ctf_dev->poll_interval_ms);
}

static ssize_t poll_interval_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    struct ctf2301_dev *ctf_dev = dev_get_drvdata(dev);
    unsigned long val;
    int ret;

    ret = kstrtoul(buf, 10, &val);
    if (ret < 0) {
        return ret;
    }

    if (val < 150 || val > 1000) {
        return -EINVAL;
    }

    mutex_lock(&ctf_dev->lock);
    ctf_dev->poll_interval_ms = val;
    mutex_unlock(&ctf_dev->lock);

    return count;
}

static ssize_t local_temp_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct ctf2301_dev *ctf_dev = dev_get_drvdata(dev);
    s32 val;

    mutex_lock(&ctf_dev->lock);
    val = (s32)ctf_dev->local_temp * 625;
    mutex_unlock(&ctf_dev->lock);

    return sysfs_emit(buf, "%d\n", val);
}

static ssize_t remote_temp_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct ctf2301_dev *ctf_dev = dev_get_drvdata(dev);
    s32 val;
    mutex_lock(&ctf_dev->lock);
    val = (s32)ctf_dev->remote_temp * 3125;
    mutex_unlock(&ctf_dev->lock);

    return sysfs_emit(buf, "%d\n", val);
}

static ssize_t rotational_speed_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct ctf2301_dev *ctf_dev = dev_get_drvdata(dev);
    u16 val;

    mutex_lock(&ctf_dev->lock);
    val = ctf_dev->rotational_speed;
    mutex_unlock(&ctf_dev->lock);

    return sysfs_emit(buf, "%u\n", val);
}

static DEVICE_ATTR_RW(mode);
static DEVICE_ATTR_RW(pwm);
static DEVICE_ATTR_RW(poll_interval);
static DEVICE_ATTR_RO(local_temp);
static DEVICE_ATTR_RO(remote_temp);
static DEVICE_ATTR_RO(rotational_speed);

static struct attribute *ctf2301_attrs[] = {
    &dev_attr_mode.attr,
    &dev_attr_pwm.attr,
    &dev_attr_poll_interval.attr,
    &dev_attr_local_temp.attr,
    &dev_attr_remote_temp.attr,
    &dev_attr_rotational_speed.attr,
    NULL,
};

static const struct attribute_group ctf2301_attr_group = {
    .attrs = ctf2301_attrs,
};

static int ctf2301_wait_por_ready(struct ctf2301_dev *ctf_dev)
{
    struct device *dev = &ctf_dev->client->dev;
    int ret;
    unsigned long timeout;
    u8 val;

    // wait 100ms
    timeout = jiffies + msecs_to_jiffies(100);

    do {
        ret = ctf2301_read_reg(ctf_dev, CTF2301_POWER_ON_RESET_STATUS, &val);
        if (ret < 0) {
            dev_err(dev, "ctf2301_read_reg POR ERROR\n");
            return ret;
        }

        if (!(val & 0x80)){
            dev_info(dev, "CTF2301 POR is ready!\n");
            return 0;
        }

        usleep_range(1000, 2000);

    } while (time_before(jiffies, timeout));

    dev_err(dev, "POR waiting Timeout\n");

    return -ETIMEDOUT;
}

// static int ctf2301_setpoint_config(struct ctf2301_dev *ctf_dev)
// {
//     struct device *dev = &ctf_dev->client->dev;
//     int ret;

//     ret = ctf2301_write_reg(ctf_dev, CTF2301_LOCAL_HIGH_SETPOINT_MSB, );
//     if (ret < 0) {
//         dev_err(dev, "ctf2301_write_reg CTF2301_LOCAL_HIGH_SETPOINT_MSB error\n");
//         return ret;
//     }

//     ret = ctf2301_write_reg(ctf_dev, CTF2301_LOCAL_HIGH_SETPOINT_LSB, );
//     if (ret < 0) {
//         dev_err(dev, "ctf2301_write_reg CTF2301_LOCAL_HIGH_SETPOINT_LSB error\n");
//         return ret;
//     }

//     ret = ctf2301_write_reg(ctf_dev, CTF2301_REMOTE_HIGH_SETPOINT_MSB, );
//     if (ret < 0) {
//         dev_err(dev, "ctf2301_write_reg CTF2301_REMOTE_HIGH_SETPOINT_MSB error\n");
//         return ret;
//     }

//     ret = ctf2301_write_reg(ctf_dev, CTF2301_REMOTE_HIGH_SETPOINT_LSB, );
//     if (ret < 0) {
//         dev_err(dev, "ctf2301_write_reg CTF2301_REMOTE_HIGH_SETPOINT_LSB error\n");
//         return ret;
//     }

//     ret = ctf2301_write_reg(ctf_dev, CTF2301_REMOTE_LOW_SETPOINT_MSB, );
//     if (ret < 0) {
//         dev_err(dev, "ctf2301_write_reg CTF2301_REMOTE_LOW_SETPOINT_MSB error\n");
//         return ret;
//     }

//     ret = ctf2301_write_reg(ctf_dev, CTF2301_REMOTE_LOW_SETPOINT_LSB, );
//     if (ret < 0) {
//         dev_err(dev, "ctf2301_write_reg CTF2301_REMOTE_LOW_SETPOINT_LSB error\n");
//         return ret;
//     }

//     ret = ctf2301_write_reg(ctf_dev, CTF2301_REMOTE_T_CRIT_SETPOINT, );
//     if (ret < 0) {
//         dev_err(dev, "ctf2301_write_reg CTF2301_REMOTE_T_CRIT_SETPOINT error\n");
//         return ret;
//     }

//     ret = ctf2301_write_reg(ctf_dev, CTF2301_REMOTE_T_CRIT_HYS_SETPOINT, );
//     if (ret < 0) {
//         dev_err(dev, "ctf2301_write_reg CTF2301_REMOTE_T_CRIT_HYS_SETPOINT error\n");
//         return ret;
//     }
// }

static int ctf2301_LUT_config(struct ctf2301_dev *ctf_dev)
{
    struct device *dev = &ctf_dev->client->dev;
    int ret;

    ret = ctf2301_write_reg(ctf_dev, CTF2301_LUT1_Temp, 0x3C);
    ret = ctf2301_write_reg(ctf_dev, CTF2301_LUT2_Temp, 0x41);
    ret = ctf2301_write_reg(ctf_dev, CTF2301_LUT3_Temp, 0x46);
    ret = ctf2301_write_reg(ctf_dev, CTF2301_LUT4_Temp, 0x4B);
    ret = ctf2301_write_reg(ctf_dev, CTF2301_LUT5_Temp, 0x50);
    ret = ctf2301_write_reg(ctf_dev, CTF2301_LUT6_Temp, 0x55);
    ret = ctf2301_write_reg(ctf_dev, CTF2301_LUT7_Temp, 0x5A);
    ret = ctf2301_write_reg(ctf_dev, CTF2301_LUT8_Temp, 0x5F);
    ret = ctf2301_write_reg(ctf_dev, CTF2301_LUT9_Temp, 0x64);
    ret = ctf2301_write_reg(ctf_dev, CTF2301_LUT10_Temp, 0x69);
    ret = ctf2301_write_reg(ctf_dev, CTF2301_LUT11_Temp, 0x6E);
    ret = ctf2301_write_reg(ctf_dev, CTF2301_LUT12_Temp, 0x73);

    ret = ctf2301_write_reg(ctf_dev, CTF2301_LUT1_PWM, 0x15);
    ret = ctf2301_write_reg(ctf_dev, CTF2301_LUT2_PWM, 0x2A);
    ret = ctf2301_write_reg(ctf_dev, CTF2301_LUT3_PWM, 0x3F);
    ret = ctf2301_write_reg(ctf_dev, CTF2301_LUT4_PWM, 0x54);
    ret = ctf2301_write_reg(ctf_dev, CTF2301_LUT5_PWM, 0x69);
    ret = ctf2301_write_reg(ctf_dev, CTF2301_LUT6_PWM, 0x7E);
    ret = ctf2301_write_reg(ctf_dev, CTF2301_LUT7_PWM, 0x93);
    ret = ctf2301_write_reg(ctf_dev, CTF2301_LUT8_PWM, 0xA8);
    ret = ctf2301_write_reg(ctf_dev, CTF2301_LUT9_PWM, 0xBD);
    ret = ctf2301_write_reg(ctf_dev, CTF2301_LUT10_PWM, 0xD2);
    ret = ctf2301_write_reg(ctf_dev, CTF2301_LUT11_PWM, 0xE7);
    ret = ctf2301_write_reg(ctf_dev, CTF2301_LUT12_PWM, 0xFC);
    if (ret < 0) {
        dev_err(dev, "ctf2301_write_reg CTF2301_LUT error\n");
        return ret;
    }

    dev_info(dev, "ctf2301 LUT config success\n");
    
    return 0;
}

static int ctf2301_init(struct ctf2301_dev *ctf_dev)
{
    struct device *dev = &ctf_dev->client->dev;
    int ret;

    // ret = ctf2301_wait_por_ready(ctf_dev);
    // if (ret < 0) {
    //     dev_err(dev, "ctf2301_wait_por_ready error\n");
    //     return ret;
    // }
    
    //STBY set 1, low power standby mode, TACH pin can be Tach input
    ret = ctf2301_write_reg(ctf_dev, CTF2301_CONFIG, 0x44);
    if (ret < 0) {
        dev_err(dev, "ctf2301_write_reg CTF2301_CONFIG_start error\n");
        return ret;
    }

    //Conversion Rate = 6.494Hz
    ret = ctf2301_write_reg(ctf_dev, CTF2301_CONV, 0x07);
    if (ret < 0) {
        dev_err(dev, "ctf2301_write_reg CTF2301_CONV error\n");
        return ret;
    }

    //PWM Smoothing Ramp: 10.9s to 100%, PWM Resolution = 0.39%, LUT Resolution = 0.5°, enabled remote temperature LSbs[4:3]
    ret = ctf2301_write_reg(ctf_dev, CTF2301_ENHANCED_CONFIG, 0x73);
    if (ret < 0) {
        dev_err(dev, "ctf2301_write_reg CTF2301_ENHANCED_CONFIG error\n");
        return ret;
    }
    
    //PWM Frequency: PWM clock/2^n = 22.5khz. n = 8
    ret = ctf2301_write_reg(ctf_dev, CTF2301_PWM_FREQUENCY, 0x08);
    if (ret < 0) {
        dev_err(dev, "ctf2301_write_reg CTF2301_PWM_FREQUENCY error\n");
        return ret;
    }

    //PWM clock is 360 kHz, PWM Output Polarity:PWM output pin will be open for fan OFF and 0V for fan ON
    ret = ctf2301_write_reg(ctf_dev, CTF2301_PWM_TACH_CONFIG, 0x20);
    if (ret < 0) {
        dev_err(dev, "ctf2301_write_reg CTF2301_PWM_TACH_CONFIG error\n");
        return ret;
    }

    //LUT Temp:30-57.5℃ -> PWM:8-96%
    ret = ctf2301_LUT_config(ctf_dev);
    if (ret < 0) {
        dev_err(dev, "ctf2301_write_reg ctf2301_LUT_config error\n");
        return ret;
    }

    //PWM clock is 360 kHz, PWM Output Polarity:PWM output pin will be open for fan OFF and 0V for fan ON
    ret = ctf2301_write_reg(ctf_dev, CTF2301_PWM_TACH_CONFIG, 0x00);
    if (ret < 0) {
        dev_err(dev, "ctf2301_write_reg CTF2301_PWM_TACH_CONFIG error\n");
        return ret;
    }

    //Reload LUT Cofig
    ret = ctf2301_write_reg(ctf_dev, CTF2301_SPIN_UP_CONFIG, 0x01);
    if (ret < 0) {
        dev_err(dev, "ctf2301_write_reg CTF2301_SPIN_UP_CONFIG error\n");
        return ret;
    }

    //STBY set 0, operational mode, TACH pin can be Tach input
    ret = ctf2301_write_reg(ctf_dev, CTF2301_CONFIG, 0x04);
    if (ret < 0) {
        dev_err(dev, "ctf2301_write_reg CTF2301_CONFIG_end error\n");
        return ret;
    }

    return 0;
}

static int ctf2301_Local_Temp_read(struct ctf2301_dev *ctf_dev)
{
    struct device *dev = &ctf_dev->client->dev;
    int ret;
    u8 buf[2];
    s16 raw;

    ret = ctf2301_read_reg(ctf_dev, CTF2301_LOCAL_TEMP_MSB, &buf[0]);
    if (ret < 0) {
        dev_err(dev, "CTF2301_LOCAL_TEMP_MSB ERROR\n");
        return ret;
    }
    ret = ctf2301_read_reg(ctf_dev, CTF2301_LOCAL_TEMP_LSB, &buf[1]);
    if (ret < 0) {
        dev_err(dev, "CTF2301_LOCAL_TEMP_LSB ERROR\n");
        return ret;
    }

    raw = ((u16)buf[0] << 4) | (buf[1] >> 4);
    raw = sign_extend32(raw, 11);

    ctf_dev->local_temp = raw;

    return 0;
}

static int ctf2301_Remote_Temp_read(struct ctf2301_dev *ctf_dev)
{
    struct device *dev = &ctf_dev->client->dev;
    int ret;
    u8 buf[2];
    s16 raw;

    ret = ctf2301_read_reg(ctf_dev, CTF2301_REMOTE_TEMP_MSB, &buf[0]);
    if (ret < 0) {
        dev_err(dev, "CTF2301_REMOTE_TEMP_MSB ERROR\n");
        return ret;
    }
    ret = ctf2301_read_reg(ctf_dev, CTF2301_REMOTE_TEMP_LSB, &buf[1]);
    if (ret < 0) {
        dev_err(dev, "CTF2301_REMOTE_TEMP_LSB ERROR\n");
        return ret;
    }

    raw = ((u16)buf[0] << 5) | (buf[1] >> 3);
    raw = sign_extend32(raw, 12);

    ctf_dev->remote_temp = raw;

    return 0;
}

static int ctf2301_Tach_Count_read(struct ctf2301_dev *ctf_dev)
{
    struct device *dev = &ctf_dev->client->dev;
    int ret;
    u8 buf[2];
    u16 raw;

    ret = ctf2301_read_reg(ctf_dev, CTF2301_TACH_COUNT_MSB, &buf[0]);
    if (ret < 0) {
        dev_err(dev, "CTF2301_TACH_COUNT_MSB ERROR\n");
        return ret;
    }
    ret = ctf2301_read_reg(ctf_dev, CTF2301_TACH_COUNT_LSB, &buf[1]);
    if (ret < 0) {
        dev_err(dev, "CTF2301_TACH_COUNT_LSB ERROR\n");
        return ret;
    }

    raw = ((u16)buf[0] << 6) | (buf[1] >> 2);

    ctf_dev->rotational_speed = raw;

    return 0;
}

static void ctf2301_poll_work(struct work_struct *work)
{
    struct ctf2301_dev *ctf_dev = container_of(to_delayed_work(work), struct ctf2301_dev, poll_work);
    int ret;

    mutex_lock(&ctf_dev->lock);
    ret = ctf2301_Local_Temp_read(ctf_dev);
    if (ret < 0) {
        dev_err(&ctf_dev->client->dev, "ctf2301_Local_Temp_read Failed\n");
        goto unlock;
    }

    ret = ctf2301_Remote_Temp_read(ctf_dev);
    if (ret < 0) {
        dev_err(&ctf_dev->client->dev, "ctf2301_Remote_Temp_read Failed\n");
        goto unlock;
    }

    ret = ctf2301_Tach_Count_read(ctf_dev);
    if (ret < 0) {
        dev_err(&ctf_dev->client->dev, "ctf2301_Tach_Count_read Failed\n");
        goto unlock;
    }
    ctf_dev->data_ready = true;

unlock:
    mutex_unlock(&ctf_dev->lock);

    wake_up_interruptible(&ctf_dev->wq);

    schedule_delayed_work(&ctf_dev->poll_work, msecs_to_jiffies(ctf_dev->poll_interval_ms));
}

static int ctf2301_open(struct inode *inode, struct file *filp)
{
    struct ctf2301_dev *ctf_dev = container_of(inode->i_cdev, struct ctf2301_dev, cdev);
    filp->private_data = ctf_dev;

    return 0;
}

struct ctf2301_data {
    s16 local_temp;
    s16 remote_temp;
    u16 rotational_speed;
};

static ssize_t ctf2301_read(struct file *filp, char __user *buf, size_t cnt, loff_t *off)
{
    struct ctf2301_dev *ctf_dev = filp->private_data;
    struct ctf2301_data data;
    unsigned long uncopied;
    int ret;

    if (cnt < sizeof(data)) {
        pr_err("ctf2301 recived cnt do not enough\n");
        return -EINVAL;
    }

    ret = wait_event_interruptible(ctf_dev->wq, ctf_dev->data_ready);
    if (ret) {
        pr_err("ctf2301 Sudden Stopped\n");
        return ret;
    }

    mutex_lock(&ctf_dev->lock);
    data.local_temp = ctf_dev->local_temp;
    data.remote_temp = ctf_dev->remote_temp;
    data.rotational_speed = ctf_dev->rotational_speed;

    ctf_dev->data_ready = false;
    mutex_unlock(&ctf_dev->lock);

    uncopied = copy_to_user(buf, &data, sizeof(data));
    if (uncopied != 0) {
        pr_err("ctf2301 copy_to_user error\n");
        return -EFAULT;
    }

    return sizeof(data);
}

static int ctf2301_release(struct inode *inode, struct file *filp)
{
    filp->private_data = NULL;
    
    return 0;
}

static __poll_t ctf2301_poll(struct file *filp, struct poll_table_struct *wait)
{
    struct ctf2301_dev *ctf_dev = filp->private_data;
    __poll_t mask = 0;

    poll_wait(filp, &ctf_dev->wq, wait);

    mutex_lock(&ctf_dev->lock);
    if (ctf_dev->data_ready) {
        mask |= POLLIN | POLLRDNORM;
    }
    mutex_unlock(&ctf_dev->lock);

    return mask;
}

static const struct file_operations ctf2301_ops = {
	.owner = THIS_MODULE,
	.open = ctf2301_open,
	.read = ctf2301_read,
	.release = ctf2301_release,
    .poll = ctf2301_poll,
};

static int ctf2301_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    int ret;
    struct ctf2301_dev *ctf_dev = NULL;

    dev_info(&client->dev, "ctf2301_probe start!");

    ctf_dev = devm_kzalloc(&client->dev, sizeof(*ctf_dev), GFP_KERNEL);
    if (!ctf_dev) {
        return -ENOMEM;
    }

    ctf_dev->client = client;
    mutex_init(&ctf_dev->lock);

    /* Registered cdevid */
    ret = alloc_chrdev_region(&ctf_dev->devid, 0, 1, CTF2301_NAME);
    if (ret < 0) {
        dev_err(&client->dev, "alloc_chrdev_region Failed\n");
        return ret;
    }

    /* Registered cdev */
    cdev_init(&ctf_dev->cdev, &ctf2301_ops);
    ret = cdev_add(&ctf_dev->cdev, ctf_dev->devid, 1);
    if (ret < 0) {
        dev_err(&client->dev, "cdev_add Failed\n");
        goto err_unregister_chrdev;
    }

    /* created class */
    ctf_dev->class = class_create(THIS_MODULE, CTF2301_NAME);
    if (IS_ERR(ctf_dev->class)) {
        ret = PTR_ERR(ctf_dev->class);
        dev_err(&client->dev, "class_create Failed\n");
        goto err_cdev_del;
    }

    /*creared device*/
    ctf_dev->device = device_create(ctf_dev->class, NULL, ctf_dev->devid, NULL, CTF2301_NAME);
    if (IS_ERR(ctf_dev->device)) {
        ret = PTR_ERR(ctf_dev->device);
        dev_err(&client->dev, "device_create Failed\n");
        goto err_class_destroy;
    }

    dev_set_drvdata(ctf_dev->device, ctf_dev);

    ret = sysfs_create_group(&ctf_dev->device->kobj, &ctf2301_attr_group);
    if (ret < 0) {
        dev_err(&client->dev, "sysfs_create_group Failed\n");
        goto err_device_destroy;
    }
    
    i2c_set_clientdata(client, ctf_dev);

    init_waitqueue_head(&ctf_dev->wq);
    INIT_DELAYED_WORK(&ctf_dev->poll_work, ctf2301_poll_work);
    ctf_dev->data_ready = false;
    ctf_dev->poll_interval_ms = 200;

    dev_info(&client->dev, "ctf2301_base init success");

    ret = ctf2301_init(ctf_dev);
    if (ret < 0) {
        dev_info(&client->dev, "ctf2301_init ERROR!");
        goto err_sysfs_remove;
    }

    schedule_delayed_work(&ctf_dev->poll_work, msecs_to_jiffies(ctf_dev->poll_interval_ms));

    dev_info(&client->dev, "ctf2301_i2c_probe Succeed!\n");

    return 0;

err_sysfs_remove:
    sysfs_remove_group(&ctf_dev->device->kobj, &ctf2301_attr_group);
err_device_destroy:
    device_destroy(ctf_dev->class, ctf_dev->devid);
err_class_destroy:
    class_destroy(ctf_dev->class);
err_cdev_del:
    cdev_del(&ctf_dev->cdev);
err_unregister_chrdev:
    unregister_chrdev_region(ctf_dev->devid, 1);
    return ret;
}

static int ctf2301_i2c_remove(struct i2c_client *client)
{
    struct ctf2301_dev *ctf_dev = i2c_get_clientdata(client);

    cancel_delayed_work_sync(&ctf_dev->poll_work);
    ctf_dev->data_ready = true;
    wake_up_interruptible_all(&ctf_dev->wq);

    sysfs_remove_group(&ctf_dev->device->kobj, &ctf2301_attr_group);

    device_destroy(ctf_dev->class, ctf_dev->devid);
    class_destroy(ctf_dev->class);
    cdev_del(&ctf_dev->cdev);
    unregister_chrdev_region(ctf_dev->devid, 1);

    dev_info(&client->dev, "CTF2301 I2C Removed\n");

    return 0;
}

static const struct of_device_id ctf2301_of_match[] = {
    { .compatible = "sensy,ctf2301" },
    { /* Sentinel */ }
};
MODULE_DEVICE_TABLE(of, ctf2301_of_match);

static const struct i2c_device_id ctf2301_id[] = {
    { CTF2301_NAME, 0 },
    { }
};
MODULE_DEVICE_TABLE(i2c, ctf2301_id);

static struct i2c_driver ctf2301_driver = {
    .probe = ctf2301_i2c_probe,
    .remove = ctf2301_i2c_remove,
    .driver = {
        .owner = THIS_MODULE,
        .name = CTF2301_NAME,
        .of_match_table = ctf2301_of_match,
    },
    .id_table = ctf2301_id,
};

static int __init ctf2301_init_driver(void)
{
    int ret;

    ret = i2c_add_driver(&ctf2301_driver);
    return ret;
}

static void __exit ctf2301_exit_driver(void)
{
    i2c_del_driver(&ctf2301_driver);
}

module_init(ctf2301_init_driver);
module_exit(ctf2301_exit_driver);
// module_i2c_driver(ctf2301_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("zgs");
MODULE_DESCRIPTION("CTF2301 sensor driver");