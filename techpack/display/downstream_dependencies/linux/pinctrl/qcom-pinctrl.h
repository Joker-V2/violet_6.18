/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Stubs of the msm-kernel TLMM extensions, which the pinctrl driver of
 * this kernel does not have.
 */

#ifndef __LINUX_PINCTRL_MSM_H__
#define __LINUX_PINCTRL_MSM_H__

#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/types.h>

static inline int msm_qup_write(u32 mode, u32 val)
{
	return -ENODEV;
}

static inline int msm_qup_read(u32 mode)
{
	return -ENODEV;
}

static inline int msm_gpio_mpm_wake_set(unsigned int gpio, bool enable)
{
	return -ENODEV;
}

static inline bool msm_gpio_get_pin_address(unsigned int gpio,
		struct resource *res)
{
	return false;
}

static inline int msm_spare_write(int spare_reg, u32 val)
{
	return -ENODEV;
}

static inline int msm_spare_read(int spare_reg)
{
	return -ENODEV;
}

#endif /* __LINUX_PINCTRL_MSM_H__ */
