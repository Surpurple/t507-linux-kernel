/*
 * wl sdio command module
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
 * $Id: wluc_sdio.c 458728 2014-02-27 18:15:25Z $
 */
#ifdef WIN32
#include <windows.h>
#endif

#include <wlioctl.h>


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

#include <sdiovar.h>

static cmd_func_t wl_sd_reg, wl_sd_msglevel, wl_sd_blocksize, wl_sd_mode;

static cmd_t wl_sdio_cmds[] = {
	{ "sd_cis", wl_var_getandprintstr, WLC_GET_VAR, -1,
	"dump sdio CIS"},
	{ "sd_devreg", wl_sd_reg, WLC_GET_VAR, WLC_SET_VAR,
	"g/set device register across SDIO bus"},
	{ "sd_drivestrength", wl_varint, WLC_GET_VAR, WLC_SET_VAR,
	"g/set SDIO bus drive strenth in mA"},
	{ "sd_hostreg", wl_sd_reg, WLC_GET_VAR, WLC_SET_VAR,
	"g/set local controller register"},
	{ "sd_blockmode", wl_varint, WLC_GET_VAR, WLC_SET_VAR,
	"g/set blockmode"},
	{ "sd_blocksize", wl_sd_blocksize, WLC_GET_VAR, WLC_SET_VAR,
	"g/set block size for a function"},
	{ "sd_ints", wl_varint, WLC_GET_VAR, WLC_SET_VAR,
	"g/set client ints"},
	{ "sd_dma", wl_varint, WLC_GET_VAR, WLC_SET_VAR,
	"g/set dma usage"},
	{ "sd_numints", wl_varint, WLC_GET_VAR, -1,
	"number of device interrupts"},
	{ "sd_numlocalints", wl_varint, WLC_GET_VAR, -1,
	"number of non-device controller interrupts"},
	{ "sd_divisor", wl_varint, WLC_GET_VAR, WLC_SET_VAR,
	"set the divisor for SDIO clock generation"},
	{ "sd_mode", wl_sd_mode, WLC_GET_VAR, WLC_SET_VAR,
	"g/set SDIO bus mode (spi, sd1, sd4)"},
	{ "sd_highspeed", wl_varint, WLC_GET_VAR, WLC_SET_VAR,
	"set the high-speed clocking mode"},
	{ "sd_msglevel", wl_sd_msglevel, WLC_GET_VAR, WLC_SET_VAR,
	"g/set debug message level"},
	{ NULL, NULL, 0, 0, NULL }
};

static char *buf;

/* module initialization */
void
wluc_sdio_module_init(void)
{
	/* get the global buf */
	buf = wl_get_buf();

	/* register sdio commands */
	wl_module_cmds_register(wl_sdio_cmds);
}


static dbg_msg_t wl_sd_msgs[] = {
	{SDH_ERROR_VAL,	"error"},
	{SDH_TRACE_VAL,	"trace"},
	{SDH_INFO_VAL,	"info"},
	{SDH_DATA_VAL,	"data"},
	{SDH_CTRL_VAL,	"control"}
};

static int
wl_sd_msglevel(void *wl, cmd_t *cmd, char **argv)
{
	int ret, i;
	uint val, last_val = 0, msglevel_add = 0, msglevel_del = 0;
	char *endptr = NULL;
	int msglevel;
	dbg_msg_t *dbg_msg = wl_sd_msgs;

	if ((ret = wlu_iovar_getint(wl, cmd->name, &msglevel)) < 0)
		return (ret);

	if (!*++argv) {
		printf("0x%x ", msglevel);
		for (i = 0; (val = dbg_msg[i].value); i++) {
			if ((msglevel & val) && (val != last_val))
				printf(" %s", dbg_msg[i].string);
			last_val = val;
		}
		printf("\n");
		return (0);
	}

	while (*argv) {
		char *s = *argv;
		if (*s == '+' || *s == '-')
			s++;
		else
			msglevel_del = ~0;	/* make the whole list absolute */
		val = strtoul(s, &endptr, 0);
		/* not a plain integer if not all the string was parsed by strtoul */
		if (*endptr != '\0') {
			for (i = 0; (val = dbg_msg[i].value); i++)
				if (stricmp(dbg_msg[i].string, s) == 0)
					break;
			if (!val)
				goto usage;
		}
		if (**argv == '-')
			msglevel_del |= val;
		else
			msglevel_add |= val;
		++argv;
	}

	msglevel &= ~msglevel_del;
	msglevel |= msglevel_add;

	return (wlu_iovar_setint(wl, cmd->name, msglevel));

usage:
	fprintf(stderr, "msg values may be a list of numbers or names from the following set.\n");
	fprintf(stderr, "Use a + or - prefix to make an incremental change.");

	for (i = 0; (val = dbg_msg[i].value); i++) {
		if (val != last_val)
			fprintf(stderr, "\n0x%04x %s", val, dbg_msg[i].string);
		else
			fprintf(stderr, ", %s", dbg_msg[i].string);
		last_val = val;
	}
	fprintf(stderr, "\n");

	return 0;
}

static int
wl_sd_blocksize(void *wl, cmd_t *cmd, char **argv)
{
	int ret;
	int argc;
	char *endptr = NULL;
	void *ptr = NULL;
	int func, size;

	/* arg count */
	for (argc = 0; argv[argc]; argc++);
	argc--;

	if (argc < 1 || argc > 2) {
		printf("required args: function [size] (size 0 means max)\n");
		return BCME_USAGE_ERROR;
	}

	func = strtol(argv[1], &endptr, 0);
	if (*endptr != '\0') {
		printf("Invaild function: %s\n", argv[1]);
		return BCME_USAGE_ERROR;
	}

	if (argc > 1) {
		size = strtol(argv[2], &endptr, 0);
		if (*endptr != '\0') {
			printf("Invalid size: %s\n", argv[1]);
			return BCME_USAGE_ERROR;
		}
	}

	if (argc == 1) {
		func = htod32(func);
		if ((ret = wlu_var_getbuf(wl, cmd->name, &func, sizeof(func), &ptr)) >= 0)
			printf("Function %d block size: %d\n", func, dtoh32(*(int*)ptr));
	} else {
		printf("Setting function %d block size to %d\n", func, size);
		size &= 0x0000ffff; size |= (func << 16);
		size = htod32(size);
		ret = wlu_var_setbuf(wl, cmd->name, &size, sizeof(size));
	}

	return (ret);
}

static int
wl_sd_mode(void *wl, cmd_t *cmd, char **argv)
{
	int ret;
	int argc;
	int sdmode;

	/* arg count */
	for (argc = 0; argv[argc]; argc++);
	argc--;

	if (argv[1]) {
		if (!strcmp(argv[1], "spi")) {
			strcpy(argv[1], "0");
		} else if (!strcmp(argv[1], "sd1")) {
			strcpy(argv[1], "1");
		} else if (!strcmp(argv[1], "sd4")) {
			strcpy(argv[1], "2");
		} else {
			return BCME_USAGE_ERROR;
		}

		ret = wl_var_setint(wl, cmd, argv);

	} else {
		if ((ret = wl_var_get(wl, cmd, argv))) {
			return (ret);
		} else {
			sdmode = dtoh32(*(int32*)buf);

			printf("SD Mode is: %s\n",
			       sdmode == 0 ? "SPI"
			       : sdmode == 1 ? "SD1"
				   : sdmode == 2 ? "SD4" : "Unknown");
		}
	}

	return (ret);
}

static int
wl_sd_reg(void *wl, cmd_t *cmd, char **argv)
{
	int ret;
	sdreg_t sdreg;
	char *endptr = NULL;
	uint argc;
	void *ptr = NULL;

	memset(&sdreg, 0, sizeof(sdreg));

	/* arg count */
	for (argc = 0; argv[argc]; argc++);
	argc--;

	/* hostreg: offset [value]; devreg: func offset [value] */
	if (!strcmp(cmd->name, "sd_hostreg")) {
		argv++;
		if (argc < 1) {
			printf("required args: offset [value]\n");
			return BCME_USAGE_ERROR;
		}

	} else if (!strcmp(cmd->name, "sd_devreg")) {
		argv++;
		if (argc < 2) {
			printf("required args: func offset [value]\n");
			return BCME_USAGE_ERROR;
		}

		sdreg.func = htod32(strtol(*argv++, &endptr, 0));
		if (*endptr != '\0') {
			printf("Bad func.\n");
			return BCME_USAGE_ERROR;
		}
	} else {
		return BCME_USAGE_ERROR;
	}

	sdreg.offset = htod32(strtol(*argv++, &endptr, 0));
	if (*endptr != '\0') {
		printf("Bad offset\n");
		return BCME_USAGE_ERROR;
	}

	/* third arg: value */
	if (*argv) {
		sdreg.value = htod32(strtol(*argv, &endptr, 0));
		if (*endptr != '\0') {
			printf("Bad Value\n");
			return BCME_USAGE_ERROR;
		}
	}

	/* no third arg means get, otherwise set */
	if (!*argv) {
		if ((ret = wlu_var_getbuf(wl, cmd->name, &sdreg, sizeof(sdreg), &ptr)) >= 0)
			printf("0x%x\n", dtoh32(*(int *)ptr));
	} else {
		ret = wlu_var_setbuf(wl, cmd->name, &sdreg, sizeof(sdreg));
	}

	return (ret);
}
