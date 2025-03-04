/* Copyright (c) 2015, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/leds.h>
#include <linux/types.h>
#include <linux/err.h>
#include <linux/of_device.h>
#include <linux/slab.h>
#include "leds-ipq40xx.h"

//wucl debug
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>


#define LEDC_BASE_REG_OFFSET	0x20040
#define LEDC_REG_SIZE	4
#define BUF_NAME_LEN	25
#define MAX_BLINK_IDX	4
#define LEDC_DRV_NAME	"ipq40xx-leds"
#define LEDC_ADDR(x)	(ledc_base_addr + LEDC_BASE_REG_OFFSET + \
				(LEDC_REG_SIZE * (x)))

enum ledc_offsets {
	LEDC_CG0_OFFSET,
	LEDC_CG1_OFFSET,
	LEDC_CG2_OFFSET,
	LEDC_CG3_OFFSET,
	LEDC_CG4_OFFSET,
	LEDC_CG5_OFFSET,
	LEDC_CG6_OFFSET,
	LEDC_CG7_OFFSET,
	LEDC_CG8_OFFSET,
	LEDC_CG9_OFFSET,
	LEDC_CG10_OFFSET,
	LEDC_CG11_OFFSET,
	LEDC_MAX_OFFSET
};

enum blink_freq_values {
	BLINK_2HZ,
	BLINK_4HZ,
	BLINK_8HZ,
	BLINK_16HZ,
	BLINK_32HZ,
	BLINK_64HZ
};

enum blink_duty_values {
	BLINK_DUTY_50,
	BLINK_DUTY_25,
	BLINK_DUTY_33,
	BLINK_DUTY_67,
	BLINK_DUTY_75
};

struct ipq40xx_led_data {
	struct led_classdev cdev;
	int led_blink_idx;
};

static struct ipq40xx_led_data *leds;
static void *ledc_base_addr;
static int blink_idx_cnt;
static int led_blink_array[MAX_BLINK_IDX];

static int ipq40xx_set_led_blink_set(struct led_classdev *led_cdev,
		unsigned long *delay_on, unsigned long *delay_off)
{
	struct ipq40xx_led_data *led_dat = container_of(led_cdev,
					struct ipq40xx_led_data, cdev);
	int blink_enable, blink_freq, blink_duty, blink_value,
					duty_cycle, total_on_off;
	int reg, mask;

	if (*delay_on <= 0)
		blink_enable = blink_freq = blink_duty = 0;
	else
		blink_enable = 1;

	//printk(KERN_CRIT "wucl: %s:%d, set %s *delay_on=%lu, *delay_off=%lu\n", __func__, __LINE__, led_dat->cdev.name, *delay_on, *delay_off);

	if (blink_enable) {
		total_on_off = *delay_on + *delay_off;
		/* Rounding off to the nearest available frequency */
		if (total_on_off >= 500)
			blink_freq = BLINK_2HZ;
		else if (total_on_off >= 250)
			blink_freq = BLINK_4HZ;
		else if (total_on_off >= 125)
			blink_freq = BLINK_8HZ;
		else if (total_on_off >= 62)
			blink_freq = BLINK_16HZ;
		else if (total_on_off >= 31)
			blink_freq = BLINK_32HZ;
		else
			blink_freq = BLINK_64HZ;

		blink_freq = blink_freq << 1;

		duty_cycle = (100 * (*delay_on)) / total_on_off;
		/* Rounding off to the nearest available duty cycle */
		if (duty_cycle >= 75)
			blink_duty = BLINK_DUTY_75;
		else if (duty_cycle >= 67)
			blink_duty = BLINK_DUTY_67;
		else if (duty_cycle >= 50)
			blink_duty = BLINK_DUTY_50;
		else if (duty_cycle >= 33)
			blink_duty = BLINK_DUTY_33;
		else
			blink_duty = BLINK_DUTY_25;

		blink_duty = blink_duty << 5;
	}

	blink_value = blink_enable | blink_freq | blink_duty;
	reg = readl(LEDC_ADDR(LEDC_CG9_OFFSET));
	blink_value = blink_value << (led_dat->led_blink_idx * 8);
	mask = ~(0xFF << (led_dat->led_blink_idx * 8));
	reg = (reg & mask) | blink_value;
	writel(reg, LEDC_ADDR(LEDC_CG9_OFFSET));

	return 0;
}

static void ipq40xx_set_led_brightness_set(struct led_classdev *led_cdev,
		enum led_brightness brightness)
{
	struct ipq40xx_led_data *led_dat = container_of(led_cdev,
					struct ipq40xx_led_data, cdev);
	int reg, mask;
	unsigned long delay_on, delay_off;


	//printk(KERN_CRIT "wucl: %s:%d, set %s brightness %d\n", __func__, __LINE__, led_dat->cdev.name, brightness);

	delay_on = 0;
	delay_off = 0;
	reg = readl(LEDC_ADDR(LEDC_CG6_OFFSET));
	mask = 0x1 << led_blink_array[led_dat->led_blink_idx];
	if (brightness == LED_OFF)
		reg = reg & (~mask);
	else
		reg = reg | mask;
	/* Clear blink settings. Setting brightness overrides blink settings. */
	ipq40xx_set_led_blink_set(led_cdev, &delay_on, &delay_off);
	writel(reg, LEDC_ADDR(LEDC_CG6_OFFSET));
}

int ipq40xx_led_source_select(int led_num, enum led_source src_type)
{
	int val, cg_reg;

	cg_reg = (led_num / NUM_LED_IN_REG) + 1;

	if (cg_reg > LEDC_CG4_OFFSET || !ledc_base_addr)
		return -EINVAL;

	val = readl(LEDC_ADDR(cg_reg));
	val &= LED_MASK(led_num);
	val |=  SET_LED(led_num, src_type);

	writel(val, LEDC_ADDR(cg_reg));

	return 0;
}
EXPORT_SYMBOL(ipq40xx_led_source_select);

//wucl debug
static int my_led_num = 0;
static int my_led_source = 0;

static int led_num_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "led_num: %d\n", my_led_num);

	return 0;
}

static int led_source_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "led_source: %d\n", my_led_source);

	return 0;
}

static int led_num_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, led_num_proc_show, NULL);
}

static int led_source_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, led_source_proc_show, NULL);
}

ssize_t led_num_proc_write(struct file *file, const char *buffer, size_t count, loff_t *ppos)
{
    my_led_num = simple_strtoul(buffer, NULL, 10);
    return count;
}

ssize_t led_source_proc_write(struct file *file, const char *buffer, size_t count, loff_t *ppos)
{
    my_led_source = simple_strtoul(buffer, NULL, 10);

    //printk(KERN_CRIT "wucl: ipq40xx_led_source_select, led_num %d, led_source=%d\n", my_led_num, my_led_source);

    ipq40xx_led_source_select(my_led_num, my_led_source);

    return count;
}

static const struct file_operations my_led_num_proc_fops = {
	.open           = led_num_proc_open,
	.read           = seq_read,
	.write 		= led_num_proc_write,
	.llseek         = seq_lseek,
	.release        = single_release,
};

static const struct file_operations my_led_source_proc_fops = {
	.open           = led_source_proc_open,
	.read           = seq_read,
	.write 		= led_source_proc_write,
	.llseek         = seq_lseek,
	.release        = single_release,
};

static int ipq40xx_led_procfs_create(void)
{
	proc_create("led_num", 0, NULL, &my_led_num_proc_fops);
	proc_create("led_source", 0, NULL, &my_led_source_proc_fops);
}
//wucl debug end

static int __init ipq40xx_led_probe(struct platform_device *pdev)
{
	int ret, i;
	uint32_t val_arr[LEDC_MAX_OFFSET];
	char buf[BUF_NAME_LEN];
	struct resource *res;
	struct device_node *of_node = pdev->dev.of_node;

	if (!of_node)
		return -ENODEV;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
					"ledc_base_addr");
	if (!res) {
		dev_err(&pdev->dev, "Could not get ledc base addr.\n");
		return -EINVAL;
	}
	ledc_base_addr = ioremap(res->start, res->end - res->start + 1);
	if (!ledc_base_addr) {
		dev_err(&pdev->dev, "Failed to IO map ledc base addr.\n");
		return -ENOMEM;
	}

	//printk(KERN_CRIT "wucl: ipq40xx_led_probe, ledc_base_addr:[%x..%x]\n", res->start, res->end);

	ret = of_property_read_u32_array(of_node, "qcom,tcsr_ledc_values",
		val_arr, LEDC_MAX_OFFSET);
	if (ret) {
		dev_err(&pdev->dev,
		"invalid or missing property: qcom,tcsr_ledc_values\n");
		return -ENODEV;
	};

	for (i = 0; i < LEDC_MAX_OFFSET; i++){
		writel(val_arr[i] , LEDC_ADDR(i));
	    	//printk(KERN_CRIT "wucl: ipq40xx_led_probe, LEDC_ADDR[%d]=%x=%x\n", i, LEDC_ADDR(i), val_arr[i]);
	}

	ret = of_property_read_u32(of_node, "qcom,ledc_blink_indices_cnt",
					&blink_idx_cnt);
	if (ret) {
		dev_err(&pdev->dev, "missing blink idx count.\n");
		return -ENODEV;
	}

	//printk(KERN_CRIT "wucl: ipq40xx_led_probe, blink_idx_cnt=%d\n", blink_idx_cnt);

	if (blink_idx_cnt > MAX_BLINK_IDX)
		blink_idx_cnt = MAX_BLINK_IDX;

	ret = of_property_read_u32_array(of_node, "qcom,ledc_blink_indices",
		led_blink_array, blink_idx_cnt);
	if (ret) {
		dev_err(&pdev->dev,
			"invalid or missing property: blink indices\n");
		return -ENODEV;
	};

	for (i = 0; i < blink_idx_cnt; i++){
	    	//printk(KERN_CRIT "wucl: ipq40xx_led_probe, led_blink_array[%d]=%d\n", i, led_blink_array[i]);
	}

	leds = kzalloc((sizeof(*leds) * blink_idx_cnt), GFP_KERNEL);
	if (!leds) {
		ret = -EINVAL;
		goto err_iounmap;
	}

	for (i = 0; i < blink_idx_cnt; i++) {
		leds[i].led_blink_idx = i;
		leds[i].cdev.brightness = LED_OFF;
		leds[i].cdev.max_brightness = 1;
		leds[i].cdev.brightness_set = ipq40xx_set_led_brightness_set;
		leds[i].cdev.blink_set = ipq40xx_set_led_blink_set;

		memset(buf, 0, BUF_NAME_LEN);
		snprintf(buf, BUF_NAME_LEN, "ipq40xx::led%d", i);
		leds[i].cdev.name = buf;

	        //printk(KERN_CRIT "wucl: ipq40xx_led_probe, register blink LED: %s\n", buf);

		ret = led_classdev_register(&pdev->dev, &leds[i].cdev);
		if (ret)
			goto err_iounmap;
	}

	//wucl debug
	ipq40xx_led_procfs_create();

	return 0;

err_iounmap:
	if (leds) {
		for (; i > 0; i--)
			led_classdev_unregister(&leds[i - 1].cdev);
		kfree(leds);
		leds = NULL;
	}
	pr_err("%s - Error registering class.\n" , __func__);
	iounmap(ledc_base_addr);
	ledc_base_addr = NULL;
	return ret;
}

static int __exit ipq40xx_led_remove(struct platform_device *pdev)
{
	int i;
	if (leds) {
		for (i = 0; i < blink_idx_cnt; i++)
			led_classdev_unregister(&leds[i].cdev);
		kfree(leds);
		leds = NULL;
	}

	if (ledc_base_addr) {
		iounmap(ledc_base_addr);
		ledc_base_addr = NULL;
	}
	return 0;
}

static struct of_device_id ipq40xx_match_table[] = {
	{.compatible = "qca,ledc"},
	{},
};

static struct platform_driver ipq40xx_led_driver = {
	.probe  = ipq40xx_led_probe,
	.remove = ipq40xx_led_remove,
	.driver = {
		.name   = LEDC_DRV_NAME,
		.owner  = THIS_MODULE,
		.of_match_table = ipq40xx_match_table,
	},
};

module_platform_driver(ipq40xx_led_driver);

MODULE_LICENSE("GPL v2");
MODULE_VERSION("0.1");
MODULE_ALIAS("platform:"LEDC_DRV_NAME);
