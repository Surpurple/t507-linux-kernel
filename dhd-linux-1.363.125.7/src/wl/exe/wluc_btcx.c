/*
 * wl btcx command module
 *
 * Copyright (C) 2017, Broadcom Corporation
 * All Rights Reserved.
 * 
 * This is UNPUBLISHED PROPRIETARY SOURCE CODE of Broadcom Corporation;
 * the contents of this file may not be disclosed to third parties, copied
 * or duplicated in any form, in whole or in part, without the prior
 * written permission of Broadcom Corporation.
 *
 *
 * <<Broadcom-WL-IPTag/Proprietary:>>
 *
 * $Id: wluc_btcx.c 458728 2014-02-27 18:15:25Z $
 */

#ifdef WIN32
#include <windows.h>
#endif

#include <wlioctl.h>


#ifdef CUSTOMER_HW_31_2
#include <wluc_horizon.h>
#endif /* CUSTOMER_HW_31_2 */

/* Because IL_BIGENDIAN was removed there are few warnings that need
 * to be fixed. Windows was not compiled earlier with IL_BIGENDIAN.
 * Hence these warnings were not seen earlier.
 * For now ignore the following warnings
 */
#ifdef WIN32
#pragma warning(push)
#pragma warning(disable : 4244)
#pragma warning(disable : 4761)
#endif

#include <bcmutils.h>
#include <bcmendian.h>
#include "wlu_common.h"
#include "wlu.h"

static cmd_t wl_btcx_cmds[] = {
	{ "btc_params", wlu_reg2args, WLC_GET_VAR, WLC_SET_VAR, "g/set BT Coex parameters"},
	{ "btc_flags", wlu_reg2args, WLC_GET_VAR, WLC_SET_VAR, "g/set BT Coex flags"},
	{ NULL, NULL, 0, 0, NULL }
};

static char *buf;

/* module initialization */
void
wluc_btcx_module_init(void)
{
	(void)g_swap;

	/* get the global buf */
	buf = wl_get_buf();

	/* register btcx commands */
	wl_module_cmds_register(wl_btcx_cmds);
}
