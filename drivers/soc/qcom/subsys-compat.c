// SPDX-License-Identifier: GPL-2.0-only
/*
 * The subsystem interface of downstream kernels, on top of remoteproc.
 *
 * Android vendors built for downstream kernels look a peripheral up under
 * /sys/bus/msm_subsys/devices/subsysN/{name,state} and power it up by
 * opening /dev/subsys_<name>, keeping it up for as long as that file stays
 * open.  The peripheral manager boots the modem this way.
 */

#include <linux/device.h>
#include <linux/fs.h>
#include <linux/idr.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/remoteproc.h>
#include <linux/slab.h>

struct subsys_compat {
	struct device dev;
	struct miscdevice misc;
	struct rproc *rproc;
	const char *name;
	char misc_name[32];
};

static const struct bus_type msm_subsys_bus = {
	.name = "msm_subsys",
};

static DEFINE_IDA(subsys_ida);

static ssize_t name_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct subsys_compat *ss = container_of(dev, struct subsys_compat, dev);

	return sysfs_emit(buf, "%s\n", ss->name);
}
static DEVICE_ATTR_RO(name);

static ssize_t state_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct subsys_compat *ss = container_of(dev, struct subsys_compat, dev);

	return sysfs_emit(buf, "%s\n", ss->rproc->state == RPROC_RUNNING ?
			  "ONLINE" : "OFFLINE");
}
static DEVICE_ATTR_RO(state);

/* "related" restarts only the peripheral on a crash, "system" the phone */
static ssize_t restart_level_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct subsys_compat *ss = container_of(dev, struct subsys_compat, dev);

	return sysfs_emit(buf, "%s\n", ss->rproc->recovery_disabled ?
			  "SYSTEM" : "RELATED");
}

static ssize_t restart_level_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct subsys_compat *ss = container_of(dev, struct subsys_compat, dev);

	if (sysfs_streq(buf, "related") || sysfs_streq(buf, "RELATED"))
		ss->rproc->recovery_disabled = false;
	else if (sysfs_streq(buf, "system") || sysfs_streq(buf, "SYSTEM"))
		ss->rproc->recovery_disabled = true;
	else
		return -EINVAL;

	return count;
}
static DEVICE_ATTR_RW(restart_level);

static struct attribute *subsys_attrs[] = {
	&dev_attr_name.attr,
	&dev_attr_state.attr,
	&dev_attr_restart_level.attr,
	NULL
};
ATTRIBUTE_GROUPS(subsys);

static int subsys_open(struct inode *inode, struct file *file)
{
	struct subsys_compat *ss = container_of(file->private_data,
						struct subsys_compat, misc);
	int ret;

	ret = rproc_boot(ss->rproc);
	if (ret)
		dev_err(&ss->dev, "failed to boot %s: %d\n", ss->name, ret);

	return ret;
}

static int subsys_release(struct inode *inode, struct file *file)
{
	struct subsys_compat *ss = container_of(file->private_data,
						struct subsys_compat, misc);

	rproc_shutdown(ss->rproc);
	return 0;
}

static const struct file_operations subsys_fops = {
	.owner = THIS_MODULE,
	.open = subsys_open,
	.release = subsys_release,
	.llseek = noop_llseek,
};

static void subsys_dev_release(struct device *dev)
{
	struct subsys_compat *ss = container_of(dev, struct subsys_compat, dev);

	ida_free(&subsys_ida, ss->dev.id);
	rproc_put(ss->rproc);
	kfree(ss);
}

static int subsys_compat_probe(struct platform_device *pdev)
{
	struct subsys_compat *ss;
	struct rproc *rproc;
	phandle ph;
	int id, ret;

	if (of_property_read_u32(pdev->dev.of_node, "qcom,rproc-handle", &ph))
		return -EINVAL;

	rproc = rproc_get_by_phandle(ph);
	if (!rproc)
		return -EPROBE_DEFER;

	ss = kzalloc(sizeof(*ss), GFP_KERNEL);
	if (!ss) {
		rproc_put(rproc);
		return -ENOMEM;
	}
	ss->rproc = rproc;

	if (of_property_read_string(pdev->dev.of_node, "qcom,subsys-name",
				    &ss->name))
		ss->name = rproc->name;

	id = ida_alloc(&subsys_ida, GFP_KERNEL);
	if (id < 0) {
		rproc_put(rproc);
		kfree(ss);
		return id;
	}

	ss->dev.id = id;
	ss->dev.bus = &msm_subsys_bus;
	ss->dev.parent = &pdev->dev;
	ss->dev.groups = subsys_groups;
	ss->dev.release = subsys_dev_release;
	dev_set_name(&ss->dev, "subsys%d", id);

	ret = device_register(&ss->dev);
	if (ret) {
		put_device(&ss->dev);
		return ret;
	}

	snprintf(ss->misc_name, sizeof(ss->misc_name), "subsys_%s", ss->name);
	ss->misc.minor = MISC_DYNAMIC_MINOR;
	ss->misc.name = ss->misc_name;
	ss->misc.fops = &subsys_fops;
	ss->misc.parent = &ss->dev;

	ret = misc_register(&ss->misc);
	if (ret) {
		device_unregister(&ss->dev);
		return ret;
	}

	platform_set_drvdata(pdev, ss);
	dev_info(&pdev->dev, "%s is %s, /dev/%s\n", ss->name,
		 dev_name(&ss->dev), ss->misc_name);
	return 0;
}

static void subsys_compat_remove(struct platform_device *pdev)
{
	struct subsys_compat *ss = platform_get_drvdata(pdev);

	misc_deregister(&ss->misc);
	device_unregister(&ss->dev);
}

static const struct of_device_id subsys_compat_of_match[] = {
	{ .compatible = "qcom,subsys-compat" },
	{ }
};
MODULE_DEVICE_TABLE(of, subsys_compat_of_match);

static struct platform_driver subsys_compat_driver = {
	.probe = subsys_compat_probe,
	.remove = subsys_compat_remove,
	.driver = {
		.name = "qcom-subsys-compat",
		.of_match_table = subsys_compat_of_match,
	},
};

static int __init subsys_compat_init(void)
{
	int ret;

	ret = bus_register(&msm_subsys_bus);
	if (ret)
		return ret;

	ret = platform_driver_register(&subsys_compat_driver);
	if (ret)
		bus_unregister(&msm_subsys_bus);

	return ret;
}
module_init(subsys_compat_init);

static void __exit subsys_compat_exit(void)
{
	platform_driver_unregister(&subsys_compat_driver);
	bus_unregister(&msm_subsys_bus);
}
module_exit(subsys_compat_exit);

MODULE_DESCRIPTION("Qualcomm downstream subsystem interface on remoteproc");
MODULE_LICENSE("GPL");
