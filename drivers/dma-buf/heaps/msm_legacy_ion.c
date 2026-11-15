// SPDX-License-Identifier: GPL-2.0
/*
 * /dev/ion for vendor userspace built against msm-4.14/4.19.
 *
 * libion in such a vendor partition talks to /dev/ion with the ION API of
 * Linux 4.12 and later: an allocation returns a dma-buf fd, heaps are listed
 * with ION_IOC_HEAP_QUERY and there is no ION_IOC_FREE, which is how libion
 * tells this API from the older handle based one. Its users pick msm heaps
 * by the IDs those kernels gave them; libQSEEComAPI for instance asks for
 * the QSEECOM heap, 27.
 *
 * 6.18 has no ION, so provide that interface on top of DMA-BUF, with the
 * heaps described in the DT the way the msm ION driver took them:
 *
 *  - "SYSTEM" heaps come from the DMA-BUF system heap;
 *  - "DMA" heaps hand out physically contiguous buffers from the reserved
 *    memory region they point to: CMA for a reusable region, a plain
 *    allocator for a no-map carveout.
 */

#define pr_fmt(fmt) "msm_ion: " fmt

#include <linux/cma.h>
#include <linux/dma-buf.h>
#include <linux/dma-heap.h>
#include <linux/dma-mapping.h>
#include <linux/genalloc.h>
#include <linux/highmem.h>
#include <linux/io.h>
#include <linux/iosys-map.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

/* ION API of Linux 4.12-5.x (include/uapi/linux/ion.h) */
#define ION_HEAP_TYPE_SYSTEM	0
#define ION_HEAP_TYPE_DMA	4

#define ION_FLAG_CACHED		1

#define ION_HEAP_NAME_LEN	32

struct ion_allocation_data {
	__u64 len;
	__u32 heap_id_mask;
	__u32 flags;
	__u32 fd;
	__u32 unused;
};

struct ion_heap_data {
	char name[ION_HEAP_NAME_LEN];
	__u32 type;
	__u32 heap_id;
	__u32 reserved0;
	__u32 reserved1;
	__u32 reserved2;
};

struct ion_heap_query {
	__u32 cnt;
	__u32 reserved0;
	__u64 heaps;
	__u32 reserved1;
	__u32 reserved2;
};

#define ION_IOC_MAGIC		'I'
#define ION_IOC_ALLOC		_IOWR(ION_IOC_MAGIC, 0, struct ion_allocation_data)
#define ION_IOC_HEAP_QUERY	_IOWR(ION_IOC_MAGIC, 8, struct ion_heap_query)

/* msm extensions, kept as no-ops */
#define ION_IOC_MSM_MAGIC	'M'
#define ION_IOC_PREFETCH	_IOWR(ION_IOC_MSM_MAGIC, 3, struct ion_prefetch_data)
#define ION_IOC_DRAIN		_IOWR(ION_IOC_MSM_MAGIC, 4, struct ion_prefetch_data)

struct ion_prefetch_regions {
	__u64 sizes;
	__u32 vmid;
	__u32 nr_sizes;
};

struct ion_prefetch_data {
	__u64 len;
	__u64 regions;
	__u32 heap_id;
	__u32 nr_regions;
};

#define MSM_ION_MAX_HEAPS	32

struct msm_ion_heap {
	unsigned int id;
	unsigned int type;
	const char *name;

	/* SYSTEM */
	struct dma_heap *cached;
	struct dma_heap *uncached;

	/* DMA */
	struct device *dev;
	phys_addr_t base;
	size_t size;
	struct cma *cma;		/* reusable region */
	struct gen_pool *pool;		/* no-map region */
};

static struct msm_ion_heap *msm_ion_heaps[MSM_ION_MAX_HEAPS];

/* heap names of msm-4.14/4.19, by ID, as ION_IOC_HEAP_QUERY reported them */
static const char * const msm_ion_heap_names[MSM_ION_MAX_HEAPS] = {
	[8] = "cp_mm",
	[9] = "secure_heap",
	[10] = "secure_display",
	[13] = "spss",
	[14] = "secure_carveout",
	[19] = "qsecom_ta",
	[20] = "camera_preview",
	[22] = "adsp",
	[25] = "system",
	[26] = "user_contig",
	[27] = "qsecom",
	[28] = "audio",
};

/* --- contiguous buffers from a reserved region ------------------------- */

struct msm_ion_buffer {
	struct msm_ion_heap *heap;
	phys_addr_t phys;
	size_t size;
	struct page *pages;		/* CMA only */
	bool cached;
	void *vaddr;
	unsigned int vmap_count;
	struct mutex lock;
};

static int msm_ion_attach(struct dma_buf *dmabuf,
			  struct dma_buf_attachment *attach)
{
	struct msm_ion_buffer *buf = dmabuf->priv;
	struct sg_table *table;
	int ret;

	table = kzalloc(sizeof(*table), GFP_KERNEL);
	if (!table)
		return -ENOMEM;

	ret = sg_alloc_table(table, 1, GFP_KERNEL);
	if (ret) {
		kfree(table);
		return ret;
	}
	sg_set_page(table->sgl, phys_to_page(buf->phys), buf->size, 0);
	attach->priv = table;
	return 0;
}

static void msm_ion_detach(struct dma_buf *dmabuf,
			   struct dma_buf_attachment *attach)
{
	struct sg_table *table = attach->priv;

	sg_free_table(table);
	kfree(table);
}

/*
 * No-map memory is not in the kernel linear map, so it is never in the CPU
 * caches through it and must not be cache-maintained through it either.
 */
static unsigned long msm_ion_dma_attrs(struct msm_ion_buffer *buf)
{
	return buf->heap->pool ? DMA_ATTR_SKIP_CPU_SYNC : 0;
}

static struct sg_table *msm_ion_map_dma_buf(struct dma_buf_attachment *attach,
					    enum dma_data_direction dir)
{
	struct msm_ion_buffer *buf = attach->dmabuf->priv;
	struct sg_table *table = attach->priv;
	int ret;

	ret = dma_map_sgtable(attach->dev, table, dir, msm_ion_dma_attrs(buf));
	if (ret)
		return ERR_PTR(ret);
	return table;
}

static void msm_ion_unmap_dma_buf(struct dma_buf_attachment *attach,
				  struct sg_table *table,
				  enum dma_data_direction dir)
{
	struct msm_ion_buffer *buf = attach->dmabuf->priv;

	dma_unmap_sgtable(attach->dev, table, dir, msm_ion_dma_attrs(buf));
}

static int msm_ion_begin_cpu_access(struct dma_buf *dmabuf,
				    enum dma_data_direction dir)
{
	struct msm_ion_buffer *buf = dmabuf->priv;

	if (buf->heap->cma && buf->cached)
		dma_sync_single_for_cpu(buf->heap->dev, buf->phys, buf->size, dir);
	return 0;
}

static int msm_ion_end_cpu_access(struct dma_buf *dmabuf,
				  enum dma_data_direction dir)
{
	struct msm_ion_buffer *buf = dmabuf->priv;

	if (buf->heap->cma && buf->cached)
		dma_sync_single_for_device(buf->heap->dev, buf->phys, buf->size, dir);
	return 0;
}

static int msm_ion_mmap(struct dma_buf *dmabuf, struct vm_area_struct *vma)
{
	struct msm_ion_buffer *buf = dmabuf->priv;
	unsigned long size = vma->vm_end - vma->vm_start;
	unsigned long off = vma->vm_pgoff << PAGE_SHIFT;

	if (off >= buf->size || size > buf->size - off)
		return -EINVAL;

	if (!buf->heap->cma || !buf->cached)
		vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);

	return remap_pfn_range(vma, vma->vm_start,
			       PHYS_PFN(buf->phys + off), size,
			       vma->vm_page_prot);
}

static int msm_ion_vmap(struct dma_buf *dmabuf, struct iosys_map *map)
{
	struct msm_ion_buffer *buf = dmabuf->priv;
	int ret = 0;

	mutex_lock(&buf->lock);
	if (!buf->vmap_count) {
		if (buf->heap->cma)
			buf->vaddr = page_address(buf->pages);
		else
			buf->vaddr = memremap(buf->phys, buf->size, MEMREMAP_WC);
		if (!buf->vaddr) {
			ret = -ENOMEM;
			goto out;
		}
	}
	buf->vmap_count++;
	iosys_map_set_vaddr(map, buf->vaddr);
out:
	mutex_unlock(&buf->lock);
	return ret;
}

static void msm_ion_vunmap(struct dma_buf *dmabuf, struct iosys_map *map)
{
	struct msm_ion_buffer *buf = dmabuf->priv;

	mutex_lock(&buf->lock);
	if (!--buf->vmap_count) {
		if (!buf->heap->cma)
			memunmap(buf->vaddr);
		buf->vaddr = NULL;
	}
	mutex_unlock(&buf->lock);
	iosys_map_clear(map);
}

static void msm_ion_release(struct dma_buf *dmabuf)
{
	struct msm_ion_buffer *buf = dmabuf->priv;
	struct msm_ion_heap *heap = buf->heap;

	if (buf->vmap_count && !heap->cma)
		memunmap(buf->vaddr);
	if (heap->cma)
		cma_release(heap->cma, buf->pages, buf->size >> PAGE_SHIFT);
	else
		gen_pool_free(heap->pool, buf->phys, buf->size);
	kfree(buf);
}

static const struct dma_buf_ops msm_ion_dma_buf_ops = {
	.attach = msm_ion_attach,
	.detach = msm_ion_detach,
	.map_dma_buf = msm_ion_map_dma_buf,
	.unmap_dma_buf = msm_ion_unmap_dma_buf,
	.begin_cpu_access = msm_ion_begin_cpu_access,
	.end_cpu_access = msm_ion_end_cpu_access,
	.mmap = msm_ion_mmap,
	.vmap = msm_ion_vmap,
	.vunmap = msm_ion_vunmap,
	.release = msm_ion_release,
};

static struct dma_buf *msm_ion_dma_alloc(struct msm_ion_heap *heap,
					 size_t len, unsigned int flags)
{
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);
	struct msm_ion_buffer *buf;
	struct dma_buf *dmabuf;
	void *vaddr;

	len = PAGE_ALIGN(len);
	buf = kzalloc(sizeof(*buf), GFP_KERNEL);
	if (!buf)
		return ERR_PTR(-ENOMEM);

	buf->heap = heap;
	buf->size = len;
	buf->cached = flags & ION_FLAG_CACHED;
	mutex_init(&buf->lock);

	if (heap->cma) {
		buf->pages = cma_alloc(heap->cma, len >> PAGE_SHIFT,
				       get_order(len), false);
		if (!buf->pages)
			goto nomem;
		buf->phys = page_to_phys(buf->pages);
		/* hand out zeroed memory, written back past the caches */
		memset(page_address(buf->pages), 0, len);
		dma_sync_single_for_device(heap->dev, buf->phys, len,
					   DMA_BIDIRECTIONAL);
	} else {
		buf->phys = gen_pool_alloc(heap->pool, len);
		if (!buf->phys)
			goto nomem;
		vaddr = memremap(buf->phys, len, MEMREMAP_WC);
		if (vaddr) {
			memset(vaddr, 0, len);
			memunmap(vaddr);
		}
	}

	exp_info.ops = &msm_ion_dma_buf_ops;
	exp_info.size = len;
	exp_info.flags = O_RDWR;
	exp_info.priv = buf;
	exp_info.exp_name = heap->name;
	dmabuf = dma_buf_export(&exp_info);
	if (IS_ERR(dmabuf)) {
		if (heap->cma)
			cma_release(heap->cma, buf->pages, len >> PAGE_SHIFT);
		else
			gen_pool_free(heap->pool, buf->phys, len);
		kfree(buf);
	}
	return dmabuf;

nomem:
	kfree(buf);
	return ERR_PTR(-ENOMEM);
}

/* --- /dev/ion ---------------------------------------------------------- */

static int msm_ion_alloc_fd(struct ion_allocation_data *data)
{
	unsigned long mask = data->heap_id_mask;
	struct msm_ion_heap *heap;
	struct dma_buf *dmabuf;
	unsigned int id;
	int fd;

	if (!data->len)
		return -EINVAL;

	/* like ION, try the heaps in the mask from the highest ID down */
	for (id = MSM_ION_MAX_HEAPS; id-- > 0;) {
		if (!(mask & BIT(id)) || !msm_ion_heaps[id])
			continue;
		heap = msm_ion_heaps[id];

		if (heap->type == ION_HEAP_TYPE_SYSTEM) {
			struct dma_heap *dh = heap->cached;

			if (!(data->flags & ION_FLAG_CACHED) && heap->uncached)
				dh = heap->uncached;
			dmabuf = dma_heap_buffer_alloc(dh, data->len,
						       O_RDWR | O_CLOEXEC, 0);
		} else {
			dmabuf = msm_ion_dma_alloc(heap, data->len, data->flags);
		}
		if (IS_ERR(dmabuf))
			continue;

		fd = dma_buf_fd(dmabuf, O_CLOEXEC);
		if (fd < 0) {
			dma_buf_put(dmabuf);
			return fd;
		}
		data->fd = fd;
		return 0;
	}
	return -ENOMEM;
}

static int msm_ion_query(struct ion_heap_query *query)
{
	struct ion_heap_data __user *out = u64_to_user_ptr(query->heaps);
	unsigned int id, cnt = 0, max = query->cnt;

	for (id = 0; id < MSM_ION_MAX_HEAPS; id++) {
		struct msm_ion_heap *heap = msm_ion_heaps[id];
		struct ion_heap_data hd = {};

		if (!heap)
			continue;
		if (out && cnt < max) {
			strscpy(hd.name, heap->name, sizeof(hd.name));
			hd.type = heap->type;
			hd.heap_id = heap->id;
			if (copy_to_user(&out[cnt], &hd, sizeof(hd)))
				return -EFAULT;
		}
		cnt++;
	}
	query->cnt = out ? min(cnt, max) : cnt;
	return 0;
}

static long msm_ion_ioctl(struct file *file, unsigned int cmd,
			  unsigned long arg)
{
	void __user *uarg = (void __user *)arg;
	int ret;

	switch (cmd) {
	case ION_IOC_ALLOC: {
		struct ion_allocation_data data;

		if (copy_from_user(&data, uarg, sizeof(data)))
			return -EFAULT;
		ret = msm_ion_alloc_fd(&data);
		if (ret)
			return ret;
		if (copy_to_user(uarg, &data, sizeof(data)))
			return -EFAULT;
		return 0;
	}
	case ION_IOC_HEAP_QUERY: {
		struct ion_heap_query query;

		if (copy_from_user(&query, uarg, sizeof(query)))
			return -EFAULT;
		ret = msm_ion_query(&query);
		if (ret)
			return ret;
		if (copy_to_user(uarg, &query, sizeof(query)))
			return -EFAULT;
		return 0;
	}
	case ION_IOC_PREFETCH:
	case ION_IOC_DRAIN:
		return 0;
	default:
		/* includes ION_IOC_FREE: libion takes -ENOTTY as the new API */
		return -ENOTTY;
	}
}

static const struct file_operations msm_ion_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = msm_ion_ioctl,
	.compat_ioctl = compat_ptr_ioctl,
};

static struct miscdevice msm_ion_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "ion",
	.fops = &msm_ion_fops,
};

/* --- heaps from the DT ------------------------------------------------- */

static int msm_ion_add_system_heap(struct msm_ion_heap *heap)
{
	heap->type = ION_HEAP_TYPE_SYSTEM;
	heap->cached = dma_heap_find("system");
	if (!heap->cached)
		return -EPROBE_DEFER;
	heap->uncached = dma_heap_find("system-uncached");
	return 0;
}

static int msm_ion_add_dma_heap(struct platform_device *pdev,
				struct device_node *np,
				struct msm_ion_heap *heap)
{
	struct device_node *mem_np;
	struct reserved_mem *rmem;
	int ret;

	mem_np = of_parse_phandle(np, "memory-region", 0);
	if (!mem_np)
		return -EINVAL;
	rmem = of_reserved_mem_lookup(mem_np);
	if (!rmem) {
		of_node_put(mem_np);
		return -EINVAL;
	}

	heap->type = ION_HEAP_TYPE_DMA;
	heap->dev = &pdev->dev;
	heap->base = rmem->base;
	heap->size = rmem->size;

	if (of_property_read_bool(mem_np, "reusable")) {
		/* a reusable shared-dma-pool is a CMA area */
		heap->cma = rmem->priv;
		ret = heap->cma ? 0 : -EINVAL;
	} else {
		heap->pool = devm_gen_pool_create(&pdev->dev, PAGE_SHIFT, -1,
						  heap->name);
		ret = heap->pool ? gen_pool_add(heap->pool, rmem->base,
						rmem->size, -1) : -ENOMEM;
	}
	of_node_put(mem_np);
	return ret;
}

static int msm_ion_probe(struct platform_device *pdev)
{
	struct device_node *np;
	int ret;

	dma_coerce_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));

	for_each_available_child_of_node(pdev->dev.of_node, np) {
		struct msm_ion_heap *heap;
		const char *type;
		u32 id;

		if (of_property_read_u32(np, "reg", &id) || id >= MSM_ION_MAX_HEAPS ||
		    of_property_read_string(np, "qcom,ion-heap-type", &type))
			continue;

		heap = devm_kzalloc(&pdev->dev, sizeof(*heap), GFP_KERNEL);
		if (!heap) {
			of_node_put(np);
			return -ENOMEM;
		}
		heap->id = id;
		heap->name = msm_ion_heap_names[id];
		if (!heap->name)
			heap->name = devm_kasprintf(&pdev->dev, GFP_KERNEL,
						    "heap%u", id);

		if (!strcmp(type, "SYSTEM"))
			ret = msm_ion_add_system_heap(heap);
		else if (!strcmp(type, "DMA"))
			ret = msm_ion_add_dma_heap(pdev, np, heap);
		else
			ret = -EOPNOTSUPP;

		if (ret == -EPROBE_DEFER) {
			of_node_put(np);
			return ret;
		}
		if (ret) {
			dev_warn(&pdev->dev, "heap %u (%s): %d, skipped\n",
				 id, type, ret);
			continue;
		}
		msm_ion_heaps[id] = heap;
		dev_info(&pdev->dev, "heap %u: %s (%s)\n", id, heap->name, type);
	}

	ret = misc_register(&msm_ion_misc);
	if (ret)
		memset(msm_ion_heaps, 0, sizeof(msm_ion_heaps));
	return ret;
}

static const struct of_device_id msm_ion_of_match[] = {
	{ .compatible = "qcom,msm-ion" },
	{ }
};

static struct platform_driver msm_ion_driver = {
	.probe = msm_ion_probe,
	.driver = {
		.name = "msm-ion",
		.of_match_table = msm_ion_of_match,
		.suppress_bind_attrs = true,
	},
};
builtin_platform_driver(msm_ion_driver);
