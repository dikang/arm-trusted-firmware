/*
 * Copyright (c) 2013-2016, ARM Limited and Contributors. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * Neither the name of ARM nor the names of its contributors may be used
 * to endorse or promote products derived from this software without specific
 * prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <arch_helpers.h>
#include <errno.h>
#include <assert.h>
#include <debug.h>
#define HPSC_NEW_GIC
#ifdef HPSC_NEW_GIC
#include <gicv3.h>
#else
#include <gicv2.h>
#endif
#include <mmio.h>
#include <plat_arm.h>
#include <platform.h>
#include <psci.h>
#include "pm_api_sys.h"
#include "pm_client.h"
#include "zynqmp_private.h"

uintptr_t zynqmp_sec_entry;

#define HPSC_CPU_HOTPLUG
#define HPSC

void zynqmp_cpu_standby(plat_local_state_t cpu_state)
{
	VERBOSE("%s: cpu_state: 0x%x\n", __func__, cpu_state);

	dsb();
	wfi();
}

static int zynqmp_nopmu_pwr_domain_on(u_register_t mpidr)
{
	uint32_t r;
	unsigned int cpu_id = plat_core_pos_by_mpidr(mpidr);

	VERBOSE("%s: mpidr: 0x%lx\n", __func__, mpidr);

#ifdef HPSC

	VERBOSE("%s: mpidr: 0x%lx, cpu_id = %u\n", __func__, mpidr, cpu_id);
	int t_cpu_id = 0;
	if (cpu_id >= 0x100) {
		t_cpu_id = cpu_id - 0x100;
	} else if (cpu_id >= 4) {
		t_cpu_id = cpu_id - 4;
	} else
		t_cpu_id = cpu_id;
WARN("%s: start, cpu_id(%d), t_cpu_id(%d)\n", __func__, cpu_id, t_cpu_id);
#endif
	if (cpu_id == -1)
		return PSCI_E_INTERN_FAIL;

#ifdef HPSC
	if (t_cpu_id == cpu_id) {
#endif
#if 0 /* RVBAR setting through APU is currently broken; value fixed in device tree */
	/* program RVBAR */
	mmio_write_32(APU_RVBAR_L_0 + (cpu_id << 3), zynqmp_sec_entry);	
		// 0xFD5C0000 + 0x40 + (cpu_id << 3)
		// 0xFD5C00{40, 48, 50, 58, 60, 68, 70, 78}
	mmio_write_32(APU_RVBAR_H_0 + (cpu_id << 3), zynqmp_sec_entry >> 32); // 0xFD5C0000 + 0x44
		// 0xFD5C0000 + 0x44 + (cpu_id << 3)
		// 0xFD5C00{44, 4C, 54, 5C, 64, 6C, 74, 7C}
#endif

	/* clear VINITHI */
	r = mmio_read_32(APU_CONFIG_0);
	r &= ~(1 << APU_CONFIG_0_VINITHI_SHIFT << cpu_id);
	mmio_write_32(APU_CONFIG_0, r);

	/* clear power down request */
	r = mmio_read_32(APU_PWRCTL);
	r &= ~(1 << cpu_id);	/* ~(1 << cpu_id) =  */
	mmio_write_32(APU_PWRCTL, r);	// FC5C0090 

#ifdef HPSC
	} else {
#if 0 /* RVBAR setting through APU is currently broken; value fixed in device tree */
	/* program RVBAR */
	mmio_write_32(APU1_RVBAR_L_0 + (t_cpu_id << 3), zynqmp_sec_entry);	
		// 0xFD5C1000 + 0x40 + (cpu_id << 3)
		// 0xFD5C10{40, 48, 50, 58, 60, 68, 70, 78}
	mmio_write_32(APU1_RVBAR_H_0 + (t_cpu_id << 3), zynqmp_sec_entry >> 32); // 0xFD5C0000 + 0x44
		// 0xFD5C1000 + 0x44 + (cpu_id << 3)
		// 0xFD5C10{44, 4C, 54, 5C, 64, 6C, 74, 7C}
#endif

	/* clear VINITHI */
	r = mmio_read_32(APU1_CONFIG_0);
	r &= ~(1 << APU1_CONFIG_0_VINITHI_SHIFT << t_cpu_id);
	mmio_write_32(APU1_CONFIG_0, r);

	/* clear power down request */
	r = mmio_read_32(APU1_PWRCTL);
	r &= ~(1 << t_cpu_id);	/* ~(1 << cpu_id) =  */
	mmio_write_32(APU1_PWRCTL, r);	// FC5C0090 
	}
	if (cpu_id >= 0x100) cpu_id = cpu_id - 0x100 + 4;
#endif
	/* power up island */
	mmio_write_32(PMU_GLOBAL_REQ_PWRUP_EN, 1 << cpu_id);	// DK: looks OK
		// 0xFFD80118 <- 0x{1, 2, 4, 8, 10, 20, 40, 80}
	mmio_write_32(PMU_GLOBAL_REQ_PWRUP_TRIG, 1 << cpu_id);
		// 0xFFD80120 <- 0x{1, 2, 4, 8, 10, 20, 40, 80}	// DK: looks OK
	/* FIXME: we should have a way to break out */
	while (mmio_read_32(PMU_GLOBAL_REQ_PWRUP_STATUS) & (1 << cpu_id))
		;

	/* release core reset */
	r = mmio_read_32(CRF_APB_RST_FPD_APU);
#ifdef HPSC_CPU_HOTPLUG
	unsigned new_r = r | (CRF_APB_RST_FPD_APU_ACPU_RESET << cpu_id);
	if ((r & (CRF_APB_RST_FPD_APU_ACPU_RESET << cpu_id)) == 0) { 
		// reset for cpu_id is 0. Then reset would not be triggered.
		// Set the reset for cpu_id at 1.
		mmio_write_32(CRF_APB_RST_FPD_APU, new_r);
	new_r = mmio_read_32(CRF_APB_RST_FPD_APU);
WARN("%s: fix: set it to 1, and then read CRF_APB_RST_FPD_APU(%x)\n", __func__, new_r);
	}
#endif
	r &= ~((CRF_APB_RST_FPD_APU_ACPU_PWRON_RESET |
			CRF_APB_RST_FPD_APU_ACPU_RESET) << cpu_id);
	mmio_write_32(CRF_APB_RST_FPD_APU, r);	// DK: looks OK
		// 0xFD1A0104 <- ~(((1 << 10) | (1 << 0) << cpu_id)
		// 0xFD1A0104 <- ~( 0x400 | (1 << 0) << cpu_id)

	return PSCI_E_SUCCESS;
}

static int zynqmp_pwr_domain_on(u_register_t mpidr)
{
	unsigned int cpu_id = plat_core_pos_by_mpidr(mpidr);
	const struct pm_proc *proc;
	VERBOSE("%s: cpu_id(0x%x):  mpidr: 0x%lx\n", __func__, cpu_id, mpidr);
#ifdef HPSC
if (cpu_id >= 0x100) cpu_id = cpu_id - 0x100 + 4;
#endif

	if (cpu_id == -1)
		return PSCI_E_INTERN_FAIL;

#if LOG_LEVEL >= LOG_LEVEL_VERBOSE
	int kk;
	aff_info_state_t kk2[8];
	for (kk = 0; kk < 8; kk++) {
		kk2[kk] = get_cpu_data_by_index(kk,psci_svc_cpu_data.aff_info_state); 
	}
	VERBOSE("%s: power state = (%d, %d, %d, %d, %d, %d, %d, %d)\n",
		__func__, kk2[0], kk2[1], kk2[2], kk2[3], kk2[4], kk2[5], kk2[6], kk2[7]); 
#endif
	proc = pm_get_proc(cpu_id);
	/* Clear power down request */
#if LOG_LEVEL >= LOG_LEVEL_VERBOSE
	for (kk = 0; kk < 8; kk++) {
		kk2[kk] = get_cpu_data_by_index(kk,psci_svc_cpu_data.aff_info_state); 
	}
	VERBOSE("%s: power state = (%d, %d, %d, %d, %d, %d, %d, %d)\n",
		__func__, kk2[0], kk2[1], kk2[2], kk2[3], kk2[4], kk2[5], kk2[6], kk2[7]); 
#endif
	pm_client_wakeup(proc);
#if LOG_LEVEL >= LOG_LEVEL_VERBOSE
	for (kk = 0; kk < 8; kk++) {
		kk2[kk] = get_cpu_data_by_index(kk,psci_svc_cpu_data.aff_info_state); 
	}
	VERBOSE("%s: power state = (%d, %d, %d, %d, %d, %d, %d, %d)\n",
		__func__, kk2[0], kk2[1], kk2[2], kk2[3], kk2[4], kk2[5], kk2[6], kk2[7]); 
#endif

	/* Send request to PMU to wake up selected APU CPU core */
	pm_req_wakeup(proc->node_id, 1, zynqmp_sec_entry, REQ_ACK_BLOCKING);
#if LOG_LEVEL >= LOG_LEVEL_VERBOSE
	for (kk = 0; kk < 8; kk++) {
		kk2[kk] = get_cpu_data_by_index(kk,psci_svc_cpu_data.aff_info_state); 
	}
	VERBOSE("%s: power state = (%d, %d, %d, %d, %d, %d, %d, %d)\n",
		__func__, kk2[0], kk2[1], kk2[2], kk2[3], kk2[4], kk2[5], kk2[6], kk2[7]); 
#endif

	return PSCI_E_SUCCESS;
}

static void zynqmp_nopmu_pwr_domain_off(const psci_power_state_t *target_state)
{
	uint32_t r;
	unsigned int cpu_id = plat_my_core_pos();

	for (size_t i = 0; i <= PLAT_MAX_PWR_LVL; i++)
		VERBOSE("%s: target_state->pwr_domain_state[%lu]=%x\n",
			__func__, i, target_state->pwr_domain_state[i]);

	/* Prevent interrupts from spuriously waking up this cpu */
#ifdef HPSC_NEW_GIC
	gicv3_cpuif_disable(plat_my_core_pos());
#else
	gicv2_cpuif_disable();
#endif

	/* set power down request */
	r = mmio_read_32(APU_PWRCTL);
VERBOSE("%s: cpu_id = %d : mmio_read_32(APU_PWRCTL) : 0x%x \n", __func__, cpu_id, r);
	r |= (1 << cpu_id);
	mmio_write_32(APU_PWRCTL, r);
VERBOSE("%s: cpu_id = %d : mmio_write_32(APU_PWRCTL, 0x%x) \n", __func__, cpu_id, r);

#if LOG_LEVEL >= LOG_LEVEL_VERBOSE
 r = mmio_read_32(CRF_APB_RST_FPD_APU);
 VERBOSE("zynqmp_nopmu_pwr_domain_off: read CRF_APB_RST_FPD_APU(%x)\n", r);
#endif
}

static void zynqmp_pwr_domain_off(const psci_power_state_t *target_state)
{
	unsigned int cpu_id = plat_my_core_pos();
	const struct pm_proc *proc = pm_get_proc(cpu_id);

	for (size_t i = 0; i <= PLAT_MAX_PWR_LVL; i++)
		VERBOSE("%s: target_state->pwr_domain_state[%lu]=%x\n",
			__func__, i, target_state->pwr_domain_state[i]);

	/* Prevent interrupts from spuriously waking up this cpu */
#ifdef HPSC_NEW_GIC
	gicv3_cpuif_disable(plat_my_core_pos());
#else
	gicv2_cpuif_disable();
#endif

	/*
	 * Send request to PMU to power down the appropriate APU CPU
	 * core.
	 * According to PSCI specification, CPU_off function does not
	 * have resume address and CPU core can only be woken up
	 * invoking CPU_on function, during which resume address will
	 * be set.
	 */
	pm_self_suspend(proc->node_id, MAX_LATENCY, PM_STATE_CPU_IDLE, 0);
}

static void zynqmp_nopmu_pwr_domain_suspend(const psci_power_state_t *target_state)
{
	uint32_t r;
	unsigned int cpu_id = plat_my_core_pos();

	for (size_t i = 0; i <= PLAT_MAX_PWR_LVL; i++)
		VERBOSE("%s: cpu_id(0x%x): target_state->pwr_domain_state[%lu]=%x\n",
			__func__, cpu_id, i, target_state->pwr_domain_state[i]);

#ifdef HPSC
	int t_cpu_id = 0;
	if (cpu_id >= 0x100) t_cpu_id = cpu_id - 0x100;
	else if (cpu_id >= 4) t_cpu_id = cpu_id - 4;
	else t_cpu_id = cpu_id;
WARN("%s: start, cpu_id(%d), t_cpu_id(%d)\n", __func__, cpu_id, t_cpu_id);

	if (t_cpu_id == cpu_id) {
#endif
	/* set power down request */
	r = mmio_read_32(APU_PWRCTL);
	r |= (1 << cpu_id);
	mmio_write_32(APU_PWRCTL, r);

#if 0 /* RVBAR setting through APU is currently broken; value fixed in device tree */
	/* program RVBAR */
	mmio_write_32(APU_RVBAR_L_0 + (cpu_id << 3), zynqmp_sec_entry);
	mmio_write_32(APU_RVBAR_H_0 + (cpu_id << 3), zynqmp_sec_entry >> 32);
#endif

	/* clear VINITHI */
	r = mmio_read_32(APU_CONFIG_0);
	r &= ~(1 << APU_CONFIG_0_VINITHI_SHIFT << cpu_id);
	mmio_write_32(APU_CONFIG_0, r);
#ifdef HPSC
	} else {
	/* set power down request */
	r = mmio_read_32(APU1_PWRCTL);
	r |= (1 << t_cpu_id);
	mmio_write_32(APU1_PWRCTL, r);

#if 0 /* RVBAR setting through APU is currently broken; value fixed in device tree */
	/* program RVBAR */
	mmio_write_32(APU1_RVBAR_L_0 + (t_cpu_id << 3), zynqmp_sec_entry);
	mmio_write_32(APU1_RVBAR_H_0 + (t_cpu_id << 3), zynqmp_sec_entry >> 32);
#endif

	/* clear VINITHI */
	r = mmio_read_32(APU1_CONFIG_0);
	r &= ~(1 << APU1_CONFIG_0_VINITHI_SHIFT << t_cpu_id);
	mmio_write_32(APU1_CONFIG_0, r);
	}
#endif
	/* enable power up on IRQ */
	mmio_write_32(PMU_GLOBAL_REQ_PWRUP_EN, 1 << cpu_id);
}

static void zynqmp_pwr_domain_suspend(const psci_power_state_t *target_state)
{
	unsigned int state;
	unsigned int cpu_id = plat_my_core_pos();
	const struct pm_proc *proc = pm_get_proc(cpu_id);

	for (size_t i = 0; i <= PLAT_MAX_PWR_LVL; i++)
		VERBOSE("%s: cpu_id(0x%x): target_state->pwr_domain_state[%lu]=%x\n",
			__func__, cpu_id, i, target_state->pwr_domain_state[i]);

	state = target_state->pwr_domain_state[1] > PLAT_MAX_RET_STATE ?
		PM_STATE_SUSPEND_TO_RAM : PM_STATE_CPU_IDLE;

	/* Send request to PMU to suspend this core */
	pm_self_suspend(proc->node_id, MAX_LATENCY, state, zynqmp_sec_entry);

	/* APU is to be turned off */
	if (target_state->pwr_domain_state[1] > PLAT_MAX_RET_STATE) {
		/* disable coherency */
		plat_arm_interconnect_exit_coherency();
	}
}

static void zynqmp_pwr_domain_on_finish(const psci_power_state_t *target_state)
{
	for (size_t i = 0; i <= PLAT_MAX_PWR_LVL; i++)
		VERBOSE("%s: target_state->pwr_domain_state[%lu]=%x\n",
			__func__, i, target_state->pwr_domain_state[i]);

#ifdef HPSC_NEW_GIC
	gicv3_cpuif_enable(plat_my_core_pos());
	gicv3_rdistif_init(plat_my_core_pos());
#else
	gicv2_cpuif_enable();
	gicv2_pcpu_distif_init();
#endif
}

static void zynqmp_nopmu_pwr_domain_suspend_finish(const psci_power_state_t *target_state)
{
	uint32_t r;
	unsigned int cpu_id = plat_my_core_pos();

	for (size_t i = 0; i <= PLAT_MAX_PWR_LVL; i++)
		VERBOSE("%s: cpu_id(0x%x): target_state->pwr_domain_state[%lu]=%x\n",
			__func__, cpu_id, i, target_state->pwr_domain_state[i]);

	/* disable power up on IRQ */
	mmio_write_32(PMU_GLOBAL_REQ_PWRUP_DIS, 1 << cpu_id);

#ifdef HPSC
	int t_cpu_id = 0;
	if (cpu_id >= 0x100) t_cpu_id = cpu_id - 0x100;
	else if (cpu_id >= 4) t_cpu_id = cpu_id - 4;
	else t_cpu_id = cpu_id;
WARN("%s: start, cpu_id(%d), t_cpu_id(%d)\n", __func__, cpu_id, t_cpu_id);

	if (t_cpu_id == cpu_id) {
#endif
	/* clear powerdown bit */
	r = mmio_read_32(APU_PWRCTL);
	r &= ~(1 << cpu_id);
	mmio_write_32(APU_PWRCTL, r);
#ifdef HPSC
	} else {
	/* clear powerdown bit */
	r = mmio_read_32(APU1_PWRCTL);
	r &= ~(1 << t_cpu_id);
	mmio_write_32(APU1_PWRCTL, r);
	}
#endif
}

static void zynqmp_pwr_domain_suspend_finish(const psci_power_state_t *target_state)
{
	unsigned int cpu_id = plat_my_core_pos();
	const struct pm_proc *proc = pm_get_proc(cpu_id);

	for (size_t i = 0; i <= PLAT_MAX_PWR_LVL; i++)
		VERBOSE("%s: cpu(%d): target_state->pwr_domain_state[%lu]=%x\n",
			__func__, cpu_id, i, target_state->pwr_domain_state[i]);

	/* Clear the APU power control register for this cpu */
	pm_client_wakeup(proc);

	/* enable coherency */
	plat_arm_interconnect_enter_coherency();
	/* APU was turned off */
	if (target_state->pwr_domain_state[1] > PLAT_MAX_RET_STATE) {	/* > 1 */
		VERBOSE("%s: cpu(%d): plat_arm_gic_init()\n", __func__, cpu_id);
		plat_arm_gic_init();
	} else {
#ifdef HPSC_NEW_GIC
		VERBOSE("%s: cpu(%d): gicv3_cpuif_enable()\n", __func__, cpu_id);
		gicv3_cpuif_enable(plat_my_core_pos());
		gicv3_rdistif_init(plat_my_core_pos());
#else
		VERBOSE("%s: cpu(%d): gicv2_cpuif_enable()\n", __func__, cpu_id);
		gicv2_cpuif_enable();
		gicv2_pcpu_distif_init();
#endif
	}
}

/*******************************************************************************
 * ZynqMP handlers to shutdown/reboot the system
 ******************************************************************************/
static void __dead2 zynqmp_nopmu_system_off(void)
{
//	ERROR("ZynqMP System Off: operation not handled.\n");

	/* disable coherency */
	plat_arm_interconnect_exit_coherency();
#ifdef HPSC
	/* trigger system reset */
	mmio_write_32(CRL_APB_RESET_CTRL, 1 << 5);
	while (1)
		wfi();
#else
	panic();
#endif
}

static void __dead2 zynqmp_system_off(void)
{
	/* disable coherency */
	plat_arm_interconnect_exit_coherency();

	/* Send the power down request to the PMU */
	pm_system_shutdown(PMF_SHUTDOWN_TYPE_SHUTDOWN,
			   pm_get_shutdown_scope());

	while (1)
		wfi();
}

static void __dead2 zynqmp_nopmu_system_reset(void)
{
	/* DK: if trigger APU only reset */
	/* But, then who will load the images? */
	/* this does not work, because the images (Linux, FDT, rootfs, etc.) are not reloaded by the system */
	/*    --> it keeps idling. */
	/* With TRCH, it can ask TRCH to start a boot process for Linux. */

	/*
	 * This currently triggers a system reset. I.e. the whole
	 * system will be reset! Including RPUs, PMU, PL, etc.
	 */

	/* disable coherency */
	plat_arm_interconnect_exit_coherency();

	/* bypass RPLL (needed on 1.0 silicon) */
	uint32_t reg = mmio_read_32(CRL_APB_RPLL_CTRL);
	reg |= CRL_APB_RPLL_CTRL_BYPASS;
	mmio_write_32(CRL_APB_RPLL_CTRL, reg);

#ifdef HPSC
	/* trigger A53's reset first*/
	mmio_write_32(CRF_APB_RST_FPD_APU, 1<<5); 
#endif
	/* trigger system reset */
	mmio_write_32(CRL_APB_RESET_CTRL, CRL_APB_RESET_CTRL_SOFT_RESET);
	while (1)
		wfi();
}

static void __dead2 zynqmp_system_reset(void)
{
	/* disable coherency */
	plat_arm_interconnect_exit_coherency();

	/* Send the system reset request to the PMU */
	pm_system_shutdown(PMF_SHUTDOWN_TYPE_RESET,
			   pm_get_shutdown_scope());

	while (1)
		wfi();
}

int zynqmp_validate_power_state(unsigned int power_state,
				psci_power_state_t *req_state)
{
	VERBOSE("%s: power_state: 0x%x\n", __func__, power_state);

	int pstate = psci_get_pstate_type(power_state);

	assert(req_state);

	/* Sanity check the requested state */
	if (pstate == PSTATE_TYPE_STANDBY)
		req_state->pwr_domain_state[MPIDR_AFFLVL0] = PLAT_MAX_RET_STATE;
	else
		req_state->pwr_domain_state[MPIDR_AFFLVL0] = PLAT_MAX_OFF_STATE;

	/* We expect the 'state id' to be zero */
	if (psci_get_pstate_id(power_state))
		return PSCI_E_INVALID_PARAMS;

	return PSCI_E_SUCCESS;
}

int zynqmp_validate_ns_entrypoint(unsigned long ns_entrypoint)
{
	VERBOSE("%s: ns_entrypoint: 0x%lx\n", __func__, ns_entrypoint);

	/* FIXME: Actually validate */
	return PSCI_E_SUCCESS;
}

void zynqmp_get_sys_suspend_power_state(psci_power_state_t *req_state)
{
	req_state->pwr_domain_state[PSCI_CPU_PWR_LVL] = PLAT_MAX_OFF_STATE;
	req_state->pwr_domain_state[1] = PLAT_MAX_OFF_STATE;
}

/*******************************************************************************
 * Export the platform handlers to enable psci to invoke them
 ******************************************************************************/
static const struct plat_psci_ops zynqmp_psci_ops = {
	.cpu_standby			= zynqmp_cpu_standby,
	.pwr_domain_on			= zynqmp_pwr_domain_on,
	.pwr_domain_off			= zynqmp_pwr_domain_off,
	.pwr_domain_suspend		= zynqmp_pwr_domain_suspend,
	.pwr_domain_on_finish		= zynqmp_pwr_domain_on_finish,
	.pwr_domain_suspend_finish	= zynqmp_pwr_domain_suspend_finish,
	.system_off			= zynqmp_system_off,
	.system_reset			= zynqmp_system_reset,
	.validate_power_state		= zynqmp_validate_power_state,
	.validate_ns_entrypoint		= zynqmp_validate_ns_entrypoint,
	.get_sys_suspend_power_state	= zynqmp_get_sys_suspend_power_state,
};

static const struct plat_psci_ops zynqmp_nopmu_psci_ops = {
	.cpu_standby			= zynqmp_cpu_standby,
	.pwr_domain_on			= zynqmp_nopmu_pwr_domain_on,
	.pwr_domain_off			= zynqmp_nopmu_pwr_domain_off,
	.pwr_domain_suspend		= zynqmp_nopmu_pwr_domain_suspend,
	.pwr_domain_on_finish		= zynqmp_pwr_domain_on_finish,
	.pwr_domain_suspend_finish	= zynqmp_nopmu_pwr_domain_suspend_finish,
	.system_off			= zynqmp_nopmu_system_off,
	.system_reset			= zynqmp_nopmu_system_reset,
	.validate_power_state		= zynqmp_validate_power_state,
	.validate_ns_entrypoint		= zynqmp_validate_ns_entrypoint,
	.get_sys_suspend_power_state	= zynqmp_get_sys_suspend_power_state,
};

/*******************************************************************************
 * Export the platform specific power ops.
 ******************************************************************************/
int plat_setup_psci_ops(uintptr_t sec_entrypoint,
			const struct plat_psci_ops **psci_ops)
{
	zynqmp_sec_entry = sec_entrypoint;

	if (zynqmp_is_pmu_up())
		*psci_ops = &zynqmp_psci_ops;
	else
		*psci_ops = &zynqmp_nopmu_psci_ops;

	return 0;
}
