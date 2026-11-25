/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Compatibility layer for the generic Qualcomm PAS API.
 *
 * qcom-next provides a generic PAS service (drivers/firmware/qcom/qcom_pas.c)
 * with SCM and TEE backends. This tree only has the SCM implementation, so
 * map the generic qcom_pas_* API used by the video drivers onto the
 * existing qcom_scm_pas_* calls.
 */
#ifndef __QCOM_PAS_H
#define __QCOM_PAS_H

#include <linux/firmware/qcom/qcom_scm.h>

#define QCOM_PAS_BACKEND_SCM	"qcom_scm"
#define QCOM_PAS_BACKEND_TEE	"qcom-pas-tee"

#define qcom_pas_context	qcom_scm_pas_context

static inline const char *qcom_pas_get_backend(void)
{
	return qcom_scm_is_available() ? QCOM_PAS_BACKEND_SCM : NULL;
}

static inline bool qcom_pas_is_available(void)
{
	return qcom_scm_is_available();
}

static inline struct qcom_pas_context *
devm_qcom_pas_context_alloc(struct device *dev, u32 pas_id,
			    phys_addr_t mem_phys, size_t mem_size)
{
	return devm_qcom_scm_pas_context_alloc(dev, pas_id, mem_phys, mem_size);
}

static inline int qcom_pas_init_image(u32 pas_id, const void *metadata, size_t size,
				      struct qcom_pas_context *ctx)
{
	return qcom_scm_pas_init_image(pas_id, metadata, size, ctx);
}

static inline struct resource_table *
qcom_pas_get_rsc_table(struct qcom_pas_context *ctx, void *input_rt,
		       size_t input_rt_size, size_t *output_rt_size)
{
	return qcom_scm_pas_get_rsc_table(ctx, input_rt, input_rt_size, output_rt_size);
}

static inline int qcom_pas_mem_setup(u32 pas_id, phys_addr_t addr, phys_addr_t size)
{
	return qcom_scm_pas_mem_setup(pas_id, addr, size);
}

static inline int qcom_pas_auth_and_reset(u32 pas_id)
{
	return qcom_scm_pas_auth_and_reset(pas_id);
}

static inline int qcom_pas_prepare_and_auth_reset(struct qcom_pas_context *ctx)
{
	return qcom_scm_pas_prepare_and_auth_reset(ctx);
}

static inline int qcom_pas_set_remote_state(u32 state, u32 pas_id)
{
	return qcom_scm_set_remote_state(state, pas_id);
}

static inline int qcom_pas_shutdown(u32 pas_id)
{
	return qcom_scm_pas_shutdown(pas_id);
}

static inline bool qcom_pas_supported(u32 pas_id)
{
	return qcom_scm_pas_supported(pas_id);
}

static inline void qcom_pas_metadata_release(struct qcom_pas_context *ctx)
{
	qcom_scm_pas_metadata_release(ctx);
}

#endif /* __QCOM_PAS_H */
