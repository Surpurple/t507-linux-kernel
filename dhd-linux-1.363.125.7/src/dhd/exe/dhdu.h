/*
 * Definitions for DHD command-line utility
 *
 * $ Copyright Open Broadcom Corporation $
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id: dhdu.h 528396 2015-01-22 06:24:24Z $
 */

#ifndef _dhdu_h_
#define _dhdu_h_

#include "dhdu_cmd.h"

extern char *dhdu_av0;

/* parse common option */
extern int dhd_option(char ***pargv, char **pifname, int *phelp);
extern void dhd_cmd_init(void);

/* print usage */
extern void dhd_cmd_usage(cmd_t *cmd);
extern void dhd_usage(cmd_t *port_cmds);
extern void dhd_cmds_usage(cmd_t *port_cmds);

/* print helpers */
extern void dhd_printlasterror(void *dhd);
extern void dhd_printint(int val);

/* check driver version */
extern int dhd_check(void *dhd);

/* gets primary and virtual bss id from if_name */
extern int get_bsscfg_idx(void *dhd, int *bsscfg_idx);
#define GET_BSSCFG_IDX(dhd, bsscfg_idx)		get_bsscfg_idx(dhd, bsscfg_idx)

/* utility functions */
struct ipv4_addr;
int dhd_ether_atoe(const char *a, struct ether_addr *n);
int dhd_atoip(const char *a, struct ipv4_addr *n);

/* integer output format */
#define INT_FMT_DEC	0	/* signed integer */
#define INT_FMT_UINT	1	/* unsigned integer */
#define INT_FMT_HEX	2	/* hexdecimal */

/* command line argument usage */
#define CMD_ERR		-1	/* Error for command */
#define CMD_OPT		0	/* a command line option */
#define CMD_DHD		1	/* the start of a dhd command */

#endif /* _dhdu_h_ */
