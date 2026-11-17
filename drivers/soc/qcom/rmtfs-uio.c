// SPDX-License-Identifier: GPL-2.0-only
/*
 * The remote filesystem shared memory as a UIO device named "rmtfs".
 *
 * The rmt_storage daemon of Android vendors built for downstream kernels
 * serves the modem's EFS from this memory: it finds it by name under
 * /sys/class/uio, maps /dev/uioN and hands its address to the modem.
 */

#include <linux/dma-mapping.h>
#include <linux/firmware/qcom/qcom_scm.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/uio_driver.h>

static int rmtfs_uio_mmap(struct uio_info *info, struct vm_area_struct *vma)
{
	struct uio_mem *mem = &info->mem[0];

	if (vma->vm_pgoff || vma->vm_end - vma->vm_start > mem->size)
		return -EINVAL;

	vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
	return remap_pfn_range(vma, vma->vm_start, mem->addr >> PAGE_SHIFT,
			       vma->vm_end - vma->vm_start, vma->vm_page_prot);
}

static int rmtfs_uio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct uio_info *info;
	dma_addr_t phys;
	void *virt;
	u32 size = SZ_2M;
	u32 vmid;
	int ret;

	of_property_read_u32(dev->of_node, "qcom,size", &size);

	if (!qcom_scm_is_available())
		return -EPROBE_DEFER;

	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32));
	if (ret)
		return ret;

	/*
	 * Keep 4K free on either side so that nothing else ends up next to
	 * the memory the modem may access.  It is never freed: the modem
	 * may hold on to it.
	 */
	virt = dma_alloc_coherent(dev, size + SZ_8K, &phys, GFP_KERNEL);
	if (!virt)
		return -ENOMEM;
	phys += SZ_4K;

	if (!of_property_read_u32(dev->of_node, "qcom,vmid", &vmid)) {
		struct qcom_scm_vmperm perms[] = {
			{ QCOM_SCM_VMID_HLOS, QCOM_SCM_PERM_RW },
			{ vmid, QCOM_SCM_PERM_RW },
		};
		u64 src = BIT(QCOM_SCM_VMID_HLOS);

		ret = qcom_scm_assign_mem(phys, size, &src, perms,
					  ARRAY_SIZE(perms));
		if (ret)
			dev_warn(dev, "failed to share %pad with VMID %u: %d\n",
				 &phys, vmid, ret);
	}

	info = devm_kzalloc(dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->name = "rmtfs";
	info->version = "1.0";
	info->mmap = rmtfs_uio_mmap;
	info->mem[0].name = "rmtfs";
	info->mem[0].addr = phys;
	info->mem[0].size = size;
	info->mem[0].memtype = UIO_MEM_PHYS;

	ret = devm_uio_register_device(dev, info);
	if (ret)
		return ret;

	dev_info(dev, "rmtfs at %pad, %u bytes\n", &phys, size);
	return 0;
}

static const struct of_device_id rmtfs_uio_of_match[] = {
	{ .compatible = "qcom,rmtfs-uio" },
	{ }
};
MODULE_DEVICE_TABLE(of, rmtfs_uio_of_match);

static struct platform_driver rmtfs_uio_driver = {
	.probe = rmtfs_uio_probe,
	.driver = {
		.name = "qcom-rmtfs-uio",
		.of_match_table = rmtfs_uio_of_match,
		.suppress_bind_attrs = true,
	},
};
module_platform_driver(rmtfs_uio_driver);

MODULE_DESCRIPTION("Qualcomm remote filesystem shared memory over UIO");
MODULE_LICENSE("GPL");
