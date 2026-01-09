/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef _QCOM_TGU_H
#define _QCOM_TGU_H

/* Register addresses */
#define TGU_CONTROL 0x0000
#define TGU_LAR		0xfb0
#define TGU_UNLOCK_OFFSET	0xc5acce55

static inline void TGU_LOCK(void __iomem *addr)
{
	do {
		/* Wait for things to settle */
		mb();
		writel_relaxed(0x0, addr + TGU_LAR);
	} while (0);
}

static inline void TGU_UNLOCK(void __iomem *addr)
{
	do {
		writel_relaxed(TGU_UNLOCK_OFFSET, addr + TGU_LAR);
		/* Make sure everyone has seen this */
		mb();
	} while (0);
}

/**
 * struct tgu_drvdata - Data structure for a TGU (Trigger Generator Unit)
 * @base: Memory-mapped base address of the TGU device
 * @dev: Pointer to the associated device structure
 * @lock: Spinlock for handling concurrent access
 * @enable: Flag indicating whether the TGU device is enabled
 *
 * This structure defines the data associated with a TGU device,
 * including its base address, device pointers, clock, spinlock for
 * synchronization, trigger data pointers, maximum limits for various
 * trigger-related parameters, and enable status.
 */
struct tgu_drvdata {
	void __iomem *base;
	struct device *dev;
	spinlock_t lock;
	bool enable;
};

#endif
