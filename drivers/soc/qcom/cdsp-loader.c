// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2012-2014, 2017-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/remoteproc.h>
#include <linux/slab.h>
#include <linux/sysfs.h>

#define BOOT_CMD 1
#define IMAGE_UNLOAD_CMD 0

/*
 * One loader per DSP: the "qcom,proc-img-to-load" name ("adsp", "cdsp")
 * picks the /sys/kernel/boot_<name>/boot node the vendor init writes to.
 */
struct cdsp_loader_private {
	struct device *dev;
	struct rproc *rproc;
	struct kobject *boot_obj;
	struct kobj_attribute boot_attr;
	struct attribute *attrs[2];
	struct attribute_group attr_group;
	struct mutex lock;
	const char *img_name;
	bool booted;
};

static int cdsp_loader_do(struct cdsp_loader_private *priv)
{
	int rc = 0;

	mutex_lock(&priv->lock);
	if (!priv->booted) {
		dev_dbg(priv->dev, "calling rproc_boot on %s\n", priv->img_name);
		rc = rproc_boot(priv->rproc);
		if (rc)
			dev_err(priv->dev, "%s: rproc_boot failed with error %d\n",
				priv->img_name, rc);
		else
			priv->booted = true;
	}
	mutex_unlock(&priv->lock);

	return rc;
}

static void cdsp_loader_unload(struct cdsp_loader_private *priv)
{
	mutex_lock(&priv->lock);
	if (priv->booted) {
		dev_dbg(priv->dev, "calling rproc_shutdown on %s\n",
			priv->img_name);
		rproc_shutdown(priv->rproc);
		priv->booted = false;
	}
	mutex_unlock(&priv->lock);
}

static ssize_t cdsp_boot_store(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct cdsp_loader_private *priv =
		container_of(attr, struct cdsp_loader_private, boot_attr);
	u32 boot = 0;
	int ret;

	ret = kstrtou32(buf, 0, &boot);
	if (ret)
		return ret;

	if (boot == BOOT_CMD)
		cdsp_loader_do(priv);
	else if (boot == IMAGE_UNLOAD_CMD)
		cdsp_loader_unload(priv);

	return count;
}

static int cdsp_loader_init_sysfs(struct cdsp_loader_private *priv)
{
	char *name;
	int ret;

	sysfs_attr_init(&priv->boot_attr.attr);
	priv->boot_attr.attr.name = "boot";
	priv->boot_attr.attr.mode = 0220;
	priv->boot_attr.store = cdsp_boot_store;
	priv->attrs[0] = &priv->boot_attr.attr;
	priv->attr_group.attrs = priv->attrs;

	name = devm_kasprintf(priv->dev, GFP_KERNEL, "boot_%s", priv->img_name);
	if (!name)
		return -ENOMEM;

	priv->boot_obj = kobject_create_and_add(name, kernel_kobj);
	if (!priv->boot_obj)
		return -ENOMEM;

	ret = sysfs_create_group(priv->boot_obj, &priv->attr_group);
	if (ret) {
		kobject_put(priv->boot_obj);
		priv->boot_obj = NULL;
	}

	return ret;
}

static void cdsp_loader_remove(struct platform_device *pdev)
{
	struct cdsp_loader_private *priv = platform_get_drvdata(pdev);

	sysfs_remove_group(priv->boot_obj, &priv->attr_group);
	kobject_put(priv->boot_obj);
	cdsp_loader_unload(priv);
	rproc_put(priv->rproc);
}

static int cdsp_loader_probe(struct platform_device *pdev)
{
	struct cdsp_loader_private *priv;
	phandle rproc_phandle;
	int ret;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = &pdev->dev;
	mutex_init(&priv->lock);

	ret = of_property_read_string(pdev->dev.of_node,
				      "qcom,proc-img-to-load", &priv->img_name);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "error reading qcom,proc-img-to-load\n");

	ret = of_property_read_u32(pdev->dev.of_node, "qcom,rproc-handle",
				   &rproc_phandle);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "error reading rproc phandle\n");

	priv->rproc = rproc_get_by_phandle(rproc_phandle);
	if (!priv->rproc)
		return -EPROBE_DEFER;

	ret = cdsp_loader_init_sysfs(priv);
	if (ret) {
		rproc_put(priv->rproc);
		return dev_err_probe(&pdev->dev, ret, "error in initing sysfs\n");
	}

	platform_set_drvdata(pdev, priv);

	return 0;
}

static const struct of_device_id cdsp_loader_dt_match[] = {
	{ .compatible = "qcom,adsp-loader" },
	{ .compatible = "qcom,cdsp-loader" },
	{ }
};
MODULE_DEVICE_TABLE(of, cdsp_loader_dt_match);

static struct platform_driver cdsp_loader_driver = {
	.driver = {
		.name = "cdsp-loader",
		.of_match_table = cdsp_loader_dt_match,
	},
	.probe = cdsp_loader_probe,
	.remove = cdsp_loader_remove,
};
module_platform_driver(cdsp_loader_driver);

MODULE_DESCRIPTION("CDSP Loader module");
MODULE_LICENSE("GPL");
