/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * The msm-4.14 scm_call2() interface, on top of the qcom_scm driver, for
 * drivers imported from msm-4.14 that build their SMC IDs themselves.
 *
 * The definitions come from msm-4.14's include/soc/qcom/scm.h:
 * Copyright (c) 2010-2020, The Linux Foundation. All rights reserved.
 */
#ifndef __SOC_QCOM_SCM_H
#define __SOC_QCOM_SCM_H

#include <linux/firmware/qcom/qcom_scm.h>
#include <linux/types.h>

#define SCM_SVC_BOOT			0x1
#define SCM_SVC_PIL			0x2
#define SCM_SVC_UTIL			0x3
#define SCM_SVC_TZ			0x4
#define SCM_SVC_IO			0x5
#define SCM_SVC_INFO			0x6
#define SCM_SVC_SSD			0x7
#define SCM_SVC_FUSE			0x8
#define SCM_SVC_PWR			0x9
#define SCM_SVC_MP			0xC
#define SCM_SVC_DCVS			0xD
#define SCM_SVC_ES			0x10
#define SCM_SVC_HDCP			0x11
#define SCM_SVC_MDTP			0x12
#define SCM_SVC_LMH			0x13
#define SCM_SVC_SMMU_PROGRAM		0x15
#define SCM_SVC_QDSS			0x16
#define SCM_SVC_RTIC			0x19
#define SCM_SVC_TSENS			0x1E
#define SCM_SVC_TZSCHEDULER		0xFC

#define SCM_FUSE_READ			0x7
#define SCM_CMD_HDCP			0x01

#define SCM_SIP_FNID(s, c) (((((s) & 0xFF) << 8) | ((c) & 0xFF)) | 0x02000000)
#define SCM_QSEEOS_FNID(s, c) (((((s) & 0xFF) << 8) | ((c) & 0xFF)) | \
			      0x32000000)
#define SCM_SVC_ID(s) (((s) & 0xFF00) >> 8)

#define MAX_SCM_ARGS 10
#define MAX_SCM_RETS 3

enum scm_arg_types {
	SCM_VAL,
	SCM_RO,
	SCM_RW,
	SCM_BUFVAL,
};

#define SCM_ARGS_IMPL(num, a, b, c, d, e, f, g, h, i, j, ...) (\
			(((a) & 0x3) << 4) | \
			(((b) & 0x3) << 6) | \
			(((c) & 0x3) << 8) | \
			(((d) & 0x3) << 10) | \
			(((e) & 0x3) << 12) | \
			(((f) & 0x3) << 14) | \
			(((g) & 0x3) << 16) | \
			(((h) & 0x3) << 18) | \
			(((i) & 0x3) << 20) | \
			(((j) & 0x3) << 22) | \
			(num & 0xf))

#define SCM_ARGS(...) SCM_ARGS_IMPL(__VA_ARGS__, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)

/**
 * struct scm_desc
 * @arginfo:	argument count and types
 * @args:	the arguments
 * @ret:	the values the call returns
 */
struct scm_desc {
	u32 arginfo;
	u64 args[MAX_SCM_ARGS];
	u64 ret[MAX_SCM_RETS];

	/* private */
	void *extra_arg_buf;
	u64 x5;
};

static inline int scm_call2(u32 fn_id, struct scm_desc *desc)
{
	return qcom_scm_call_fnid(fn_id, desc->arginfo, desc->args, desc->ret);
}

/*
 * qcom_scm already retries while TrustZone reports busy, so the noretry
 * variant only differs in how long it may take before returning -EBUSY.
 */
static inline int scm_call2_noretry(u32 fn_id, struct scm_desc *desc)
{
	return scm_call2(fn_id, desc);
}

static inline bool is_scm_armv8(void)
{
	return true;
}

#endif
