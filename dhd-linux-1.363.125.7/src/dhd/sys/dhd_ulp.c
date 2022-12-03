/*
 * Broadcom Dongle Host Driver (DHD)
 * DHD Implementation for ULP mode opernation.
 *
 * $Copyright Open Broadcom Corporation$
 *
 * $Id: $
 */

#ifdef DHD_ULP

#include <typedefs.h>
#include <bcmutils.h>
#include <bcmendian.h>
#include <bcmdevs.h>
#include <siutils.h>
#include <sbgci.h>
#include <hndsoc.h>

#include <proto/ethernet.h>
#include <proto/bcmevent.h>
#include <dngl_stats.h>

#include <sdio.h>
#include <dhd.h>

#include <dhd_bus.h>
#include <dhd_proto.h>
#include <dhd_ulp.h>
#include <dhd_dbg.h>

#define F2EN_SET 1
#define F2EN_CLEAR 0

/* ULP DHD F2 Enable set/clear wait time */
#define DHD_ULP_F2ENAB_SET_CLEAR_TIME 4000

#define DHD_ULP_IDLE		(0)
#define DHD_ULP_TRIGGERED	(1)

typedef struct dhd_ulp {
	atomic_t dhd_ulp_txrx_state;
	tsk_ctl_t thr_ulp_ctl;
	enum dhd_ulp_paths dhd_ulp_path;
	uint dhd_ulp_console_count;
	uint dhd_ulp_state;
	atomic_t dhd_ulp_ucode_oobint;
} dhd_ulp_t;

/* Function to get current DHD ULP state */
enum dhd_ulp_states
dhd_ulp_get_ulp_state(dhd_pub_t *dhdp)
{
	dhd_ulp_t *dhd_ulp;

	DHD_NULL_CHK_AND_RET_VAL(dhdp, DHD_ULP_MAX_STATE);

	dhd_ulp = dhdp->dhd_ulp;
	DHD_NULL_CHK_AND_RET_VAL(dhd_ulp, DHD_ULP_MAX_STATE);

	return atomic_read(&dhd_ulp->dhd_ulp_txrx_state);
}

/* Function to set DHD ULP state, with state passed in the function argument */
void
dhd_ulp_set_ulp_state(dhd_pub_t *dhdp, enum dhd_ulp_states ulp_state)
{
	dhd_ulp_t *dhd_ulp;

	DHD_NULL_CHK_AND_RET(dhdp);

	dhd_ulp = dhdp->dhd_ulp;
	DHD_NULL_CHK_AND_RET(dhd_ulp);

	DHD_TRACE(("%s Setting ulp state to %d\n", __FUNCTION__, ulp_state));
	atomic_set(&dhd_ulp->dhd_ulp_txrx_state, ulp_state);
}

/*
 * This function checks the DHD ULP state verification before FW re-download.
 * This function also does min rest mask setting depending on chip ID before redownload.
 */
bool
dhd_ulp_pre_redownload_check(dhd_pub_t *dhdp, bcmsdh_info_t *sdh, void *sih, uint32 hmbdata)
{
	dhd_ulp_t *dhd_ulp;

#ifdef DHD_HUDI
	dhd_timeout_t tmo;
	uint32 drvr_ucode_if_ptr, val32, ulp_wake_ind, wowl_wake_ind;
#endif /* DHD_HUDI */

	DHD_NULL_CHK_AND_RET_VAL(dhdp, FALSE);

	dhd_ulp = dhdp->dhd_ulp;
	DHD_NULL_CHK_AND_RET_VAL(dhd_ulp, FALSE);

	DHD_TRACE(("%s: dhd_ulp_path: %d\n", __func__, dhd_ulp->dhd_ulp_path));

	if (hmbdata) {
		return FALSE;
	}

	DHD_ERROR(("%s: GOT THE INTERRUPT FROM UCODE\n", __FUNCTION__));

	bcmsdh_cfg_write(sdh, SDIO_FUNC_0, SDIOD_CCCR_IOEN, SDIO_FUNC_ENABLE_1, NULL);

#ifdef DHD_HUDI
	/* CASE 1: Wake up becuase of RX and other misc error conditions. */
	/*     - RX (magic packet wakeup, netpattern wakeup, unknown pkt etc)
	 *     - M_ULP_WAKE_IND
	 *			Error conditions like
	 *			- Watchdog expiry
	 *			- FCBS error
	 *			- Re-transmission failure
	 *			- Invalid FCBS block
	 */
	drvr_ucode_if_ptr = D11SHM_RD(sdh, M_DRVR_UCODE_IF_PTR);
	DHD_TRACE(("%s: M_DRVR_UCODE_IF_PTR: 0x%08x\n", __func__, drvr_ucode_if_ptr));

	ulp_wake_ind = D11SHM_RD(sdh, (drvr_ucode_if_ptr * 2 + M_ULP_WAKE_IND));
	wowl_wake_ind = D11SHM_RD(sdh, M_WAKEEVENT_IND);

	DHD_ERROR(("%s: wowl_wake_ind: 0x%08x, ulp_wake_ind: 0x%08x\n",
			__func__, wowl_wake_ind, ulp_wake_ind));

	if (wowl_wake_ind || ulp_wake_ind) {
		/* Don't do anything. Just bail out and re-download firmware. */
	}

	/* CASE 2: Wake up becuase of TX data or ctrl */
	else if ((dhd_ulp->dhd_ulp_path == DHD_ULP_TX_DATA) ||
			(dhd_ulp->dhd_ulp_path == DHD_ULP_TX_CTRL)) {

		dhd_ulp->dhd_ulp_path = DHD_ULP_NO_PATH;

		DHD_ERROR(("%s: [%s]\n", __func__, (dhd_ulp->dhd_ulp_path == DHD_ULP_TX_DATA)?
				"Tx data path":"Tx ctrl path"));

		D11SHM_WR(sdh, M_DS1_CTRL_SDIO, M_DS1_CTRL_SDIO_DS1_EXIT | M_DS1_CTRL_REQ_VALID);

		SET_D11HOSTWAKE(sdh, val32);

		dhd_timeout_start(&tmo, DHD_ULP_HUDI_PROC_DONE_TIME * 1000);

		/* Poll for PROC_DONE to be set by ucode */
		while (!(D11SHM_RD(sdh, M_DS1_CTRL_SDIO) & M_DS1_CTRL_PROC_DONE) &&
				!dhd_timeout_expired(&tmo));

		DHD_ERROR(("%s: %d: M_DS1_CTRL_SDIO: 0x%08x\n", __func__, __LINE__,
				(uint32)D11SHM_RD(sdh, M_DS1_CTRL_SDIO)));

		if (!(D11SHM_RD(sdh, M_DS1_CTRL_SDIO) & M_DS1_CTRL_PROC_DONE)) {
			DHD_ERROR(("%s: Failed to enter DS1 Exit state!\n", __func__));
			return FALSE;
		}

		DHD_ERROR(("%s: wowl_wake_ind: 0x%08x, ulp_wake_ind: 0x%08x\n", __func__,
				D11SHM_RD(sdh, M_WAKEEVENT_IND),
				D11SHM_RD(sdh, (drvr_ucode_if_ptr * 2 + M_ULP_WAKE_IND))));
	}

	/* CASE 3: Wake up becuase of HUDI debug */
	/* For example,
	 *     - Reading some counters present in D11 SHM when the chip is in ULP.
	 *     - Reading any register on backplane when the chip is in DS1.
	 */
	else {
		D11SHM_WR(sdh, M_DS1_CTRL_SDIO, M_DS1_CTRL_SDIO_MAC_ON | M_DS1_CTRL_REQ_VALID);

		SET_D11HOSTWAKE(sdh, val32);

		dhd_timeout_start(&tmo, DHD_ULP_HUDI_PROC_DONE_TIME * 1000);

		/* Poll for PROC_DONE to be set by ucode */
		while (!(D11SHM_RD(sdh, M_DS1_CTRL_SDIO) & M_DS1_CTRL_PROC_DONE) &&
				!dhd_timeout_expired(&tmo));

		if (!(D11SHM_RD(sdh, M_DS1_CTRL_SDIO) & M_DS1_CTRL_PROC_DONE)) {
			DHD_ERROR(("%s: Failed to enter MAC ON state!\n", __func__));
		} else {
			CLR_D11HOSTWAKE(sdh, val32);
		}

		/* We need to return false, as we dont want to redownload the firmware
		 * for DS1 debug mode using HUDI.
		 */
		return FALSE;
	}
#endif /* DHD_HUDI */

	/* Set min res mask depending on chip */
	if (!dhd_bus_set_default_min_res_mask(dhdp->bus))
		return FALSE;

	return TRUE;
}

int
dhd_ulp_f2_ready(dhd_pub_t *dhdp, bcmsdh_info_t *sdh)
{
	int ret = -1;
	dhd_ulp_t *dhd_ulp;
	int iordy_status = 0;

	DHD_NULL_CHK_AND_RET_VAL(dhdp, ret);

	dhd_ulp = dhdp->dhd_ulp;
	DHD_NULL_CHK_AND_RET_VAL(dhd_ulp, ret);

	/* Read the status of IOR2 */
	iordy_status = bcmsdh_cfg_read(sdh, SDIO_FUNC_0, SDIOD_CCCR_IORDY, NULL);

	ret = iordy_status & SDIO_FUNC_ENABLE_2;

	return ret;

}

void
dhd_ulp_set_path(dhd_pub_t *dhdp, int path)
{
	dhd_ulp_t *dhd_ulp;

	DHD_NULL_CHK_AND_RET(dhdp);

	dhd_ulp = dhdp->dhd_ulp;
	DHD_NULL_CHK_AND_RET(dhd_ulp);

	dhd_ulp->dhd_ulp_path = path;
}

/*
 * Function to check DHD ULP feature is requested by the user application.
 */
int
dhd_ulp_check_ulp_request(dhd_pub_t *dhdp, void *buf)
{
	bool val = FALSE;
	int ret = 0;

	DHD_NULL_CHK_AND_RET_VAL(dhdp, -EINVAL);

	if (buf && (!strncmp(buf, "wowl_force", sizeof("wowl_force")) ||
		!strncmp(buf, "ulp", sizeof("ulp")))) {

		if (!strncmp(buf, "wowl_force", sizeof("wowl_force"))) {
			val = *((char*)buf + sizeof("wowl_force"));
		} else if (!strncmp(buf, "ulp", sizeof("ulp"))) {
			val = *((char*)buf + sizeof("ulp"));
		}
		/* Flush and disable console messages */
		dhd_bus_ulp_disable_console(dhdp);

		/* Read the wowl_force argument to decide enable/disable of ULP */
		if (val == TRUE) {
			dhd_ulp_set_ulp_state(dhdp, DHD_ULP_READY);
		} else if (val == FALSE) {
			dhd_ulp_set_ulp_state(dhdp, DHD_ULP_DISABLED);
		} else {
			DHD_ERROR(("%s: Invalid argument\n", __FUNCTION__));
			ret = -EINVAL;
		}
	}

	return ret;
}

/*
 * Function to initialize DHD ULP structure and semaphore.
 */
bool
dhd_ulp_init(osl_t *osh, dhd_pub_t *dhdp)
{
	dhd_ulp_t *dhd_ulp = NULL;
	bool ret = TRUE;

	/* Allocate memory for DHD ULP structure */
	dhd_ulp = MALLOC(osh, sizeof(dhd_ulp_t));
	if (dhd_ulp == NULL) {
		DHD_ERROR(("%s: MALLOC of dhd_ulp_t failed\n", __FUNCTION__));
		ret = FALSE;
	} else {
		bzero(dhd_ulp, sizeof(dhd_ulp_t));
		dhdp->dhd_ulp = dhd_ulp;
	}

	return ret;
}

/*
 * Function to reset lhl registers.
 */
void
dhd_ulp_reset_lhl_regs(dhd_pub_t *dhdp)
{
	si_t *sih = dhd_bus_sih(dhdp->bus);

	LHL_REG(sih, lhl_top_pwrseq_ctl_adr, ~0, 0);	/* LHL Top Level Power Sequence Control */

	LHL_REG(sih, gpio_int_en_port_adr[0], ~0, 0);	/* GPIO Interrupt Enable0 */
	LHL_REG(sih, gpio_int_st_port_adr[0], ~0, ~0);	/* GPIO Interrupt Status0 */

	LHL_REG(sih, lhl_wl_armtim0_intrp_adr, ~0, 0);	/* WL ARM Timer0 Interrupt Mask */
	LHL_REG(sih, lhl_wl_armtim0_st_adr, ~0, ~0);	/* WL ARM Timer0 Interrupt Status */
	LHL_REG(sih, lhl_wl_armtim0_adr, ~0, 0);	/* WL ARM Timer */

	LHL_REG(sih, lhl_wl_mactim0_intrp_adr, ~0, 0);	/* WL MAC Timer0 Interrupt Mask */
	LHL_REG(sih, lhl_wl_mactim0_st_adr, ~0, ~0);	/* WL MAC Timer0 Interrupt Status */
	LHL_REG(sih, lhl_wl_mactim_int0_adr, ~0, 0);	/* WL MAC TimerInt0 */

	dhd_bus_set_default_min_res_mask(dhdp->bus);
}

/*
 * Function to deinit DHD ULP structure and semaphore.
 */
void
dhd_ulp_deinit(osl_t *osh, dhd_pub_t *dhdp)
{
	dhd_ulp_t *dhd_ulp;

	DHD_NULL_CHK_AND_RET(dhdp);

	dhd_ulp = dhdp->dhd_ulp;
	DHD_NULL_CHK_AND_RET(dhd_ulp);

	dhd_ulp_reset_lhl_regs(dhdp);
	dhd_bus_pmu_reg_reset(dhdp);

	/* De allocate the DHD ULP memory */
	MFREE(osh, dhd_ulp, sizeof(dhd_ulp_t));
	dhdp->dhd_ulp = NULL;
}

#ifdef DHD_DEBUG
/*
 * Functions to save and restore the console print interval.
 * These are used before entering ULP(save) and after redownload(restore)
 */
void
dhd_ulp_save_console_interval(dhd_pub_t *dhdp)
{
	dhd_ulp_t *dhd_ulp = dhdp->dhd_ulp;

	DHD_NULL_CHK_AND_RET(dhdp);

	dhd_ulp = dhdp->dhd_ulp;
	DHD_NULL_CHK_AND_RET(dhd_ulp);

	dhd_ulp->dhd_ulp_console_count = dhd_console_ms;
}

void
dhd_ulp_restore_console_interval(dhd_pub_t *dhdp)
{
	dhd_ulp_t *dhd_ulp;

	DHD_NULL_CHK_AND_RET(dhdp);

	dhd_ulp = dhdp->dhd_ulp;
	DHD_NULL_CHK_AND_RET(dhd_ulp);

	dhd_console_ms = dhd_ulp->dhd_ulp_console_count;
}
#endif /* DHD_DEBUG */
/*
 * Functions to enable/disable sbwad value caching.
 */
void
dhd_ulp_disable_cached_sbwad(dhd_pub_t *dhdp)
{
	dhd_ulp_t *dhd_ulp;

	DHD_NULL_CHK_AND_RET(dhdp);

	dhd_ulp = dhdp->dhd_ulp;
	DHD_NULL_CHK_AND_RET(dhd_ulp);

	dhd_ulp->dhd_ulp_state = DHD_ULP_TRIGGERED;
}

void
dhd_ulp_enable_cached_sbwad(dhd_pub_t *dhdp, bcmsdh_info_t *sdh)
{
	dhd_ulp_t *dhd_ulp;

	DHD_NULL_CHK_AND_RET(dhdp);

	dhd_ulp = dhdp->dhd_ulp;
	DHD_NULL_CHK_AND_RET(dhd_ulp);

	if (dhd_ulp->dhd_ulp_state == DHD_ULP_TRIGGERED) {
		bcmsdh_force_sbwad_calc(sdh, TRUE);
		dhd_ulp->dhd_ulp_state = DHD_ULP_IDLE;
	}
}

#endif /* DHD_ULP */
