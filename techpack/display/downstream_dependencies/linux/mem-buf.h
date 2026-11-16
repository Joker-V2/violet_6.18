/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * The msm-kernel mem-buf interface the display driver uses. This kernel has
 * no mem-buf, and all its dma-bufs belong to HLOS.
 */

#ifndef _MEM_BUF_H
#define _MEM_BUF_H

#include <linux/dma-buf.h>
#include <linux/slab.h>
#include <soc/qcom/secure_buffer.h>

static inline int mem_buf_dma_buf_copy_vmperm(struct dma_buf *dmabuf,
		int **vmids, int **perms, int *nr_acl_entries)
{
	int *vmid = kmalloc(sizeof(*vmid), GFP_KERNEL);
	int *perm = kmalloc(sizeof(*perm), GFP_KERNEL);

	if (!vmid || !perm) {
		kfree(vmid);
		kfree(perm);
		return -ENOMEM;
	}

	*vmid = VMID_HLOS;
	*perm = PERM_READ | PERM_WRITE;

	*vmids = vmid;
	*perms = perm;
	*nr_acl_entries = 1;

	return 0;
}

#endif /* _MEM_BUF_H */
