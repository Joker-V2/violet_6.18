/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * The msm-kernel location of the SCM interface, with the secure display
 * call that the SCM driver of this kernel does not make.
 */

#ifndef __DISP_QCOM_SCM_H
#define __DISP_QCOM_SCM_H

#include <linux/errno.h>
#include <linux/firmware/qcom/qcom_scm.h>

static inline int qcom_scm_mem_protect_sd_ctrl(u32 devid, phys_addr_t mem_addr,
		u64 mem_size, u32 vmid)
{
	return -EOPNOTSUPP;
}

#endif /* __DISP_QCOM_SCM_H */
