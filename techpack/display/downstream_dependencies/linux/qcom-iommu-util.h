/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * The msm-kernel IOMMU helpers the display driver uses. The SMMU driver of
 * this kernel switches no stream IDs and enables the translation of the
 * context banks itself, so there is nothing left for them to do.
 */

#ifndef __QCOM_IOMMU_UTIL_H
#define __QCOM_IOMMU_UTIL_H

#include <linux/device.h>
#include <linux/iommu.h>

enum sid_switch_direction {
	SID_ACQUIRE,
	SID_RELEASE,
};

static inline int qcom_iommu_sid_switch(struct device *dev,
		enum sid_switch_direction dir)
{
	return 0;
}

static inline int qcom_iommu_enable_s1_translation(struct iommu_domain *domain)
{
	return 0;
}

#endif /* __QCOM_IOMMU_UTIL_H */
