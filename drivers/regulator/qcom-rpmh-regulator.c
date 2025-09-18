// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2018-2021, The Linux Foundation. All rights reserved.
// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.

#define pr_fmt(fmt) "%s: " fmt, __func__

#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>

#include <soc/qcom/cmd-db.h>
#include <soc/qcom/rpmh.h>

#include <dt-bindings/regulator/qcom,rpmh-regulator.h>

/**
 * enum rpmh_regulator_type - supported RPMh accelerator types
 * @VRM:	RPMh VRM accelerator which supports voting on enable, voltage,
 *		and mode of LDO, SMPS, and BOB type PMIC regulators.
 * @XOB:	RPMh XOB accelerator which supports voting on the enable state
 *		of PMIC regulators.
 */
enum rpmh_regulator_type {
	VRM,
	XOB,
};

/**
 * enum regulator_hw_type - supported regulator types
 * @SMPS:			Switch mode power supply.
 * @LDO:			Linear Dropout regulator.
 * @BOB:			Buck/Boost type regulator.
 * @VS:			Simple voltage ON/OFF switch.
 * @NUM_REGULATOR_TYPES:	Number of regulator types.
 */
enum regulator_hw_type {
	SMPS,
	LDO,
	BOB,
	VS,
	NUM_REGULATOR_TYPES,
};

struct resource_name_formats {
	const char *rsc_name_fmt;
	const char *rsc_name_fmt1;
};

static const struct resource_name_formats vreg_rsc_name_lookup[NUM_REGULATOR_TYPES] = {
	[SMPS]	= {"S%d%s", "smp%s%d"},
	[LDO]	= {"L%d%s", "ldo%s%d"},
	[BOB]	= {"B%d%s", "bob%s%d"},
	[VS]	= {"VS%d%s", "vs%s%d"},
};

#define RPMH_REGULATOR_REG_VRM_VOLTAGE		0x0
#define RPMH_REGULATOR_REG_ENABLE		0x4
#define RPMH_REGULATOR_REG_VRM_MODE		0x8

#define PMIC4_LDO_MODE_RETENTION		4
#define PMIC4_LDO_MODE_LPM			5
#define PMIC4_LDO_MODE_HPM			7

#define PMIC4_SMPS_MODE_RETENTION		4
#define PMIC4_SMPS_MODE_PFM			5
#define PMIC4_SMPS_MODE_AUTO			6
#define PMIC4_SMPS_MODE_PWM			7

#define PMIC4_BOB_MODE_PASS			0
#define PMIC4_BOB_MODE_PFM			1
#define PMIC4_BOB_MODE_AUTO			2
#define PMIC4_BOB_MODE_PWM			3

#define PMIC5_LDO_MODE_RETENTION		3
#define PMIC5_LDO_MODE_LPM			4
#define PMIC5_LDO_MODE_HPM			7

#define PMIC5_SMPS_MODE_RETENTION		3
#define PMIC5_SMPS_MODE_PFM			4
#define PMIC5_SMPS_MODE_AUTO			6
#define PMIC5_SMPS_MODE_PWM			7

#define PMIC5_BOB_MODE_PASS			2
#define PMIC5_BOB_MODE_PFM			4
#define PMIC5_BOB_MODE_AUTO			6
#define PMIC5_BOB_MODE_PWM			7

#define PMIC530_LDO_MODE_RETENTION		3
#define PMIC530_LDO_MODE_LPM			4
#define PMIC530_LDO_MODE_OPM			5
#define PMIC530_LDO_MODE_HPM			7

#define RPMH_VREG(_name, _vreg_hw_type, _index, _hw_data, _supply_name) \
{ \
	.name		= _name, \
	.vreg_hw_type	= _vreg_hw_type, \
	.index		= _index, \
	.hw_data	= _hw_data, \
	.supply_name	= _supply_name, \
}

static const struct rpmh_vreg_init_data pm8998_vreg_data[] = {
	RPMH_VREG("smps1",  SMPS, 1,  &pmic4_ftsmps426, "vdd-s1"),
	RPMH_VREG("smps2",  SMPS, 2,  &pmic4_ftsmps426, "vdd-s2"),
	RPMH_VREG("smps3",  SMPS, 3,  &pmic4_hfsmps3,   "vdd-s3"),
	RPMH_VREG("smps4",  SMPS, 4,  &pmic4_hfsmps3,   "vdd-s4"),
	RPMH_VREG("smps5",  SMPS, 5,  &pmic4_hfsmps3,   "vdd-s5"),
	RPMH_VREG("smps6",  SMPS, 6,  &pmic4_ftsmps426, "vdd-s6"),
	RPMH_VREG("smps7",  SMPS, 7,  &pmic4_ftsmps426, "vdd-s7"),
	RPMH_VREG("smps8",  SMPS, 8,  &pmic4_ftsmps426, "vdd-s8"),
	RPMH_VREG("smps9",  SMPS, 9,  &pmic4_ftsmps426, "vdd-s9"),
	RPMH_VREG("smps10", SMPS, 10, &pmic4_ftsmps426, "vdd-s10"),
	RPMH_VREG("smps11", SMPS, 11, &pmic4_ftsmps426, "vdd-s11"),
	RPMH_VREG("smps12", SMPS, 12, &pmic4_ftsmps426, "vdd-s12"),
	RPMH_VREG("smps13", SMPS, 13, &pmic4_ftsmps426, "vdd-s13"),
	RPMH_VREG("ldo1",   LDO,  1,  &pmic4_nldo,      "vdd-l1-l27"),
	RPMH_VREG("ldo2",   LDO,  2,  &pmic4_nldo,      "vdd-l2-l8-l17"),
	RPMH_VREG("ldo3",   LDO,  3,  &pmic4_nldo,      "vdd-l3-l11"),
	RPMH_VREG("ldo4",   LDO,  4,  &pmic4_nldo,      "vdd-l4-l5"),
	RPMH_VREG("ldo5",   LDO,  5,  &pmic4_nldo,      "vdd-l4-l5"),
	RPMH_VREG("ldo6",   LDO,  6,  &pmic4_pldo,      "vdd-l6"),
	RPMH_VREG("ldo7",   LDO,  7,  &pmic4_pldo_lv,   "vdd-l7-l12-l14-l15"),
	RPMH_VREG("ldo8",   LDO,  8,  &pmic4_nldo,      "vdd-l2-l8-l17"),
	RPMH_VREG("ldo9",   LDO,  9,  &pmic4_pldo,      "vdd-l9"),
	RPMH_VREG("ldo10",  LDO,  10, &pmic4_pldo,      "vdd-l10-l23-l25"),
	RPMH_VREG("ldo11",  LDO,  11, &pmic4_nldo,      "vdd-l3-l11"),
	RPMH_VREG("ldo12",  LDO,  12, &pmic4_pldo_lv,   "vdd-l7-l12-l14-l15"),
	RPMH_VREG("ldo13",  LDO,  13, &pmic4_pldo,      "vdd-l13-l19-l21"),
	RPMH_VREG("ldo14",  LDO,  14, &pmic4_pldo_lv,   "vdd-l7-l12-l14-l15"),
	RPMH_VREG("ldo15",  LDO,  15, &pmic4_pldo_lv,   "vdd-l7-l12-l14-l15"),
	RPMH_VREG("ldo16",  LDO,  16, &pmic4_pldo,      "vdd-l16-l28"),
	RPMH_VREG("ldo17",  LDO,  17, &pmic4_nldo,      "vdd-l2-l8-l17"),
	RPMH_VREG("ldo18",  LDO,  18, &pmic4_pldo,      "vdd-l18-l22"),
	RPMH_VREG("ldo19",  LDO,  19, &pmic4_pldo,      "vdd-l13-l19-l21"),
	RPMH_VREG("ldo20",  LDO,  20, &pmic4_pldo,      "vdd-l20-l24"),
	RPMH_VREG("ldo21",  LDO,  21, &pmic4_pldo,      "vdd-l13-l19-l21"),
	RPMH_VREG("ldo22",  LDO,  22, &pmic4_pldo,      "vdd-l18-l22"),
	RPMH_VREG("ldo23",  LDO,  23, &pmic4_pldo,      "vdd-l10-l23-l25"),
	RPMH_VREG("ldo24",  LDO,  24, &pmic4_pldo,      "vdd-l20-l24"),
	RPMH_VREG("ldo25",  LDO,  25, &pmic4_pldo,      "vdd-l10-l23-l25"),
	RPMH_VREG("ldo26",  LDO,  26, &pmic4_nldo,      "vdd-l26"),
	RPMH_VREG("ldo27",  LDO,  27, &pmic4_nldo,      "vdd-l1-l27"),
	RPMH_VREG("ldo28",  LDO,  28, &pmic4_pldo,      "vdd-l16-l28"),
	RPMH_VREG("lvs1",   VS,   1,  &pmic4_lvs,       "vin-lvs-1-2"),
	RPMH_VREG("lvs2",   VS,   2,  &pmic4_lvs,       "vin-lvs-1-2"),
	{}
};

static const struct rpmh_vreg_init_data pmg1110_vreg_data[] = {
	RPMH_VREG("smps1",  SMPS, 1, &pmic5_ftsmps510,  "vdd-s1"),
	{}
};

static const struct rpmh_vreg_init_data pmi8998_vreg_data[] = {
	RPMH_VREG("bob",    BOB,  1, &pmic4_bob,       "vdd-bob"),
	{}
};

static const struct rpmh_vreg_init_data pm8005_vreg_data[] = {
	RPMH_VREG("smps1",  SMPS, 1, &pmic4_ftsmps426, "vdd-s1"),
	RPMH_VREG("smps2",  SMPS, 2, &pmic4_ftsmps426, "vdd-s2"),
	RPMH_VREG("smps3",  SMPS, 3, &pmic4_ftsmps426, "vdd-s3"),
	RPMH_VREG("smps4",  SMPS, 4, &pmic4_ftsmps426, "vdd-s4"),
	{}
};

static const struct rpmh_vreg_init_data pm8150_vreg_data[] = {
	RPMH_VREG("smps1",  SMPS, 1,  &pmic5_ftsmps510, "vdd-s1"),
	RPMH_VREG("smps2",  SMPS, 2,  &pmic5_ftsmps510, "vdd-s2"),
	RPMH_VREG("smps3",  SMPS, 3,  &pmic5_ftsmps510, "vdd-s3"),
	RPMH_VREG("smps4",  SMPS, 4,  &pmic5_hfsmps510, "vdd-s4"),
	RPMH_VREG("smps5",  SMPS, 5,  &pmic5_hfsmps510, "vdd-s5"),
	RPMH_VREG("smps6",  SMPS, 6,  &pmic5_ftsmps510, "vdd-s6"),
	RPMH_VREG("smps7",  SMPS, 7,  &pmic5_ftsmps510, "vdd-s7"),
	RPMH_VREG("smps8",  SMPS, 8,  &pmic5_ftsmps510, "vdd-s8"),
	RPMH_VREG("smps9",  SMPS, 9,  &pmic5_ftsmps510, "vdd-s9"),
	RPMH_VREG("smps10", SMPS, 10, &pmic5_ftsmps510, "vdd-s10"),
	RPMH_VREG("ldo1",   LDO,  1,  &pmic5_nldo,      "vdd-l1-l8-l11"),
	RPMH_VREG("ldo2",   LDO,  2,  &pmic5_pldo,      "vdd-l2-l10"),
	RPMH_VREG("ldo3",   LDO,  3,  &pmic5_nldo,      "vdd-l3-l4-l5-l18"),
	RPMH_VREG("ldo4",   LDO,  4,  &pmic5_nldo,      "vdd-l3-l4-l5-l18"),
	RPMH_VREG("ldo5",   LDO,  5,  &pmic5_nldo,      "vdd-l3-l4-l5-l18"),
	RPMH_VREG("ldo6",   LDO,  6,  &pmic5_nldo,      "vdd-l6-l9"),
	RPMH_VREG("ldo7",   LDO,  7,  &pmic5_pldo,      "vdd-l7-l12-l14-l15"),
	RPMH_VREG("ldo8",   LDO,  8,  &pmic5_nldo,      "vdd-l1-l8-l11"),
	RPMH_VREG("ldo9",   LDO,  9,  &pmic5_nldo,      "vdd-l6-l9"),
	RPMH_VREG("ldo10",  LDO,  10, &pmic5_pldo,      "vdd-l2-l10"),
	RPMH_VREG("ldo11",  LDO,  11, &pmic5_nldo,      "vdd-l1-l8-l11"),
	RPMH_VREG("ldo12",  LDO,  12, &pmic5_pldo_lv,   "vdd-l7-l12-l14-l15"),
	RPMH_VREG("ldo13",  LDO,  13, &pmic5_pldo,      "vdd-l13-l16-l17"),
	RPMH_VREG("ldo14",  LDO,  14, &pmic5_pldo_lv,   "vdd-l7-l12-l14-l15"),
	RPMH_VREG("ldo15",  LDO,  15, &pmic5_pldo_lv,   "vdd-l7-l12-l14-l15"),
	RPMH_VREG("ldo16",  LDO,  16, &pmic5_pldo,      "vdd-l13-l16-l17"),
	RPMH_VREG("ldo17",  LDO,  17, &pmic5_pldo,      "vdd-l13-l16-l17"),
	RPMH_VREG("ldo18",  LDO,  18, &pmic5_nldo,      "vdd-l3-l4-l5-l18"),
	{}
};

static const struct rpmh_vreg_init_data pm8150l_vreg_data[] = {
	RPMH_VREG("smps1",  SMPS, 1, &pmic5_ftsmps510, "vdd-s1"),
	RPMH_VREG("smps2",  SMPS, 2, &pmic5_ftsmps510, "vdd-s2"),
	RPMH_VREG("smps3",  SMPS, 3, &pmic5_ftsmps510, "vdd-s3"),
	RPMH_VREG("smps4",  SMPS, 4, &pmic5_ftsmps510, "vdd-s4"),
	RPMH_VREG("smps5",  SMPS, 5, &pmic5_ftsmps510, "vdd-s5"),
	RPMH_VREG("smps6",  SMPS, 6, &pmic5_ftsmps510, "vdd-s6"),
	RPMH_VREG("smps7",  SMPS, 7, &pmic5_ftsmps510, "vdd-s7"),
	RPMH_VREG("smps8",  SMPS, 8, &pmic5_hfsmps510, "vdd-s8"),
	RPMH_VREG("ldo1",   LDO,  1, &pmic5_pldo_lv,   "vdd-l1-l8"),
	RPMH_VREG("ldo2",   LDO,  2, &pmic5_nldo,      "vdd-l2-l3"),
	RPMH_VREG("ldo3",   LDO,  3, &pmic5_nldo,      "vdd-l2-l3"),
	RPMH_VREG("ldo4",   LDO,  4, &pmic5_pldo,      "vdd-l4-l5-l6"),
	RPMH_VREG("ldo5",   LDO,  5, &pmic5_pldo,      "vdd-l4-l5-l6"),
	RPMH_VREG("ldo6",   LDO,  6, &pmic5_pldo,      "vdd-l4-l5-l6"),
	RPMH_VREG("ldo7",   LDO,  7, &pmic5_pldo,      "vdd-l7-l11"),
	RPMH_VREG("ldo8",   LDO,  8, &pmic5_pldo_lv,   "vdd-l1-l8"),
	RPMH_VREG("ldo9",   LDO,  9, &pmic5_pldo,      "vdd-l9-l10"),
	RPMH_VREG("ldo10",  LDO,  10, &pmic5_pldo,      "vdd-l9-l10"),
	RPMH_VREG("ldo11",  LDO,  11, &pmic5_pldo,      "vdd-l7-l11"),
	RPMH_VREG("bob",    BOB,  1, &pmic5_bob,       "vdd-bob"),
	{}
};

static const struct rpmh_vreg_init_data pmm8155au_vreg_data[] = {
	RPMH_VREG("smps1",  SMPS, 1,  &pmic5_ftsmps510, "vdd-s1"),
	RPMH_VREG("smps2",  SMPS, 2,  &pmic5_ftsmps510, "vdd-s2"),
	RPMH_VREG("smps3",  SMPS, 3,  &pmic5_ftsmps510, "vdd-s3"),
	RPMH_VREG("smps4",  SMPS, 4,  &pmic5_hfsmps510, "vdd-s4"),
	RPMH_VREG("smps5",  SMPS, 5,  &pmic5_hfsmps510, "vdd-s5"),
	RPMH_VREG("smps6",  SMPS, 6,  &pmic5_ftsmps510, "vdd-s6"),
	RPMH_VREG("smps7",  SMPS, 7,  &pmic5_ftsmps510, "vdd-s7"),
	RPMH_VREG("smps8",  SMPS, 8,  &pmic5_ftsmps510, "vdd-s8"),
	RPMH_VREG("smps9",  SMPS, 9,  &pmic5_ftsmps510, "vdd-s9"),
	RPMH_VREG("smps10", SMPS, 10, &pmic5_ftsmps510, "vdd-s10"),
	RPMH_VREG("ldo1",   LDO,  1,  &pmic5_nldo,      "vdd-l1-l8-l11"),
	RPMH_VREG("ldo2",   LDO,  2,  &pmic5_pldo,      "vdd-l2-l10"),
	RPMH_VREG("ldo3",   LDO,  3,  &pmic5_nldo,      "vdd-l3-l4-l5-l18"),
	RPMH_VREG("ldo4",   LDO,  4,  &pmic5_nldo,      "vdd-l3-l4-l5-l18"),
	RPMH_VREG("ldo5",   LDO,  5,  &pmic5_nldo,      "vdd-l3-l4-l5-l18"),
	RPMH_VREG("ldo6",   LDO,  6,  &pmic5_nldo,      "vdd-l6-l9"),
	RPMH_VREG("ldo7",   LDO,  7,  &pmic5_pldo_lv,   "vdd-l7-l12-l14-l15"),
	RPMH_VREG("ldo8",   LDO,  8,  &pmic5_nldo,      "vdd-l1-l8-l11"),
	RPMH_VREG("ldo9",   LDO,  9,  &pmic5_nldo,      "vdd-l6-l9"),
	RPMH_VREG("ldo10",  LDO,  10, &pmic5_pldo,      "vdd-l2-l10"),
	RPMH_VREG("ldo11",  LDO,  11, &pmic5_nldo,      "vdd-l1-l8-l11"),
	RPMH_VREG("ldo12",  LDO,  12, &pmic5_pldo_lv,   "vdd-l7-l12-l14-l15"),
	RPMH_VREG("ldo13",  LDO,  13, &pmic5_pldo,      "vdd-l13-l16-l17"),
	RPMH_VREG("ldo14",  LDO,  14, &pmic5_pldo_lv,   "vdd-l7-l12-l14-l15"),
	RPMH_VREG("ldo15",  LDO,  15, &pmic5_pldo_lv,   "vdd-l7-l12-l14-l15"),
	RPMH_VREG("ldo16",  LDO,  16, &pmic5_pldo,      "vdd-l13-l16-l17"),
	RPMH_VREG("ldo17",  LDO,  17, &pmic5_pldo,      "vdd-l13-l16-l17"),
	RPMH_VREG("ldo18",  LDO,  18, &pmic5_nldo,      "vdd-l3-l4-l5-l18"),
	{}
};

static const struct rpmh_vreg_init_data pmm8654au_vreg_data[] = {
	RPMH_VREG("smps1",  SMPS, 1,  &pmic5_ftsmps527,  "vdd-s1"),
	RPMH_VREG("smps2",  SMPS, 2,  &pmic5_ftsmps527,  "vdd-s2"),
	RPMH_VREG("smps3",  SMPS, 3,  &pmic5_ftsmps527,  "vdd-s3"),
	RPMH_VREG("smps4",  SMPS, 4,  &pmic5_ftsmps527,  "vdd-s4"),
	RPMH_VREG("smps5",  SMPS, 5,  &pmic5_ftsmps527,  "vdd-s5"),
	RPMH_VREG("smps6",  SMPS, 6,  &pmic5_ftsmps527,  "vdd-s6"),
	RPMH_VREG("smps7",  SMPS, 7,  &pmic5_ftsmps527,  "vdd-s7"),
	RPMH_VREG("smps8",  SMPS, 8,  &pmic5_ftsmps527,  "vdd-s8"),
	RPMH_VREG("smps9",  SMPS, 9,  &pmic5_ftsmps527,  "vdd-s9"),
	RPMH_VREG("ldo1",   LDO,  1,  &pmic5_nldo515,    "vdd-s9"),
	RPMH_VREG("ldo2",   LDO,  2,  &pmic5_nldo515,    "vdd-l2-l3"),
	RPMH_VREG("ldo3",   LDO,  3,  &pmic5_nldo515,    "vdd-l2-l3"),
	RPMH_VREG("ldo4",   LDO,  4,  &pmic5_nldo515,    "vdd-s9"),
	RPMH_VREG("ldo5",   LDO,  5,  &pmic5_nldo515,    "vdd-s9"),
	RPMH_VREG("ldo6",   LDO,  6,  &pmic5_nldo515,    "vdd-l6-l7"),
	RPMH_VREG("ldo7",   LDO,  7,  &pmic5_nldo515,    "vdd-l6-l7"),
	RPMH_VREG("ldo8",   LDO,  8,  &pmic5_pldo515_mv, "vdd-l8-l9"),
	RPMH_VREG("ldo9",   LDO,  9,  &pmic5_pldo,       "vdd-l8-l9"),
	{}
};

static const struct rpmh_vreg_init_data pm8350_vreg_data[] = {
	RPMH_VREG("smps1",  SMPS, 1,  &pmic5_ftsmps510, "vdd-s1"),
	RPMH_VREG("smps2",  SMPS, 2,  &pmic5_ftsmps510, "vdd-s2"),
	RPMH_VREG("smps3",  SMPS, 3,  &pmic5_ftsmps510, "vdd-s3"),
	RPMH_VREG("smps4",  SMPS, 4,  &pmic5_ftsmps510, "vdd-s4"),
	RPMH_VREG("smps5",  SMPS, 5,  &pmic5_ftsmps510, "vdd-s5"),
	RPMH_VREG("smps6",  SMPS, 6,  &pmic5_ftsmps510, "vdd-s6"),
	RPMH_VREG("smps7",  SMPS, 7,  &pmic5_ftsmps510, "vdd-s7"),
	RPMH_VREG("smps8",  SMPS, 8,  &pmic5_ftsmps510, "vdd-s8"),
	RPMH_VREG("smps9",  SMPS, 9,  &pmic5_ftsmps510, "vdd-s9"),
	RPMH_VREG("smps10", SMPS, 10, &pmic5_hfsmps510, "vdd-s10"),
	RPMH_VREG("smps11", SMPS, 11, &pmic5_hfsmps510, "vdd-s11"),
	RPMH_VREG("smps12", SMPS, 12, &pmic5_hfsmps510, "vdd-s12"),
	RPMH_VREG("ldo1",   LDO,  1,  &pmic5_nldo,      "vdd-l1-l4"),
	RPMH_VREG("ldo2",   LDO,  2,  &pmic5_pldo,      "vdd-l2-l7"),
	RPMH_VREG("ldo3",   LDO,  3,  &pmic5_nldo,      "vdd-l3-l5"),
	RPMH_VREG("ldo4",   LDO,  4,  &pmic5_nldo,      "vdd-l1-l4"),
	RPMH_VREG("ldo5",   LDO,  5,  &pmic5_nldo,      "vdd-l3-l5"),
	RPMH_VREG("ldo6",   LDO,  6,  &pmic5_nldo,      "vdd-l6-l9-l10"),
	RPMH_VREG("ldo7",   LDO,  7,  &pmic5_pldo,      "vdd-l2-l7"),
	RPMH_VREG("ldo8",   LDO,  8,  &pmic5_nldo,      "vdd-l8"),
	RPMH_VREG("ldo9",   LDO,  9,  &pmic5_nldo,      "vdd-l6-l9-l10"),
	RPMH_VREG("ldo10",  LDO,  10, &pmic5_nldo,      "vdd-l6-l9-l10"),
	{}
};

static const struct rpmh_vreg_init_data pm8350c_vreg_data[] = {
	RPMH_VREG("smps1",  SMPS, 1,  &pmic5_hfsmps515, "vdd-s1"),
	RPMH_VREG("smps2",  SMPS, 2,  &pmic5_ftsmps510, "vdd-s2"),
	RPMH_VREG("smps3",  SMPS, 3,  &pmic5_ftsmps510, "vdd-s3"),
	RPMH_VREG("smps4",  SMPS, 4,  &pmic5_ftsmps510, "vdd-s4"),
	RPMH_VREG("smps5",  SMPS, 5,  &pmic5_ftsmps510, "vdd-s5"),
	RPMH_VREG("smps6",  SMPS, 6,  &pmic5_ftsmps510, "vdd-s6"),
	RPMH_VREG("smps7",  SMPS, 7,  &pmic5_ftsmps510, "vdd-s7"),
	RPMH_VREG("smps8",  SMPS, 8,  &pmic5_ftsmps510, "vdd-s8"),
	RPMH_VREG("smps9",  SMPS, 9,  &pmic5_ftsmps510, "vdd-s9"),
	RPMH_VREG("smps10", SMPS, 10, &pmic5_ftsmps510, "vdd-s10"),
	RPMH_VREG("ldo1",   LDO,  1,  &pmic5_pldo_lv,   "vdd-l1-l12"),
	RPMH_VREG("ldo2",   LDO,  2,  &pmic5_pldo_lv,   "vdd-l2-l8"),
	RPMH_VREG("ldo3",   LDO,  3,  &pmic5_pldo,      "vdd-l3-l4-l5-l7-l13"),
	RPMH_VREG("ldo4",   LDO,  4,  &pmic5_pldo,      "vdd-l3-l4-l5-l7-l13"),
	RPMH_VREG("ldo5",   LDO,  5,  &pmic5_pldo,      "vdd-l3-l4-l5-l7-l13"),
	RPMH_VREG("ldo6",   LDO,  6,  &pmic5_pldo,      "vdd-l6-l9-l11"),
	RPMH_VREG("ldo7",   LDO,  7,  &pmic5_pldo,      "vdd-l3-l4-l5-l7-l13"),
	RPMH_VREG("ldo8",   LDO,  8,  &pmic5_pldo_lv,   "vdd-l2-l8"),
	RPMH_VREG("ldo9",   LDO,  9,  &pmic5_pldo,      "vdd-l6-l9-l11"),
	RPMH_VREG("ldo10",  LDO,  10, &pmic5_nldo,      "vdd-l10"),
	RPMH_VREG("ldo11",  LDO,  11, &pmic5_pldo,      "vdd-l6-l9-l11"),
	RPMH_VREG("ldo12",  LDO,  12, &pmic5_pldo_lv,   "vdd-l1-l12"),
	RPMH_VREG("ldo13",  LDO,  13, &pmic5_pldo,      "vdd-l3-l4-l5-l7-l13"),
	RPMH_VREG("bob",    BOB,  1,  &pmic5_bob,       "vdd-bob"),
	{}
};

static const struct rpmh_vreg_init_data pm8450_vreg_data[] = {
	RPMH_VREG("smps1",  SMPS, 1, &pmic5_ftsmps520, "vdd-s1"),
	RPMH_VREG("smps2",  SMPS, 2, &pmic5_ftsmps520, "vdd-s2"),
	RPMH_VREG("smps3",  SMPS, 3, &pmic5_ftsmps520, "vdd-s3"),
	RPMH_VREG("smps4",  SMPS, 4, &pmic5_ftsmps520, "vdd-s4"),
	RPMH_VREG("smps5",  SMPS, 5, &pmic5_ftsmps520, "vdd-s5"),
	RPMH_VREG("smps6",  SMPS, 6, &pmic5_ftsmps520, "vdd-s6"),
	RPMH_VREG("ldo1",   LDO,  1, &pmic5_nldo,      "vdd-l1"),
	RPMH_VREG("ldo2",   LDO,  2, &pmic5_nldo,      "vdd-l2"),
	RPMH_VREG("ldo3",   LDO,  3, &pmic5_nldo,      "vdd-l3"),
	RPMH_VREG("ldo4",   LDO,  4, &pmic5_pldo_lv,   "vdd-l4"),
	{}
};

static const struct rpmh_vreg_init_data pm8550_vreg_data[] = {
	RPMH_VREG("ldo1",   LDO,  1,  &pmic5_nldo515,    "vdd-l1-l4-l10"),
	RPMH_VREG("ldo2",   LDO,  2,  &pmic5_pldo,       "vdd-l2-l13-l14"),
	RPMH_VREG("ldo3",   LDO,  3,  &pmic5_nldo515,    "vdd-l3"),
	RPMH_VREG("ldo4",   LDO,  4,  &pmic5_nldo515,    "vdd-l1-l4-l10"),
	RPMH_VREG("ldo5",   LDO,  5,  &pmic5_pldo,       "vdd-l5-l16"),
	RPMH_VREG("ldo6",   LDO,  6,  &pmic5_pldo,       "vdd-l6-l7"),
	RPMH_VREG("ldo7",   LDO,  7,  &pmic5_pldo,       "vdd-l6-l7"),
	RPMH_VREG("ldo8",   LDO,  8,  &pmic5_pldo,       "vdd-l8-l9"),
	RPMH_VREG("ldo9",   LDO,  9,  &pmic5_pldo,       "vdd-l8-l9"),
	RPMH_VREG("ldo10",  LDO,  10, &pmic5_nldo515,    "vdd-l1-l4-l10"),
	RPMH_VREG("ldo11",  LDO,  11, &pmic5_nldo515,    "vdd-l11"),
	RPMH_VREG("ldo12",  LDO,  12, &pmic5_nldo515,    "vdd-l12"),
	RPMH_VREG("ldo13",  LDO,  13, &pmic5_pldo,       "vdd-l2-l13-l14"),
	RPMH_VREG("ldo14",  LDO,  14, &pmic5_pldo,       "vdd-l2-l13-l14"),
	RPMH_VREG("ldo15",  LDO,  15, &pmic5_nldo515,    "vdd-l15"),
	RPMH_VREG("ldo16",  LDO,  16, &pmic5_pldo,       "vdd-l5-l16"),
	RPMH_VREG("ldo17",  LDO,  17, &pmic5_pldo,       "vdd-l17"),
	RPMH_VREG("bob1",   BOB,  1,  &pmic5_bob,        "vdd-bob1"),
	RPMH_VREG("bob2",   BOB,  2,  &pmic5_bob,        "vdd-bob2"),
	{}
};

static const struct rpmh_vreg_init_data pm8550vs_vreg_data[] = {
	RPMH_VREG("smps1",  SMPS, 1, &pmic5_ftsmps525, "vdd-s1"),
	RPMH_VREG("smps2",  SMPS, 2, &pmic5_ftsmps525, "vdd-s2"),
	RPMH_VREG("smps3",  SMPS, 3, &pmic5_ftsmps525, "vdd-s3"),
	RPMH_VREG("smps4",  SMPS, 4, &pmic5_ftsmps525, "vdd-s4"),
	RPMH_VREG("smps5",  SMPS, 5, &pmic5_ftsmps525, "vdd-s5"),
	RPMH_VREG("smps6",  SMPS, 6, &pmic5_ftsmps525, "vdd-s6"),
	RPMH_VREG("ldo1",   LDO,  1, &pmic5_nldo515,   "vdd-l1"),
	RPMH_VREG("ldo2",   LDO,  2, &pmic5_nldo515,   "vdd-l2"),
	RPMH_VREG("ldo3",   LDO,  3, &pmic5_nldo515,   "vdd-l3"),
	{}
};

static const struct rpmh_vreg_init_data pm8550ve_vreg_data[] = {
	RPMH_VREG("smps1", SMPS, 1, &pmic5_ftsmps525, "vdd-s1"),
	RPMH_VREG("smps2", SMPS, 2, &pmic5_ftsmps525, "vdd-s2"),
	RPMH_VREG("smps3", SMPS, 3, &pmic5_ftsmps525, "vdd-s3"),
	RPMH_VREG("smps4", SMPS, 4, &pmic5_ftsmps525, "vdd-s4"),
	RPMH_VREG("smps5", SMPS, 5, &pmic5_ftsmps525, "vdd-s5"),
	RPMH_VREG("smps6", SMPS, 6, &pmic5_ftsmps525, "vdd-s6"),
	RPMH_VREG("smps7", SMPS, 7, &pmic5_ftsmps525, "vdd-s7"),
	RPMH_VREG("smps8", SMPS, 8, &pmic5_ftsmps525, "vdd-s8"),
	RPMH_VREG("ldo1",  LDO,  1, &pmic5_nldo515,   "vdd-l1"),
	RPMH_VREG("ldo2",  LDO,  2, &pmic5_nldo515,   "vdd-l2"),
	RPMH_VREG("ldo3",  LDO,  3, &pmic5_nldo515,   "vdd-l3"),
	{}
};

static const struct rpmh_vreg_init_data pmc8380_vreg_data[] = {
	RPMH_VREG("smps1", SMPS, 1, &pmic5_ftsmps525, "vdd-s1"),
	RPMH_VREG("smps2", SMPS, 2, &pmic5_ftsmps525, "vdd-s2"),
	RPMH_VREG("smps3", SMPS, 3, &pmic5_ftsmps525, "vdd-s3"),
	RPMH_VREG("smps4", SMPS, 4, &pmic5_ftsmps525, "vdd-s4"),
	RPMH_VREG("smps5", SMPS, 5, &pmic5_ftsmps525, "vdd-s5"),
	RPMH_VREG("smps6", SMPS, 6, &pmic5_ftsmps525, "vdd-s6"),
	RPMH_VREG("smps7", SMPS, 7, &pmic5_ftsmps525, "vdd-s7"),
	RPMH_VREG("smps8", SMPS, 8, &pmic5_ftsmps525, "vdd-s8"),
	RPMH_VREG("ldo1",  LDO,  1, &pmic5_nldo515,   "vdd-l1"),
	RPMH_VREG("ldo2",  LDO,  2, &pmic5_nldo515,   "vdd-l2"),
	RPMH_VREG("ldo3",  LDO,  3, &pmic5_nldo515,   "vdd-l3"),
	{}
};

static const struct rpmh_vreg_init_data pm8009_vreg_data[] = {
	RPMH_VREG("smps1", SMPS, 1, &pmic5_hfsmps510, "vdd-s1"),
	RPMH_VREG("smps2", SMPS, 2, &pmic5_hfsmps515, "vdd-s2"),
	RPMH_VREG("ldo1",  LDO,  1, &pmic5_nldo,      "vdd-l1"),
	RPMH_VREG("ldo2",  LDO,  2, &pmic5_nldo,      "vdd-l2"),
	RPMH_VREG("ldo3",  LDO,  3, &pmic5_nldo,      "vdd-l3"),
	RPMH_VREG("ldo4",  LDO,  4, &pmic5_nldo,      "vdd-l4"),
	RPMH_VREG("ldo5",  LDO,  5, &pmic5_pldo,      "vdd-l5-l6"),
	RPMH_VREG("ldo6",  LDO,  6, &pmic5_pldo,      "vdd-l5-l6"),
	RPMH_VREG("ldo7",  LDO,  7, &pmic5_pldo_lv,   "vdd-l7"),
	{}
};

static const struct rpmh_vreg_init_data pm8009_1_vreg_data[] = {
	RPMH_VREG("smps1", SMPS, 1, &pmic5_hfsmps510, "vdd-s1"),
	RPMH_VREG("smps2", SMPS, 2, &pmic5_hfsmps515_1, "vdd-s2"),
	RPMH_VREG("ldo1",  LDO,  1, &pmic5_nldo,      "vdd-l1"),
	RPMH_VREG("ldo2",  LDO,  2, &pmic5_nldo,      "vdd-l2"),
	RPMH_VREG("ldo3",  LDO,  3, &pmic5_nldo,      "vdd-l3"),
	RPMH_VREG("ldo4",  LDO,  4, &pmic5_nldo,      "vdd-l4"),
	RPMH_VREG("ldo5",  LDO,  5, &pmic5_pldo,      "vdd-l5-l6"),
	RPMH_VREG("ldo6",  LDO,  6, &pmic5_pldo,      "vdd-l5-l6"),
	RPMH_VREG("ldo7",  LDO,  7, &pmic5_pldo_lv,   "vdd-l7"),
	{}
};

static const struct rpmh_vreg_init_data pm8010_vreg_data[] = {
	RPMH_VREG("ldo1",  LDO,  1, &pmic5_nldo502,   "vdd-l1-l2"),
	RPMH_VREG("ldo2",  LDO,  2, &pmic5_nldo502,   "vdd-l1-l2"),
	RPMH_VREG("ldo3",  LDO,  3, &pmic5_pldo502ln, "vdd-l3-l4"),
	RPMH_VREG("ldo4",  LDO,  4, &pmic5_pldo502ln, "vdd-l3-l4"),
	RPMH_VREG("ldo5",  LDO,  5, &pmic5_pldo502,   "vdd-l5"),
	RPMH_VREG("ldo6",  LDO,  6, &pmic5_pldo502ln, "vdd-l6"),
	RPMH_VREG("ldo7",  LDO,  7, &pmic5_pldo502,   "vdd-l7"),
};

static const struct rpmh_vreg_init_data pm6150_vreg_data[] = {
	RPMH_VREG("smps1", SMPS, 1, &pmic5_ftsmps510, "vdd-s1"),
	RPMH_VREG("smps2", SMPS, 2, &pmic5_ftsmps510, "vdd-s2"),
	RPMH_VREG("smps3", SMPS, 3, &pmic5_ftsmps510, "vdd-s3"),
	RPMH_VREG("smps4", SMPS, 4, &pmic5_hfsmps510, "vdd-s4"),
	RPMH_VREG("smps5", SMPS, 5, &pmic5_hfsmps510, "vdd-s5"),
	RPMH_VREG("ldo1",  LDO,  1, &pmic5_nldo,      "vdd-l1"),
	RPMH_VREG("ldo2",  LDO,  2, &pmic5_nldo,      "vdd-l2-l3"),
	RPMH_VREG("ldo3",  LDO,  3, &pmic5_nldo,      "vdd-l2-l3"),
	RPMH_VREG("ldo4",  LDO,  4, &pmic5_nldo,      "vdd-l4-l7-l8"),
	RPMH_VREG("ldo5",  LDO,  5, &pmic5_pldo,      "vdd-l5-l16-l17-l18-l19"),
	RPMH_VREG("ldo6",  LDO,  6, &pmic5_nldo,      "vdd-l6"),
	RPMH_VREG("ldo7",  LDO,  7, &pmic5_nldo,      "vdd-l4-l7-l8"),
	RPMH_VREG("ldo8",  LDO,  8, &pmic5_nldo,      "vdd-l4-l7-l8"),
	RPMH_VREG("ldo9",  LDO,  9, &pmic5_nldo,      "vdd-l9"),
	RPMH_VREG("ldo10", LDO,  10, &pmic5_pldo_lv,   "vdd-l10-l14-l15"),
	RPMH_VREG("ldo11", LDO,  11, &pmic5_pldo_lv,   "vdd-l11-l12-l13"),
	RPMH_VREG("ldo12", LDO,  12, &pmic5_pldo_lv,   "vdd-l11-l12-l13"),
	RPMH_VREG("ldo13", LDO,  13, &pmic5_pldo_lv,   "vdd-l11-l12-l13"),
	RPMH_VREG("ldo14", LDO,  14, &pmic5_pldo_lv,   "vdd-l10-l14-l15"),
	RPMH_VREG("ldo15", LDO,  15, &pmic5_pldo_lv,   "vdd-l10-l14-l15"),
	RPMH_VREG("ldo16", LDO,  16, &pmic5_pldo,      "vdd-l5-l16-l17-l18-l19"),
	RPMH_VREG("ldo17", LDO,  17, &pmic5_pldo,      "vdd-l5-l16-l17-l18-l19"),
	RPMH_VREG("ldo18", LDO,  18, &pmic5_pldo,      "vdd-l5-l16-l17-l18-l19"),
	RPMH_VREG("ldo19", LDO,  19, &pmic5_pldo,      "vdd-l5-l16-l17-l18-l19"),
	{}
};

static const struct rpmh_vreg_init_data pm6150l_vreg_data[] = {
	RPMH_VREG("smps1",  SMPS, 1, &pmic5_ftsmps510, "vdd-s1"),
	RPMH_VREG("smps2",  SMPS, 2, &pmic5_ftsmps510, "vdd-s2"),
	RPMH_VREG("smps3",  SMPS, 3, &pmic5_ftsmps510, "vdd-s3"),
	RPMH_VREG("smps4",  SMPS, 4, &pmic5_ftsmps510, "vdd-s4"),
	RPMH_VREG("smps5",  SMPS, 5, &pmic5_ftsmps510, "vdd-s5"),
	RPMH_VREG("smps6",  SMPS, 6, &pmic5_ftsmps510, "vdd-s6"),
	RPMH_VREG("smps7",  SMPS, 7, &pmic5_ftsmps510, "vdd-s7"),
	RPMH_VREG("smps8",  SMPS, 8, &pmic5_hfsmps510, "vdd-s8"),
	RPMH_VREG("ldo1",   LDO,  1, &pmic5_pldo_lv,   "vdd-l1-l8"),
	RPMH_VREG("ldo2",   LDO,  2, &pmic5_nldo,      "vdd-l2-l3"),
	RPMH_VREG("ldo3",   LDO,  3, &pmic5_nldo,      "vdd-l2-l3"),
	RPMH_VREG("ldo4",   LDO,  4, &pmic5_pldo,      "vdd-l4-l5-l6"),
	RPMH_VREG("ldo5",   LDO,  5, &pmic5_pldo,      "vdd-l4-l5-l6"),
	RPMH_VREG("ldo6",   LDO,  6, &pmic5_pldo,      "vdd-l4-l5-l6"),
	RPMH_VREG("ldo7",   LDO,  7, &pmic5_pldo,      "vdd-l7-l11"),
	RPMH_VREG("ldo8",   LDO,  8, &pmic5_pldo,      "vdd-l1-l8"),
	RPMH_VREG("ldo9",   LDO,  9, &pmic5_pldo,      "vdd-l9-l10"),
	RPMH_VREG("ldo10",  LDO,  10, &pmic5_pldo,      "vdd-l9-l10"),
	RPMH_VREG("ldo11",  LDO,  11, &pmic5_pldo,      "vdd-l7-l11"),
	RPMH_VREG("bob",    BOB,  1, &pmic5_bob,       "vdd-bob"),
	{}
};

static const struct rpmh_vreg_init_data pm6350_vreg_data[] = {
	RPMH_VREG("smps1",  SMPS, 1,  &pmic5_ftsmps510, NULL),
	RPMH_VREG("smps2",  SMPS, 2,  &pmic5_hfsmps510, NULL),
	/* smps3 - smps5 not configured */
	RPMH_VREG("ldo1",   LDO,  1,  &pmic5_nldo,      NULL),
	RPMH_VREG("ldo2",   LDO,  2,  &pmic5_pldo,      NULL),
	RPMH_VREG("ldo3",   LDO,  3,  &pmic5_pldo,      NULL),
	RPMH_VREG("ldo4",   LDO,  4,  &pmic5_nldo,      NULL),
	RPMH_VREG("ldo5",   LDO,  5,  &pmic5_pldo,      NULL),
	RPMH_VREG("ldo6",   LDO,  6,  &pmic5_pldo,      NULL),
	RPMH_VREG("ldo7",   LDO,  7,  &pmic5_pldo,      NULL),
	RPMH_VREG("ldo8",   LDO,  8,  &pmic5_pldo,      NULL),
	RPMH_VREG("ldo9",   LDO,  9,  &pmic5_pldo,      NULL),
	RPMH_VREG("ldo10",  LDO,  10, &pmic5_pldo,      NULL),
	RPMH_VREG("ldo11",  LDO,  11, &pmic5_pldo,      NULL),
	RPMH_VREG("ldo12",  LDO,  12, &pmic5_pldo,      NULL),
	RPMH_VREG("ldo13",  LDO,  13, &pmic5_nldo,      NULL),
	RPMH_VREG("ldo14",  LDO,  14, &pmic5_pldo,      NULL),
	RPMH_VREG("ldo15",  LDO,  15, &pmic5_nldo,      NULL),
	RPMH_VREG("ldo16",  LDO,  16, &pmic5_nldo,      NULL),
	/* ldo17 not configured */
	RPMH_VREG("ldo18",  LDO,  18, &pmic5_nldo,      NULL),
	RPMH_VREG("ldo19",  LDO,  19, &pmic5_nldo,      NULL),
	RPMH_VREG("ldo20",  LDO,  20, &pmic5_nldo,      NULL),
	RPMH_VREG("ldo21",  LDO,  21, &pmic5_nldo,      NULL),
	RPMH_VREG("ldo22",  LDO,  22, &pmic5_nldo,      NULL),
};

static const struct rpmh_vreg_init_data pmcx0102_vreg_data[] = {
	RPMH_VREG("smps1",   SMPS, 1,    &pmic5_ftsmps530, "vdd-s1"),
	RPMH_VREG("smps2",   SMPS, 2,    &pmic5_ftsmps530, "vdd-s2"),
	RPMH_VREG("smps3",   SMPS, 3,    &pmic5_ftsmps530, "vdd-s3"),
	RPMH_VREG("smps4",   SMPS, 4,    &pmic5_ftsmps530, "vdd-s4"),
	RPMH_VREG("smps5",   SMPS, 5,    &pmic5_ftsmps530, "vdd-s5"),
	RPMH_VREG("smps6",   SMPS, 6,    &pmic5_ftsmps530, "vdd-s6"),
	RPMH_VREG("smps7",   SMPS, 7,    &pmic5_ftsmps530, "vdd-s7"),
	RPMH_VREG("smps8",   SMPS, 8,    &pmic5_ftsmps530, "vdd-s8"),
	RPMH_VREG("smps9",   SMPS, 9,    &pmic5_ftsmps530, "vdd-s9"),
	RPMH_VREG("smps10",  SMPS, 10,   &pmic5_ftsmps530, "vdd-s10"),
	RPMH_VREG("ldo1",   LDO,  1,    &pmic5_nldo530,      "vdd-l1"),
	RPMH_VREG("ldo2",   LDO,  2,    &pmic5_nldo530,      "vdd-l2"),
	RPMH_VREG("ldo3",   LDO,  3,    &pmic5_nldo530,      "vdd-l3"),
	RPMH_VREG("ldo4",   LDO,  4,    &pmic5_nldo530,      "vdd-l4"),
	{}
};

static const struct rpmh_vreg_init_data pmh0101_vreg_data[] = {
	RPMH_VREG("ldo1",   LDO, 1,  &pmic5_nldo530,      "vdd-l1-l4-l10"),
	RPMH_VREG("ldo2",   LDO, 2,  &pmic5_pldo530_mvp300,      "vdd-l2-l13-l14"),
	RPMH_VREG("ldo3",   LDO, 3,  &pmic5_nldo530,      "vdd-l3-l11"),
	RPMH_VREG("ldo4",   LDO, 4,  &pmic5_nldo530,      "vdd-l1-l4-l10"),
	RPMH_VREG("ldo5",   LDO, 5,  &pmic5_pldo530_mvp150,      "vdd-l5-l16"),
	RPMH_VREG("ldo6",   LDO, 6,  &pmic5_pldo530_mvp300,      "vdd-l6-l7"),
	RPMH_VREG("ldo7",   LDO, 7,  &pmic5_pldo530_mvp300,      "vdd-l6-l7"),
	RPMH_VREG("ldo8",   LDO, 8,  &pmic5_pldo530_mvp150,      "vdd-l8-l9"),
	RPMH_VREG("ldo9",   LDO, 9,  &pmic5_pldo515_mv,      "vdd-l8-l9"),
	RPMH_VREG("ldo10",  LDO, 10, &pmic5_nldo530,      "vdd-l1-l4-l10"),
	RPMH_VREG("ldo11",  LDO, 11, &pmic5_nldo530,      "vdd-l3-l11"),
	RPMH_VREG("ldo12",  LDO, 12, &pmic5_nldo530,      "vdd-l12"),
	RPMH_VREG("ldo13",  LDO, 13, &pmic5_pldo530_mvp150,     "vdd-l2-l13-l14"),
	RPMH_VREG("ldo14",  LDO, 14, &pmic5_pldo530_mvp150,     "vdd-l2-l13-l14"),
	RPMH_VREG("ldo15",  LDO, 15, &pmic5_nldo530,      "vdd-l15"),
	RPMH_VREG("ldo16",  LDO, 15, &pmic5_pldo530_mvp600,      "vdd-l5-l16"),
	RPMH_VREG("ldo17",  LDO, 17, &pmic5_pldo515_mv,   "vdd-l17"),
	RPMH_VREG("ldo18",  LDO, 18, &pmic5_nldo530,      "vdd-l18"),
	RPMH_VREG("bob1",   BOB, 1,  &pmic5_bob,          "vdd-bob1"),
	RPMH_VREG("bob2",   BOB, 2,  &pmic5_bob,          "vdd-bob2"),
	{}
};

static const struct rpmh_vreg_init_data pmh0104_vreg_data[] = {
	RPMH_VREG("smps1",   SMPS, 1,    &pmic5_ftsmps530, "vdd-s1"),
	RPMH_VREG("smps2",   SMPS, 2,    &pmic5_ftsmps530, "vdd-s2"),
	RPMH_VREG("smps3",   SMPS, 3,    &pmic5_ftsmps530, "vdd-s3"),
	RPMH_VREG("smps4",   SMPS, 4,    &pmic5_ftsmps530, "vdd-s4"),
	{}
};

static const struct rpmh_vreg_init_data pmh0110_vreg_data[] = {
	RPMH_VREG("smps1",   SMPS, 1,    &pmic5_ftsmps530, "vdd-s1"),
	RPMH_VREG("smps2",   SMPS, 2,    &pmic5_ftsmps530, "vdd-s2"),
	RPMH_VREG("smps3",   SMPS, 3,    &pmic5_ftsmps530, "vdd-s3"),
	RPMH_VREG("smps4",   SMPS, 4,    &pmic5_ftsmps530, "vdd-s4"),
	RPMH_VREG("smps5",   SMPS, 5,    &pmic5_ftsmps530, "vdd-s5"),
	RPMH_VREG("smps6",   SMPS, 6,    &pmic5_ftsmps530, "vdd-s6"),
	RPMH_VREG("smps7",   SMPS, 7,    &pmic5_ftsmps530, "vdd-s7"),
	RPMH_VREG("smps8",   SMPS, 8,    &pmic5_ftsmps530, "vdd-s8"),
	RPMH_VREG("smps9",   SMPS, 9,    &pmic5_ftsmps530, "vdd-s9"),
	RPMH_VREG("smps10",  SMPS, 10,   &pmic5_ftsmps530, "vdd-s10"),
	RPMH_VREG("ldo1",   LDO,  1,    &pmic5_nldo530,      "vdd-l1"),
	RPMH_VREG("ldo2",   LDO,  2,    &pmic5_nldo530,      "vdd-l2"),
	RPMH_VREG("ldo3",   LDO,  3,    &pmic5_nldo530,      "vdd-l3"),
	RPMH_VREG("ldo4",   LDO,  4,    &pmic5_nldo530,      "vdd-l4"),
	{}
	{}
};

static const struct rpmh_vreg_init_data pm660_vreg_data[] = {
	RPMH_VREG("smps1", SMPS, 1,  &pmic4_ftsmps426, "vdd-s1"),
	RPMH_VREG("smps2", SMPS, 2,  &pmic4_ftsmps426, "vdd-s2"),
	RPMH_VREG("smps3", SMPS, 3,  &pmic4_ftsmps426, "vdd-s3"),
	RPMH_VREG("smps4", SMPS, 4,  &pmic4_hfsmps3,   "vdd-s4"),
	RPMH_VREG("smps5", SMPS, 5,  &pmic4_hfsmps3,   "vdd-s5"),
	RPMH_VREG("smps6", SMPS, 6,  &pmic4_hfsmps3,   "vdd-s6"),
	RPMH_VREG("ldo1",  LDO,  1,  &pmic4_nldo,      "vdd-l1-l6-l7"),
	RPMH_VREG("ldo2",  LDO,  2,  &pmic4_nldo,      "vdd-l2-l3"),
	RPMH_VREG("ldo3",  LDO,  3,  &pmic4_nldo,      "vdd-l2-l3"),
	/* ldo4 is inaccessible on PM660 */
	RPMH_VREG("ldo5",  LDO,  5,  &pmic4_nldo,      "vdd-l5"),
	RPMH_VREG("ldo6",  LDO,  6,  &pmic4_nldo,      "vdd-l1-l6-l7"),
	RPMH_VREG("ldo7",  LDO,  7,  &pmic4_nldo,      "vdd-l1-l6-l7"),
	RPMH_VREG("ldo8",  LDO,  8,  &pmic4_pldo_lv,   "vdd-l8-l9-l10-l11-l12-l13-l14"),
	RPMH_VREG("ldo9",  LDO,  9,  &pmic4_pldo_lv,   "vdd-l8-l9-l10-l11-l12-l13-l14"),
	RPMH_VREG("ldo10", LDO,  10, &pmic4_pldo_lv,   "vdd-l8-l9-l10-l11-l12-l13-l14"),
	RPMH_VREG("ldo11", LDO,  11, &pmic4_pldo_lv,   "vdd-l8-l9-l10-l11-l12-l13-l14"),
	RPMH_VREG("ldo12", LDO,  12, &pmic4_pldo_lv,   "vdd-l8-l9-l10-l11-l12-l13-l14"),
	RPMH_VREG("ldo13", LDO,  13, &pmic4_pldo_lv,   "vdd-l8-l9-l10-l11-l12-l13-l14"),
	RPMH_VREG("ldo14", LDO,  14, &pmic4_pldo_lv,   "vdd-l8-l9-l10-l11-l12-l13-l14"),
	RPMH_VREG("ldo15", LDO,  15, &pmic4_pldo,      "vdd-l15-l16-l17-l18-l19"),
	RPMH_VREG("ldo16", LDO,  16, &pmic4_pldo,      "vdd-l15-l16-l17-l18-l19"),
	RPMH_VREG("ldo17", LDO,  17, &pmic4_pldo,      "vdd-l15-l16-l17-l18-l19"),
	RPMH_VREG("ldo18", LDO,  18, &pmic4_pldo,      "vdd-l15-l16-l17-l18-l19"),
	RPMH_VREG("ldo19", LDO,  19, &pmic4_pldo,      "vdd-l15-l16-l17-l18-l19"),
	{}
};

static const struct rpmh_vreg_init_data pm660l_vreg_data[] = {
	RPMH_VREG("smps1", SMPS, 1, &pmic4_ftsmps426, "vdd-s1"),
	RPMH_VREG("smps2", SMPS, 2, &pmic4_ftsmps426, "vdd-s2"),
	RPMH_VREG("smps3", SMPS, 3, &pmic4_ftsmps426, "vdd-s3-s4"),
	RPMH_VREG("smps5", SMPS, 5, &pmic4_ftsmps426, "vdd-s5"),
	RPMH_VREG("ldo1",  LDO,  1, &pmic4_nldo,      "vdd-l1-l9-l10"),
	RPMH_VREG("ldo2",  LDO,  2, &pmic4_pldo,      "vdd-l2"),
	RPMH_VREG("ldo3",  LDO,  3, &pmic4_pldo,      "vdd-l3-l5-l7-l8"),
	RPMH_VREG("ldo4",  LDO,  4, &pmic4_pldo,      "vdd-l4-l6"),
	RPMH_VREG("ldo5",  LDO,  5, &pmic4_pldo,      "vdd-l3-l5-l7-l8"),
	RPMH_VREG("ldo6",  LDO,  6, &pmic4_pldo,      "vdd-l4-l6"),
	RPMH_VREG("ldo7",  LDO,  7, &pmic4_pldo,      "vdd-l3-l5-l7-l8"),
	RPMH_VREG("ldo8",  LDO,  8, &pmic4_pldo,      "vdd-l3-l5-l7-l8"),
	RPMH_VREG("bob",   BOB,  1, &pmic4_bob,       "vdd-bob"),
	{}
};

static int rpmh_regulator_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct rpmh_vreg_init_data *vreg_data;
	struct rpmh_vreg *vreg;
	const char *pmic_id;
	int ret;

	vreg_data = of_device_get_match_data(dev);
	if (!vreg_data)
		return -ENODEV;

	ret = of_property_read_string(dev->of_node, "qcom,pmic-id", &pmic_id);
	if (ret < 0) {
		dev_err(dev, "qcom,pmic-id missing in DT node\n");
		return ret;
	}

	for_each_available_child_of_node_scoped(dev->of_node, node) {
		vreg = devm_kzalloc(dev, sizeof(*vreg), GFP_KERNEL);
		if (!vreg)
			return -ENOMEM;

		ret = rpmh_regulator_init_vreg(vreg, dev, node, pmic_id,
						vreg_data);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static const struct of_device_id __maybe_unused rpmh_regulator_match_table[] = {
	{
		.compatible = "qcom,pm8005-rpmh-regulators",
		.data = pm8005_vreg_data,
	},
	{
		.compatible = "qcom,pm8009-rpmh-regulators",
		.data = pm8009_vreg_data,
	},
	{
		.compatible = "qcom,pm8009-1-rpmh-regulators",
		.data = pm8009_1_vreg_data,
	},
	{
		.compatible = "qcom,pm8010-rpmh-regulators",
		.data = pm8010_vreg_data,
	},
	{
		.compatible = "qcom,pm8150-rpmh-regulators",
		.data = pm8150_vreg_data,
	},
	{
		.compatible = "qcom,pm8150l-rpmh-regulators",
		.data = pm8150l_vreg_data,
	},
	{
		.compatible = "qcom,pm8350-rpmh-regulators",
		.data = pm8350_vreg_data,
	},
	{
		.compatible = "qcom,pm8350c-rpmh-regulators",
		.data = pm8350c_vreg_data,
	},
	{
		.compatible = "qcom,pm8450-rpmh-regulators",
		.data = pm8450_vreg_data,
	},
	{
		.compatible = "qcom,pm8550-rpmh-regulators",
		.data = pm8550_vreg_data,
	},
	{
		.compatible = "qcom,pm8550ve-rpmh-regulators",
		.data = pm8550ve_vreg_data,
	},
	{
		.compatible = "qcom,pm8550vs-rpmh-regulators",
		.data = pm8550vs_vreg_data,
	},
	{
		.compatible = "qcom,pm8998-rpmh-regulators",
		.data = pm8998_vreg_data,
	},
	{
		.compatible = "qcom,pmg1110-rpmh-regulators",
		.data = pmg1110_vreg_data,
	},
	{
		.compatible = "qcom,pmi8998-rpmh-regulators",
		.data = pmi8998_vreg_data,
	},
	{
		.compatible = "qcom,pm6150-rpmh-regulators",
		.data = pm6150_vreg_data,
	},
	{
		.compatible = "qcom,pm6150l-rpmh-regulators",
		.data = pm6150l_vreg_data,
	},
	{
		.compatible = "qcom,pm6350-rpmh-regulators",
		.data = pm6350_vreg_data,
	},
	{
		.compatible = "qcom,pmc8180-rpmh-regulators",
		.data = pm8150_vreg_data,
	},
	{
		.compatible = "qcom,pmc8180c-rpmh-regulators",
		.data = pm8150l_vreg_data,
	},
	{
		.compatible = "qcom,pmc8380-rpmh-regulators",
		.data = pmc8380_vreg_data,
	},
	{
		.compatible = "qcom,pmcx0102-rpmh-regulators",
		.data = pmcx0102_vreg_data,
	},
	{
		.compatible = "qcom,pmh0101-rpmh-regulators",
		.data = pmh0101_vreg_data,
	},
	{
		.compatible = "qcom,pmh0104-rpmh-regulators",
		.data = pmh0104_vreg_data,
	},
	{
		.compatible = "qcom,pmh0110-rpmh-regulators",
		.data = pmh0110_vreg_data,
	},
	{
		.compatible = "qcom,pmm8155au-rpmh-regulators",
		.data = pmm8155au_vreg_data,
	},
	{
		.compatible = "qcom,pmm8654au-rpmh-regulators",
		.data = pmm8654au_vreg_data,
	},
	{
		.compatible = "qcom,pmx55-rpmh-regulators",
		.data = pmx55_vreg_data,
	},
	{
		.compatible = "qcom,pmx65-rpmh-regulators",
		.data = pmx65_vreg_data,
	},
	{
		.compatible = "qcom,pmx75-rpmh-regulators",
		.data = pmx75_vreg_data,
	},
	{
		.compatible = "qcom,pm7325-rpmh-regulators",
		.data = pm7325_vreg_data,
	},
	{
		.compatible = "qcom,pm7550-rpmh-regulators",
		.data = pm7550_vreg_data,
	},
	{
		.compatible = "qcom,pmr735a-rpmh-regulators",
		.data = pmr735a_vreg_data,
	},
	{
		.compatible = "qcom,pmr735b-rpmh-regulators",
		.data = pmr735b_vreg_data,
	},
	{
		.compatible = "qcom,pmr735d-rpmh-regulators",
		.data = pmr735d_vreg_data,
	},
	{
		.compatible = "qcom,pm660-rpmh-regulators",
		.data = pm660_vreg_data,
	},
	{
		.compatible = "qcom,pm660l-rpmh-regulators",
		.data = pm660l_vreg_data,
	},
	{}
};
MODULE_DEVICE_TABLE(of, rpmh_regulator_match_table);

static struct platform_driver rpmh_regulator_driver = {
	.driver = {
		.name = "qcom-rpmh-regulator",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
		.of_match_table	= of_match_ptr(rpmh_regulator_match_table),
	},
	.probe = rpmh_regulator_probe,
};
module_platform_driver(rpmh_regulator_driver);

MODULE_DESCRIPTION("Qualcomm RPMh regulator driver");
MODULE_LICENSE("GPL v2");
