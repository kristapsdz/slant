#include <sys/poll.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "slant.h"

void
draw(const struct node *n, size_t nsz)
{
	size_t	 i, sz, maxsz;

	maxsz = strlen("hostname");

	for (i = 0; i < nsz; i++) {
		sz = strlen(n[i].host);
		if (sz > maxsz)
			maxsz = sz;
	}

	printf("%*s | %6s\n", (int)maxsz, 
		"hostname", "CPU");

	for (i = 0; i < nsz; i++) {
		printf("%*s | ", (int)maxsz, n[i].host);
		if (NULL == n[i].recs) {
			printf("(no data)\n");
			continue;
		} else if (0 == n[i].recs->byqminsz) {
			printf("(no recent data)\n");
			continue;
		}
		printf("%5.1f%%\n", 
			n[i].recs->byqmin[0].value /
			n[i].recs->byqmin[0].entries);
	}
}
