/*
 * Description.
 *
 * $ Copyright Open Broadcom Corporation $
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id: miniopt.c 584872 2015-09-08 23:11:57Z $
 */

/* ---- Include Files ---------------------------------------------------- */

#include <typedefs.h>
#include <stdio.h>
#ifndef CUSTOMER_HW_31_2
#include <stdlib.h>
#endif
#include <string.h>
#include <miniopt.h>

#ifdef CUSTOMER_HW_31_2

/*
 * -1- Including stdlib.h for HORIZON platform is verboten:
 * entities declared therein are _stubbed_, hence useless.
 * Sigh.
 *
 * -2- Using our own bcm_xxx facilities instead requires bcmutils.h.
 * Including bcmutils.h generates strange compiler error about mismatched
 * prototypes of miniopt().
 * But, not including it means we get complaints about implicit decls
 * for bcm_strtoul.
 * Double Sigh.

 * -3- Including wluc_horizon.h causes similar trouble.
 * This is the same file we use for all other user level HORIZON files.
 * Triple Sigh.
 *
 * -4- Hence we define/declare what we need here along with prototype.
 *
 *
 */
extern ulong bcm_strtoul(const char *cp, char **endp, uint base);
#define strtoul(nptr, endptr, base)	bcm_strtoul((nptr), (endptr), (base))
#define strtol(nptr, endptr, base)	bcm_strtoul((nptr), (endptr), (base))

#endif /* CUSTOMER_HW_31_2 */


/* ---- Public Variables ------------------------------------------------- */
/* ---- Private Constants and Types -------------------------------------- */



/* ---- Private Variables ------------------------------------------------ */
/* ---- Private Function Prototypes -------------------------------------- */
/* ---- Functions -------------------------------------------------------- */

/* ----------------------------------------------------------------------- */
void
miniopt_init(miniopt_t *t, const char* name, const char* flags, bool longflags)
{
	static const char *null_flags = "";

	memset(t, 0, sizeof(miniopt_t));
	t->name = name;
	if (flags == NULL)
		t->flags = null_flags;
	else
		t->flags = flags;
	t->longflags = longflags;
}


/* ----------------------------------------------------------------------- */
int
miniopt(miniopt_t *t, char **argv)
{
	int keylen;
	char *p, *eq, *valstr, *endptr = NULL;
	int err = 0;

	t->consumed = 0;
	t->positional = FALSE;
	memset(t->key, 0, MINIOPT_MAXKEY);
	t->opt = '\0';
	t->valstr = NULL;
	t->good_int = FALSE;
	valstr = NULL;

	if (*argv == NULL) {
		err = -1;
		goto exit;
	}

	p = *argv++;
	t->consumed++;

	if (!t->opt_end && !strcmp(p, "--")) {
		t->opt_end = TRUE;
		if (*argv == NULL) {
			err = -1;
			goto exit;
		}
		p = *argv++;
		t->consumed++;
	}

	if (t->opt_end) {
		t->positional = TRUE;
		valstr = p;
	}
	else if (!strncmp(p, "--", 2)) {
		eq = strchr(p, '=');
		if (eq == NULL && !t->longflags) {
			fprintf(stderr,
				"%s: missing \" = \" in long param \"%s\"\n", t->name, p);
			err = 1;
			goto exit;
		}
		keylen = eq ? (int)(eq - (p + 2)) : (int)strlen(p) - 2;
		if (keylen > 63) keylen = 63;
		memcpy(t->key, p + 2, keylen);

		if (eq) {
			valstr = eq + 1;
			if (*valstr == '\0') {
				fprintf(stderr,
				        "%s: missing value after \" = \" in long param \"%s\"\n",
				        t->name, p);
				err = 1;
				goto exit;
			}
		}
	}
	else if (!strncmp(p, "-", 1)) {
		t->opt = p[1];
		if (strlen(p) > 2) {
			fprintf(stderr,
				"%s: only single char options, error on param \"%s\"\n",
				t->name, p);
			err = 1;
			goto exit;
		}
		if (strchr(t->flags, t->opt)) {
			/* this is a flag option, no value expected */
			valstr = NULL;
		} else {
			if (*argv == NULL) {
				fprintf(stderr,
				"%s: missing value parameter after \"%s\"\n", t->name, p);
				err = 1;
				goto exit;
			}
			valstr = *argv;
			argv++;
			t->consumed++;
		}
	} else {
		t->positional = TRUE;
		valstr = p;
	}

	/* parse valstr as int just in case */
	if (valstr) {
		t->uval = (uint)strtoul(valstr, &endptr, 0);
		t->val = (int)t->uval;
		t->good_int = (*endptr == '\0');
	}

	t->valstr = valstr;

exit:
	if (err == 1)
		t->opt = '?';

	return err;
}
