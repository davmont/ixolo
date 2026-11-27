#ifndef MINIX_NET_LWIP_NPFDEV_H
#define MINIX_NET_LWIP_NPFDEV_H

/*
 * /dev/npf shares its major number with /dev/bpf; the minor numbers from
 * NPFDEV_MINOR up are its own (see cdev.c).  Opening /dev/npf yields a cloned
 * device with a minor number from NPFDEV_BASE_MINOR up.
 */
#define NPFDEV_MINOR		128	/* minor number of /dev/npf */
#define NPFDEV_BASE_MINOR	129	/* base minor number for clones */

extern const struct chardriver npfdev_tab;

#endif /* !MINIX_NET_LWIP_NPFDEV_H */
