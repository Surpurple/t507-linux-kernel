/*
 * Command structure for dhd command line utility, copied from wl utility
 *
 * $ Copyright Open Broadcom Corporation $
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id: dhdu_cmd.h 514727 2014-11-12 03:02:48Z $
 */

#ifndef _dhdu_cmd_h_
#define _dhdu_cmd_h_

typedef struct cmd cmd_t;
typedef int (cmd_func_t)(void *dhd, cmd_t *cmd, char **argv);

/* generic command line argument handler */
struct cmd {
	char *name;
	cmd_func_t *func;
	int get;
	int set;
	char *help;
};

/* list of command line arguments */
extern cmd_t dhd_cmds[];
extern cmd_t dhd_varcmd;

/* Special set cmds to do download via dev node interface if present */
#define	DHD_DLDN_ST		0x400
#define	DHD_DLDN_WRITE		(DHD_DLDN_ST + 1)
#define	DHD_DLDN_END		(DHD_DLDN_ST + 2)

/* per-port ioctl handlers */
extern int dhd_get(void *dhd, int cmd, void *buf, int len);
extern int dhd_set(void *dhd, int cmd, void *buf, int len);

#endif /* _dhdu_cmd_h_ */
