/* $OpenBSD: netcat.c,v 1.82 2005/07/24 09:33:56 marius Exp $ */
/*
 * Copyright (c) 2001 Eric Jackson <ericj@monkey.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *   derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Re-written nc(1) for OpenBSD. Original implementation by
 * *Hobbit* <hobbit@avian.org>.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/telnet.h>

#include <err.h>
#include <errno.h>
#ifdef __APPLE__
#include <limits.h>
#endif /* __APPLE__ */
#include <netdb.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include "atomicio.h"

#ifndef SUN_LEN
#define SUN_LEN(su) \
	(sizeof(*(su)) - sizeof((su)->sun_path) + strlen((su)->sun_path))
#endif

#define PORT_MAX	65535
#define PORT_MAX_LEN	6

/* Command Line Options */
int	dflag;					/* detached, no stdin */
int	iflag;					/* Interval Flag */
int	kflag;					/* More than one connect */
int	lflag;					/* Bind to local port */

int timeout = -1;
int family = AF_UNSPEC;
char *portlist[PORT_MAX+1];

void	help(void);
int	local_listen(char *, char *, struct addrinfo);
void	readwrite(int);
void	usage(int);

int
main(int argc, char *argv[])
{
	int ch, s, ret, socksv;
	char *host, *uport, *endp;
	struct addrinfo hints;
	struct servent *sv;
	socklen_t len;
	struct sockaddr_storage cliaddr;
//	char *proxy;
//	const char *proxyhost = "", *proxyport = NULL;
//	struct addrinfo proxyhints;

	ret = 1;
	s = 0;
	socksv = 5;
	host = NULL;
	uport = NULL;
	endp = NULL;
	sv = NULL;

	while ((ch = getopt(argc, argv,
	    "l")) != -1) {
		switch (ch) {
		case 'l':
			lflag = 1;
			break;
		default:
			usage(1);
		}
	}
	argc -= optind;
	argv += optind;

	/* Cruft to make sure options are clean, and used properly. */
	if (argv[0] && !argv[1] && family == AF_UNIX) {
		host = argv[0];
		uport = NULL;
	} else if (argv[0] && !argv[1]) {
		if  (!lflag)
			usage(1);
		uport = argv[0];
		host = NULL;
	} else if (argv[0] && argv[1]) {
		host = argv[0];
		uport = argv[1];
	} else
		usage(1);

	/* Initialize addrinfo structure. */
	if (family != AF_UNIX) {
		memset(&hints, 0, sizeof(struct addrinfo));
		hints.ai_family = family;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_protocol = IPPROTO_TCP;

		//if (nflag)
			//hints.ai_flags |= AI_NUMERICHOST;
	}

	if (lflag) {
		//https://www.cl.cam.ac.uk/cgi-bin/manpage?2+accept
		int connfd;//connection file descriptor
		ret = 0;
		/* Allow only one connection at a time, but stay alive. */
		for (;;) {
			if (family != AF_UNIX)
				s = local_listen(host, uport, hints);
			if (s < 0)
				err(1, NULL);

			len = sizeof(cliaddr);
			connfd = accept(s, (struct sockaddr *)&cliaddr,
			    &len);

			readwrite(connfd);
			close(connfd);
			if (family != AF_UNIX)
				close(s);

			if (!kflag)
				break;
		}
	}

	if (s)
		close(s);

	exit(ret);
}



/*
 * local_listen()
 * Returns a socket listening on a local port, binds to specified source
 * address. Returns -1 on failure.
 */
int
local_listen(char *host, char *port, struct addrinfo hints)
{
	struct addrinfo *res, *res0;
	int s, ret, x = 1;
	int error;

	/* Allow nodename to be null. */
	hints.ai_flags |= AI_PASSIVE;

	/*
	 * In the case of binding to a wildcard address
	 * default to binding to an ipv4 address.
	 */
	if (host == NULL && hints.ai_family == AF_UNSPEC)
		hints.ai_family = AF_INET;

	if ((error = getaddrinfo(host, port, &hints, &res)))
		errx(1, "getaddrinfo: %s", gai_strerror(error));

	res0 = res;
	do {
		if ((s = socket(res0->ai_family, res0->ai_socktype,
		    res0->ai_protocol)) < 0)
			continue;

		ret = setsockopt(s, SOL_SOCKET, SO_REUSEPORT, &x, sizeof(x));
		if (ret == -1)
			err(1, NULL);

		if (bind(s, (struct sockaddr *)res0->ai_addr,
		    res0->ai_addrlen) == 0)
			break;

		close(s);
		s = -1;
	} while ((res0 = res0->ai_next) != NULL);

	if (s != -1) {
		if (listen(s, 1) < 0)
			err(1, "listen");
	}

	freeaddrinfo(res);

	return (s);
}

/*
 * readwrite()
 * Loop that polls on the network file descriptor and stdin.
 */
void
readwrite(int nfd)
{
	struct pollfd pfd[2];
	unsigned char buf[8192];
	int n, wfd = fileno(stdin);
	int lfd = fileno(stdout);
	int plen;

	plen = 1024;

	/* Setup Network FD */
	pfd[0].fd = nfd;
	pfd[0].events = POLLIN;

	/* Set up STDIN FD. */
	pfd[1].fd = wfd;
	pfd[1].events = POLLIN;

	while (pfd[0].fd != -1) {
		if (iflag)
			sleep(iflag);
		if ((n = poll(pfd, 2 - dflag, timeout)) < 0) {
			close(nfd);
			err(1, "Polling Error");
		}

		if (n == 0)
			return;

		if (pfd[0].revents & POLLIN) {
			if ((n = read(nfd, buf, plen)) < 0)
				return;
			else if (n == 0) {
				shutdown(nfd, SHUT_RD);
				pfd[0].fd = -1;
				pfd[0].events = 0;
			} else {
				if (atomicio(vwrite, lfd, buf, n) != n)
					return;
			}
		}

		if (!dflag && pfd[1].revents & POLLIN) {
			if ((n = read(wfd, buf, plen)) < 0)
				return;
			else if (n == 0) {
				shutdown(nfd, SHUT_WR);
				pfd[1].fd = -1;
				pfd[1].events = 0;
			} else {
				if (atomicio(vwrite, nfd, buf, n) != n)
					return;
			}
		}
	}
}




void
help(void)
{
	usage(0);
	exit(1);
}

void
usage(int ret)
{
	if (ret)
		exit(1);
}