/*
 * Definitions for DHD nl80211 driver interface.
 *
 * $ Copyright Open Broadcom Corporation $
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id: $
 */

#ifndef DHDU_NL80211_H_
#define DHDU_NL80211_H_

#ifdef NL80211

#include <netlink/genl/genl.h>
#include <netlink/genl/family.h>
#include <netlink/genl/ctrl.h>

/* libnl 1.x compatibility code */
#if !defined(CONFIG_LIBNL20) && !defined(CONFIG_LIBNL30)
#define nl_sock		nl_handle
#endif

struct dhd_netlink_info
{
	struct nl_sock *nl;
	struct nl_cb *cb;
	int nl_id;
	int ifidx;
};

int dhd_nl_sock_connect(struct dhd_netlink_info *dhd_nli);
void dhd_nl_sock_disconnect(struct dhd_netlink_info *dhd_nli);
int dhd_nl_do_vndr_cmd(struct dhd_netlink_info *dhd_nli, dhd_ioctl_t *ioc);

#endif /* NL80211 */

#endif /* DHDU_NL80211_H_ */
