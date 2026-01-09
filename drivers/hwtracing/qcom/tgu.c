// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/amba/bus.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pm_runtime.h>

#include "tgu.h"

static void tgu_write_all_hw_regs(struct tgu_drvdata *drvdata)
{
	TGU_UNLOCK(drvdata->base);
	/* Enable TGU to program the triggers */
	writel(1, drvdata->base + TGU_CONTROL);
	TGU_LOCK(drvdata->base);
}

static int tgu_enable(struct device *dev)
{
	struct tgu_drvdata *drvdata = dev_get_drvdata(dev);

	guard(spinlock)(&drvdata->lock);
	if (drvdata->enable)
		return -EBUSY;

	tgu_write_all_hw_regs(drvdata);
	drvdata->enable = true;

	return 0;
}

static void tgu_disable(struct device *dev)
{
	struct tgu_drvdata *drvdata = dev_get_drvdata(dev);

	guard(spinlock)(&drvdata->lock);
	if (drvdata->enable) {
		TGU_UNLOCK(drvdata->base);
		writel(0, drvdata->base + TGU_CONTROL);
		TGU_LOCK(drvdata->base);

		drvdata->enable = false;
	}
}

static ssize_t enable_tgu_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct tgu_drvdata *drvdata = dev_get_drvdata(dev);
	bool enabled;

	guard(spinlock)(&drvdata->lock);
	enabled = drvdata->enable;

	return sysfs_emit(buf, "%d\n", enabled ? 1 : 0);
}

/* enable_tgu_store - Configure Trace and Gating Unit (TGU) triggers. */
static ssize_t enable_tgu_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf,
				size_t size)
{
	unsigned long val;
	int ret = 0;

	ret = kstrtoul(buf, 0, &val);
	if (ret)
		return ret;

	if (val) {
		ret = pm_runtime_resume_and_get(dev);
		if (ret)
			return ret;
		ret = tgu_enable(dev);
		if (ret) {
			pm_runtime_put(dev);
			return ret;
		}
	} else {
		tgu_disable(dev);
		pm_runtime_put(dev);
	}

	return size;
}
static DEVICE_ATTR_RW(enable_tgu);

static struct attribute *tgu_common_attrs[] = {
	&dev_attr_enable_tgu.attr,
	NULL,
};

static const struct attribute_group tgu_common_grp = {
	.attrs = tgu_common_attrs,
	NULL,
};

static const struct attribute_group *tgu_attr_groups[] = {
	&tgu_common_grp,
	NULL,
};

static int tgu_probe(struct amba_device *adev, const struct amba_id *id)
{
	struct device *dev = &adev->dev;
	struct tgu_drvdata *drvdata;
	int ret;

	drvdata = devm_kzalloc(dev, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	drvdata->dev = &adev->dev;
	dev_set_drvdata(dev, drvdata);

	drvdata->base = devm_ioremap_resource(dev, &adev->res);
	if (IS_ERR(drvdata->base))
		return PTR_ERR(drvdata->base);

	spin_lock_init(&drvdata->lock);

	ret = sysfs_create_groups(&dev->kobj, tgu_attr_groups);
	if (ret) {
		dev_err(dev, "failed to create sysfs groups: %d\n", ret);
		return ret;
	}

	drvdata->enable = false;

	pm_runtime_put(&adev->dev);
	return 0;
}

static void tgu_remove(struct amba_device *adev)
{
	struct device *dev = &adev->dev;

	sysfs_remove_groups(&dev->kobj, tgu_attr_groups);

	tgu_disable(dev);
}

static const struct amba_id tgu_ids[] = {
	{
		.id = 0x000f0e00,
		.mask = 0x000fffff,
	},
	{ 0, 0, NULL },
};

MODULE_DEVICE_TABLE(amba, tgu_ids);

static struct amba_driver tgu_driver = {
	.drv = {
		.name = "qcom-tgu",
		.suppress_bind_attrs = true,
	},
	.probe = tgu_probe,
	.remove = tgu_remove,
	.id_table = tgu_ids,
};

module_amba_driver(tgu_driver);

MODULE_AUTHOR("Songwei Chai <songwei.chai@oss.qualcomm.com>");
MODULE_AUTHOR("Jinlong Mao <jinlong.mao@oss.qualcomm.com>");
MODULE_DESCRIPTION("Qualcomm Trigger Generation Unit driver");
MODULE_LICENSE("GPL");
