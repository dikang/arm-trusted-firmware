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

#include <bakery_lock.h>
#include <mmio.h>
#include <platform.h>
#include <arch_helpers.h>
#include "pm_ipi.h"
#include "../zynqmp_private.h"

#define DK
#define DK_DEBUG
/* IPI message buffers */
#define IPI_BUFFER_BASEADDR	0xFF990000U

#define IPI_BUFFER_RPU_0_BASE	(IPI_BUFFER_BASEADDR + 0x0U)
#define IPI_BUFFER_RPU_1_BASE	(IPI_BUFFER_BASEADDR + 0x200U)
#define IPI_BUFFER_APU_BASE	(IPI_BUFFER_BASEADDR + 0x400U)
#define IPI_BUFFER_PL_0_BASE	(IPI_BUFFER_BASEADDR + 0x600U)
#define IPI_BUFFER_PL_1_BASE	(IPI_BUFFER_BASEADDR + 0x800U)
#define IPI_BUFFER_PL_2_BASE	(IPI_BUFFER_BASEADDR + 0xA00U)
#define IPI_BUFFER_PL_3_BASE	(IPI_BUFFER_BASEADDR + 0xC00U)
#define IPI_BUFFER_PMU_BASE	(IPI_BUFFER_BASEADDR + 0xE00U)

#define IPI_BUFFER_TARGET_RPU_0_OFFSET	0x0U
#define IPI_BUFFER_TARGET_RPU_1_OFFSET	0x40U
#define IPI_BUFFER_TARGET_APU_OFFSET	0x80U
#define IPI_BUFFER_TARGET_PL_0_OFFSET	0xC0U
#define IPI_BUFFER_TARGET_PL_1_OFFSET	0x100U
#define IPI_BUFFER_TARGET_PL_2_OFFSET	0x140U
#define IPI_BUFFER_TARGET_PL_3_OFFSET	0x180U
#define IPI_BUFFER_TARGET_PMU_OFFSET	0x1C0U

#define IPI_BUFFER_MAX_WORDS	8

#define IPI_BUFFER_REQ_OFFSET	0x0U
#define IPI_BUFFER_RESP_OFFSET	0x20U

/* IPI Base Address */
#define IPI_BASEADDR		0XFF300000

/* APU's IPI registers */
#define IPI_APU_ISR		(IPI_BASEADDR + 0X00000010)
#define IPI_APU_IER		(IPI_BASEADDR + 0X00000018)
#define IPI_APU_IDR		(IPI_BASEADDR + 0X0000001C)
#if !ZYNQMP_WARM_RESTART
#define IPI_APU_IXR_PMU_0_MASK         (1 << 16)
#endif

#define IPI_TRIG_OFFSET		0
#define IPI_OBS_OFFSET		4

/* Power Management IPI interrupt number */
#define PM_INT_NUM		0
#define IPI_PMU_PM_INT_BASE	(IPI_PMU_0_TRIG + (PM_INT_NUM * 0x1000))
#define IPI_PMU_PM_INT_MASK	(IPI_APU_IXR_PMU_0_MASK << PM_INT_NUM)
#if (PM_INT_NUM < 0 || PM_INT_NUM > 3)
	#error PM_INT_NUM value out of range
#endif

#define IPI_APU_MASK		1U

DEFINE_BAKERY_LOCK(pm_secure_lock);
#if ZYNQMP_WARM_RESTART
static uint32_t (*usr_fiq_handler)(void);
#endif

const struct pm_ipi apu_ipi = {
	.mask = IPI_APU_MASK,
	.base = IPI_BASEADDR,
	.buffer_base = IPI_BUFFER_APU_BASE,
};

#if ZYNQMP_WARM_RESTART
/**
 * ipi_fiq_handler() - IPI Handler for PM-API callbacks
 * @id - 	number of the highest priority pending interrupt of the type
 *		that this handler was registered for
 * @flags - 	security state, bit[0]
 * @handler - 	pointer to 'cpu_context' structure of the current CPU for the
 * 	      	security state specified in the 'flags' parameter
 * @cookie  - 	unused
 *
 * Function registered as INTR_TYPE_EL3 interrupt handler
 *
 * PMU sends IPI interrupts for PM-API callbacks. If a handler is registered,
 * the handler is called with a pointer to the payload as argument.
 *
 * In presence of non-secure software layers (EL1/2) sets the interrupt
 * at registered entrance in GIC and informs that PMU responsed or demands
 * action
 */
static uint64_t ipi_fiq_handler(uint32_t id, uint32_t flags, void *handle,
			 void *cookie)
{
	INFO("BL31: Got FIQ\n");
	uint32_t handled = usr_fiq_handler();

	/* Clear IPI_APU_ISR bit */
	mmio_write_32(IPI_APU_ISR, handled);

	return 0;
}
#endif

/**
 * pm_ipi_init() - Initialize IPI peripheral for communication with PMU
 *
 * @return	On success, the initialization function must return 0.
 *		Any other return value will cause the framework to ignore
 *		the service
 *
 * Called from pm_setup initialization function
 */
#if !ZYNQMP_WARM_RESTART
int pm_ipi_init(void)
{
	bakery_lock_init(&pm_secure_lock);

	/* IPI Interrupts Clear & Disable */
	mmio_write_32(IPI_APU_ISR, 0xffffffff);
	mmio_write_32(IPI_APU_IDR, 0xffffffff);

	return 0;
}
#else
int pm_ipi_init(uint32_t (*fiq_handler)(void))
{
	int ret;
	bakery_lock_init(&pm_secure_lock);

	/* IPI Interrupts Clear & Disable */
	mmio_write_32(IPI_APU_ISR, 0xffffffff);
	mmio_write_32(IPI_APU_IDR, 0xffffffff);

	/* Register IPI interrupt as INTR_TYPE_EL3 */
	if (fiq_handler) {
		usr_fiq_handler = fiq_handler;
		ret = request_intr_type_el3(IRQ_SEC_IPI_APU, ipi_fiq_handler);
		if (ret)
			return ret;
	}
	/* enable IRQ from PMU 1 */
	mmio_write_32(IPI_APU_IER, IPI_APU_IXR_PMU_1_MASK);
	INFO("BL31: enabled warm reset FIQ\n");

	return 0;
}
#endif
/**
 * pm_ipi_wait() - wait for pmu to handle request
 * @proc	proc which is waiting for PMU to handle request
 */
static enum pm_ret_status pm_ipi_wait(const struct pm_proc *proc)
{
	int status;

	/* Wait until previous interrupt is handled by PMU */
	do {
		status = mmio_read_32(proc->ipi->base + IPI_OBS_OFFSET) &
					IPI_PMU_PM_INT_MASK;
		/* TODO: 1) Use timer to add delay between read attempts */
		/* TODO: 2) Return PM_RET_ERR_TIMEOUT if this times out */
	} while (status);

	return PM_RET_SUCCESS;
}

/**
 * pm_ipi_send_common() - Sends IPI request to the PMU
 * @proc	Pointer to the processor who is initiating request
 * @payload	API id and call arguments to be written in IPI buffer
 *
 * Send an IPI request to the power controller. Caller needs to hold
 * the 'pm_secure_lock' lock.
 *
 * @return	Returns status, either success or error+reason
 */
static enum pm_ret_status pm_ipi_send_common(const struct pm_proc *proc,
					     uint32_t payload[PAYLOAD_ARG_CNT])
{
	unsigned int offset = 0;
	uintptr_t buffer_base = proc->ipi->buffer_base +
					IPI_BUFFER_TARGET_PMU_OFFSET +
					IPI_BUFFER_REQ_OFFSET;
	/*DK 0xFF9905C0 */

	/* Wait until previous interrupt is handled by PMU */
	pm_ipi_wait(proc);

	/* Write payload into IPI buffer */
	for (size_t i = 0; i < PAYLOAD_ARG_CNT; i++) {
		mmio_write_32(buffer_base + offset, payload[i]);
		offset += PAYLOAD_ARG_SIZE;
	}
#ifdef DK__
	VERBOSE("pm_ipi_send_common: mmio_write_32, buffer_base(%lx): %x %x %x %x %x %x\n", 
		buffer_base, payload[0], payload[1], payload[2], payload[3], payload[4], payload[5]);
#endif
	/* Generate IPI to PMU */
	mmio_write_32(proc->ipi->base + IPI_TRIG_OFFSET, IPI_PMU_PM_INT_MASK);

	return PM_RET_SUCCESS;
}

/**
 * pm_ipi_send() - Sends IPI request to the PMU
 * @proc	Pointer to the processor who is initiating request
 * @payload	API id and call arguments to be written in IPI buffer
 *
 * Send an IPI request to the power controller.
 *
 * @return	Returns status, either success or error+reason
 */
enum pm_ret_status pm_ipi_send(const struct pm_proc *proc,
			       uint32_t payload[PAYLOAD_ARG_CNT])
{
	enum pm_ret_status ret;
#ifdef DK
	VERBOSE("pm_ipi_send : start \n");
#endif

	bakery_lock_get(&pm_secure_lock);

	ret = pm_ipi_send_common(proc, payload);

	bakery_lock_release(&pm_secure_lock);

	return ret;
}


/**
 * pm_ipi_buff_read() - Reads IPI response after PMU has handled interrupt
 * @proc	Pointer to the processor who is waiting and reading response
 * @value	Used to return value from IPI buffer element (optional)
 * @count	Number of values to return in @value
 *
 * @return	Returns status, either success or error+reason
 */
static enum pm_ret_status pm_ipi_buff_read(const struct pm_proc *proc,
					   unsigned int *value, size_t count)
{
	size_t i;
	uintptr_t buffer_base = proc->ipi->buffer_base +
				IPI_BUFFER_TARGET_PMU_OFFSET +
				IPI_BUFFER_RESP_OFFSET;
	pm_ipi_wait(proc);

	/*
	 * Read response from IPI buffer
	 * buf-0: success or error+reason
	 * buf-1: value
	 * buf-2: unused
	 * buf-3: unused
	 */
	for (i = 1; i <= count; i++) {
		*value = mmio_read_32(buffer_base + (i * PAYLOAD_ARG_SIZE));
#ifdef DK_DEBUG__
	VERBOSE("%s: value[%ld] = 0x%x\n", __func__, i, *value);
#endif
		value++;
	}
#ifdef DK_DEBUG__
	VERBOSE("%s: count = %ld, read %lx \n", __func__, count, buffer_base);
#endif

	return mmio_read_32(buffer_base);
}

/**
 * pm_ipi_buff_read_callb() - Reads IPI response after PMU has handled interrupt
 * @value	Used to return value from IPI buffer element (optional)
 * @count	Number of values to return in @value
 *
 * @return	Returns status, either success or error+reason
 */
void pm_ipi_buff_read_callb(unsigned int *value, size_t count)
{
	size_t i;
	uintptr_t buffer_base = IPI_BUFFER_PMU_BASE +
				IPI_BUFFER_TARGET_APU_OFFSET +
				IPI_BUFFER_REQ_OFFSET;

	if (count > IPI_BUFFER_MAX_WORDS)
		count = IPI_BUFFER_MAX_WORDS;

	for (i = 0; i <= count; i++) {
		*value = mmio_read_32(buffer_base + (i * PAYLOAD_ARG_SIZE));
		value++;
	}
}

/**
 * pm_ipi_send_sync() - Sends IPI request to the PMU
 * @proc	Pointer to the processor who is initiating request
 * @payload	API id and call arguments to be written in IPI buffer
 * @value	Used to return value from IPI buffer element (optional)
 * @count	Number of values to return in @value
 *
 * Send an IPI request to the power controller and wait for it to be handled.
 *
 * @return	Returns status, either success or error+reason and, optionally,
 *		@value
 */
enum pm_ret_status pm_ipi_send_sync(const struct pm_proc *proc,
				    uint32_t payload[PAYLOAD_ARG_CNT],
				    unsigned int *value, size_t count)
{
	enum pm_ret_status ret;
#ifdef DK_DEBUG__
	VERBOSE("pm_ipi_send_sync: start \n");
#endif

	bakery_lock_get(&pm_secure_lock);

	ret = pm_ipi_send_common(proc, payload);
#ifdef DK_DEBUG__
	VERBOSE("pm_ipi_send_sync: after pm_ipi_send_common: return(%d)\n", ret);
#endif
	if (ret != PM_RET_SUCCESS)
		goto unlock;

	ret = pm_ipi_buff_read(proc, value, count);

#ifdef DK_DEBUG__
	VERBOSE("pm_ipi_send_sync: after pm_ipi_buff_read: return(%d)\n", ret);
#endif

unlock:
	bakery_lock_release(&pm_secure_lock);

	return ret;
}

void pm_ipi_irq_enable(void)
{
	mmio_write_32(IPI_APU_IER, IPI_APU_IXR_PMU_0_MASK);
}

void pm_ipi_irq_disable(void)
{
	mmio_write_32(IPI_APU_IDR, IPI_APU_IXR_PMU_0_MASK);
}

void pm_ipi_irq_clear(void)
{
	mmio_write_32(IPI_APU_ISR, IPI_APU_IXR_PMU_0_MASK);
}
