// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2022, The Linux Foundation. All rights reserved.

#include <linux/export.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>

#include "lpass-macro-common.h"

static DEFINE_MUTEX(lpass_codec_mutex);
static enum lpass_codec_version lpass_codec_version;

struct lpass_macro *lpass_macro_pds_init(struct device *dev)
{
	struct lpass_macro *l_pds;
	int ret;

	if (!of_property_present(dev->of_node, "power-domains"))
		return NULL;

	l_pds = devm_kzalloc(dev, sizeof(*l_pds), GFP_KERNEL);
	if (!l_pds)
		return ERR_PTR(-ENOMEM);

	l_pds->macro_pd = dev_pm_domain_attach_by_name(dev, "macro");
	if (IS_ERR_OR_NULL(l_pds->macro_pd)) {
		ret = l_pds->macro_pd ? PTR_ERR(l_pds->macro_pd) : -ENODATA;
		goto macro_err;
	}

	l_pds->macro_link = device_link_add(dev, l_pds->macro_pd,
					    DL_FLAG_STATELESS | DL_FLAG_PM_RUNTIME);
	if (!l_pds->macro_link) {
		ret = -EINVAL;
		goto macro_link_err;
	}

	l_pds->dcodec_pd = dev_pm_domain_attach_by_name(dev, "dcodec");
	if (IS_ERR_OR_NULL(l_pds->dcodec_pd)) {
		ret = l_pds->dcodec_pd ? PTR_ERR(l_pds->dcodec_pd) : -ENODATA;
		goto dcodec_err;
	}

	l_pds->dcodec_link = device_link_add(dev, l_pds->dcodec_pd,
					     DL_FLAG_STATELESS | DL_FLAG_PM_RUNTIME);
	if (!l_pds->dcodec_link) {
		ret = -EINVAL;
		goto dcodec_link_err;
	}

	return l_pds;

dcodec_link_err:
	dev_pm_domain_detach(l_pds->dcodec_pd, false);
dcodec_err:
	device_link_del(l_pds->macro_link);
macro_link_err:
	dev_pm_domain_detach(l_pds->macro_pd, false);
macro_err:
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(lpass_macro_pds_init);

void lpass_macro_pds_exit(struct lpass_macro *pds)
{
	if (pds) {
		device_link_del(pds->dcodec_link);
		dev_pm_domain_detach(pds->dcodec_pd, false);
		device_link_del(pds->macro_link);
		dev_pm_domain_detach(pds->macro_pd, false);
	}
}
EXPORT_SYMBOL_GPL(lpass_macro_pds_exit);

void lpass_macro_set_codec_version(enum lpass_codec_version version)
{
	mutex_lock(&lpass_codec_mutex);
	lpass_codec_version = version;
	mutex_unlock(&lpass_codec_mutex);
}
EXPORT_SYMBOL_GPL(lpass_macro_set_codec_version);

enum lpass_codec_version lpass_macro_get_codec_version(void)
{
	enum lpass_codec_version ver;

	mutex_lock(&lpass_codec_mutex);
	ver = lpass_codec_version;
	mutex_unlock(&lpass_codec_mutex);

	return ver;
}
EXPORT_SYMBOL_GPL(lpass_macro_get_codec_version);

MODULE_DESCRIPTION("Common macro driver");
MODULE_LICENSE("GPL");
