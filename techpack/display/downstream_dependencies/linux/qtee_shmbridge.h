/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * The msm-kernel QTEE shared memory bridge the display driver uses. This
 * kernel has no shmbridge, so the bridge is never enabled.
 */

#ifndef __QTEE_SHMBRIDGE_H__
#define __QTEE_SHMBRIDGE_H__

#include <linux/errno.h>
#include <linux/types.h>
#include <soc/qcom/secure_buffer.h>

struct qtee_shm {
	phys_addr_t paddr;
	void *vaddr;
	size_t size;
};

static inline bool qtee_shmbridge_is_enabled(void)
{
	return false;
}

static inline int32_t qtee_shmbridge_allocate_shm(size_t size,
		struct qtee_shm *shm)
{
	return -EOPNOTSUPP;
}

static inline void qtee_shmbridge_free_shm(struct qtee_shm *shm)
{
}

#endif /* __QTEE_SHMBRIDGE_H__ */
