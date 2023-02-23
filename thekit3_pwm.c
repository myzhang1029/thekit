/* PWM light controller with a dev interface and button on/off support
 * as a kernel module. */
/*
 *  thekit3_pwm.c
 *  Copyright (C) 2022 Zhang Maiyun <me@myzhangll.xyz>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/pwm.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

/* Constant definitions */
#define DRIVER_NAME "thekit_pwm"
#define DRIVER_PRINTK DRIVER_NAME ": "
#define DRIVER_CLASS DRIVER_NAME

/* Period in nanoseconds: 1e6 Hz */
const int PWM_PERIOD = 10000;
/* Pin (BCM) of the switch */
const unsigned SW_PIN = 19;

/* RT variables */
/* Automatically rescaled to [0, PWM_PERIOD] */
int pwm_duty = 0;
struct pwm_device *pwm0 = NULL;
unsigned int switch_irq_number;
static dev_t ctrl_dev_number;
static struct class *dev_class;
static struct cdev ctrl_dev;

/* Convert an ASCII string to an s64.
 * Only converts digits, returns -1 immediately if there is a non-digit.
 */
static s64 string_to_int(char *string)
{
    s64 result = 0;
    while (*string)
    {
        result *= 10;
        if (*string > '9' || *string < '0')
            return -1;
        result += *string++ - '0';
    }
    return result;
}

/* If pwm_duty is not 0, change it to 0;
 * Otherwise, change it to PWM_PERIOD.
 */
static irq_handler_t pwm_toggle(unsigned int _irq, void *_dev_id,
                                struct pt_regs *_regs)
{
    pr_info(DRIVER_PRINTK "Toggling switch\n");
    pwm_duty = pwm_duty ? 0 : PWM_PERIOD;
    pwm_config(pwm0, pwm_duty, PWM_PERIOD);
    return (irq_handler_t)IRQ_HANDLED;
}

/* Device write handler: alter the duty cycle.
 * Follows pigpio convention: 1e6 is 100%.
 *
 * Supports only one operation: write an integer between 0 and 1e6,
 * terminated by a newline.
 */
static ssize_t driver_onwrite(struct file *file, const char *user_buffer,
                              size_t count, loff_t *offs)
{
    int remaining;
    s64 result;
    char buffer[11];
    /* I don't want a failed malloc */
    if (count > 10)
    {
        pr_err(DRIVER_PRINTK
               "Discarding oversized written content (%zu > 10)\n",
               count);
        return count;
    }
    /* Empty? */
    if (count < 1)
        return count;

    remaining = copy_from_user(buffer, user_buffer, count);
    if (buffer[count - 1] == '\n')
        buffer[count - 1] = 0;
    else
        buffer[count] = 0;
    /* Convert the input to integer */
    result = string_to_int(buffer);
    pr_debug(DRIVER_PRINTK "Parsed %s as %d\n", buffer, (int)result);

    /* Malformed input */
    if (result == -1)
        pr_err(DRIVER_PRINTK "Invalid Value: %s\n", buffer);
    /* Do the multiplication with s64 */
    result = result * PWM_PERIOD / 1000000;
    pwm_duty = (int)result;
    pr_info(DRIVER_PRINTK "Setting PWM duty to %d\n", pwm_duty);
    pwm_config(pwm0, pwm_duty, PWM_PERIOD);

    return count - remaining;
}

static struct file_operations fops = {.owner = THIS_MODULE,
                                      .write = driver_onwrite};

static int __init thekit_pwm_init(void)
{
    pr_info(DRIVER_PRINTK "Loading " DRIVER_NAME "\n");

    /* Set up the switch button */
    if (gpio_request(SW_PIN, "switch-gpio-pin"))
    {
        pr_err(DRIVER_PRINTK "Cannot get access to GPIO %u\n", SW_PIN);
        return -1;
    }
    if (gpio_direction_input(SW_PIN))
    {
        pr_err(DRIVER_PRINTK "Cannot set GPIO %u to input\n", SW_PIN);
        goto zeroth_death;
        return -1;
    }

    /* Set up interrupt handling */
    switch_irq_number = gpio_to_irq(SW_PIN);

    if (request_irq(switch_irq_number, (irq_handler_t)pwm_toggle,
                    IRQF_TRIGGER_FALLING, "switch-gpio-irq", NULL))
    {
        pr_err(DRIVER_PRINTK "Cannot set up interrupt handling\n");
        goto zeroth_death;
        return -1;
    }

    /* Do some debouncing. I don't care it succeed or not */
    gpio_set_debounce(SW_PIN, 4000);

    /* Allocate device number */
    if (alloc_chrdev_region(&ctrl_dev_number, 0, 1, DRIVER_NAME) < 0)
    {
        pr_crit(DRIVER_PRINTK "Cannot allocate a device number\n");
        goto first_death;
        return -1;
    }
    pr_debug(DRIVER_PRINTK "Allocated device number is: %d %d\n",
             ctrl_dev_number >> 20, ctrl_dev_number && 0xfffff);

    /* Create device class */
    dev_class = class_create(THIS_MODULE, DRIVER_CLASS);
    if (!dev_class)
    {
        pr_crit(DRIVER_PRINTK "Cannot create device class\n");
        goto second_death;
    }

    /* Create /dev device for the control interface */
    if (!device_create(dev_class, NULL, ctrl_dev_number, NULL, DRIVER_NAME))
    {
        pr_crit(DRIVER_PRINTK "Cannot create control interface device\n");
        goto third_death;
    }
    cdev_init(&ctrl_dev, &fops);
    if (cdev_add(&ctrl_dev, ctrl_dev_number, 1) == -1)
    {
        pr_crit(DRIVER_PRINTK "Cannot register control interface device\n");
        goto fourth_death;
    }

    /* Request and set up PWM0 */
    pwm0 = pwm_request(0, "light-pwm");
    if (!pwm0)
    {
        pr_crit(DRIVER_PRINTK "Cannot get access to PWM0\n");
        goto fifth_death;
    }

    pwm_config(pwm0, pwm_duty, PWM_PERIOD);
    pwm_config(pwm0, PWM_PERIOD, PWM_PERIOD);
    pwm_config(pwm0, pwm_duty, PWM_PERIOD);
    pwm_enable(pwm0);

    return 0;
fifth_death:
    cdev_del(&ctrl_dev);
fourth_death:
    device_destroy(dev_class, ctrl_dev_number);
third_death:
    class_destroy(dev_class);
second_death:
    unregister_chrdev_region(ctrl_dev_number, 1);
first_death:
    free_irq(switch_irq_number, NULL);
zeroth_death:
    gpio_free(SW_PIN);
    return -1;
}

static void __exit thekit_pwm_exit(void)
{
    pr_info(DRIVER_PRINTK "Unloading " DRIVER_NAME "\n");
    pwm_disable(pwm0);
    pwm_free(pwm0);
    cdev_del(&ctrl_dev);
    device_destroy(dev_class, ctrl_dev_number);
    class_destroy(dev_class);
    unregister_chrdev_region(ctrl_dev_number, 1);
    free_irq(switch_irq_number, NULL);
    gpio_free(SW_PIN);
}

module_init(thekit_pwm_init);
module_exit(thekit_pwm_exit);

/* Meta Information */
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Zhang Maiyun");
MODULE_DESCRIPTION("PWM light controller with a dev interface and button "
                   "on/off support");
