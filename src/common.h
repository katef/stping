/*
 * Routines common to dgping and dgpingd.
 */

#ifndef DG_COMMON_H
#define DG_COMMON_H

#include <stdint.h>

/*
 * Format a string to the given sequence number. A pointer to a static
 * string is returned.
 */
const char *
mkping(uint16_t seq);

/*
 * Validate an expected checksum. Return true on success.
 */
int
validate(const char *in, uint16_t *seq);

/*
 * Parse out strings giving a PF port and address to the given sockaddr struct;
 * a newly-created socket is returned, or -1 on error.
 */
int
getaddr(const char *addr, const char *port, struct sockaddr_in *sin);

#endif

