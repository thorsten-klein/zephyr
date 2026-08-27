/*
 * Copyright (c) 2019 Linaro Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(net_test, CONFIG_NET_SOCKETS_LOG_LEVEL);

#include <stdio.h>
#include <zephyr/ztest_assert.h>

#include <zephyr/net/socket.h>
#include <zephyr/sys/fdtable.h>

#include "../../socket_helpers.h"

#define BUF_AND_SIZE(buf) buf, sizeof(buf) - 1
#define STRLEN(buf) (sizeof(buf) - 1)

#define TEST_STR_SMALL "test"

#define MY_IPV6_ADDR "::1"

#define ANY_PORT 0
#define SERVER_PORT 4242
#define CLIENT_PORT 9898

/* On QEMU, poll() which waits takes +10ms from the requested time. */
#define FUZZ 10

#define TCP_TEARDOWN_TIMEOUT K_SECONDS(3)

ZTEST(net_socket_poll, test_poll)
{
	int res;
	int c_sock;
	int s_sock;
	int c_sock_tcp;
	int s_sock_tcp;
	struct net_sockaddr_in6 c_addr;
	struct net_sockaddr_in6 s_addr;
	struct zsock_pollfd pollfds[2];
	struct zsock_pollfd pollout[1];
	uint32_t tstamp;
	ssize_t len;
	char buf[10];

	prepare_sock_udp_v6(MY_IPV6_ADDR, CLIENT_PORT, &c_sock, &c_addr);
	prepare_sock_udp_v6(MY_IPV6_ADDR, SERVER_PORT, &s_sock, &s_addr);
	prepare_sock_tcp_v6(MY_IPV6_ADDR, CLIENT_PORT, &c_sock_tcp, &c_addr);
	prepare_sock_tcp_v6(MY_IPV6_ADDR, SERVER_PORT, &s_sock_tcp, &s_addr);

	res = zsock_bind(s_sock, (struct net_sockaddr *)&s_addr, sizeof(s_addr));
	zassert_equal(res, 0, "bind failed");

	res = zsock_connect(c_sock, (struct net_sockaddr *)&s_addr, sizeof(s_addr));
	zassert_equal(res, 0, "connect failed");

	memset(pollfds, 0, sizeof(pollfds));
	pollfds[0].fd = c_sock;
	pollfds[0].events = ZSOCK_POLLIN;
	pollfds[1].fd = s_sock;
	pollfds[1].events = ZSOCK_POLLIN;

	/* Poll non-ready fd's with timeout of 0 */
	tstamp = k_uptime_get_32();
	res = zsock_poll(pollfds, ARRAY_SIZE(pollfds), 0);
	zassert_true(k_uptime_get_32() - tstamp <= FUZZ, "");
	zassert_equal(res, 0, "");

	zassert_equal(pollfds[0].fd, c_sock, "");
	zassert_equal(pollfds[0].events, ZSOCK_POLLIN, "");
	zassert_equal(pollfds[0].revents, 0, "");
	zassert_equal(pollfds[1].fd, s_sock, "");
	zassert_equal(pollfds[1].events, ZSOCK_POLLIN, "");
	zassert_equal(pollfds[1].revents, 0, "");


	/* Poll non-ready fd's with timeout of 30 */
	tstamp = k_uptime_get_32();
	res = zsock_poll(pollfds, ARRAY_SIZE(pollfds), 30);
	tstamp = k_uptime_get_32() - tstamp;
	zassert_true(tstamp >= 30U && tstamp <= 30 + FUZZ * 2, "tstamp %d",
		     tstamp);
	zassert_equal(res, 0, "");

	/* Send pkt for s_sock and poll with timeout of 10 */
	len = zsock_send(c_sock, BUF_AND_SIZE(TEST_STR_SMALL), 0);
	zassert_equal(len, STRLEN(TEST_STR_SMALL), "invalid send len");

	tstamp = k_uptime_get_32();
	res = zsock_poll(pollfds, ARRAY_SIZE(pollfds), 30);
	tstamp = k_uptime_get_32() - tstamp;
	zassert_true(tstamp <= FUZZ, "");
	zassert_equal(res, 1, "");

	zassert_equal(pollfds[0].fd, c_sock, "");
	zassert_equal(pollfds[0].events, ZSOCK_POLLIN, "");
	zassert_equal(pollfds[0].revents, 0, "");
	zassert_equal(pollfds[1].fd, s_sock, "");
	zassert_equal(pollfds[1].events, ZSOCK_POLLIN, "");
	zassert_equal(pollfds[1].revents, ZSOCK_POLLIN, "");


	/* Recv pkt from s_sock and ensure no poll events happen */
	len = zsock_recv(s_sock, BUF_AND_SIZE(buf), 0);
	zassert_equal(len, STRLEN(TEST_STR_SMALL), "invalid recv len");

	tstamp = k_uptime_get_32();
	res = zsock_poll(pollfds, ARRAY_SIZE(pollfds), 0);
	zassert_true(k_uptime_get_32() - tstamp <= FUZZ, "");
	zassert_equal(res, 0, "");
	zassert_equal(pollfds[1].revents, 0, "");

	/* Make sure that POLLOUT does not wait if not really needed */
	memset(pollout, 0, sizeof(pollout));
	pollout[0].fd = c_sock;
	pollout[0].events = ZSOCK_POLLOUT;

	res = zsock_connect(c_sock, (const struct net_sockaddr *)&s_addr,
			    sizeof(s_addr));
	zassert_equal(res, 0, "");

	tstamp = k_uptime_get_32();
	res = zsock_poll(pollout, ARRAY_SIZE(pollout), 200);
	zassert_true(k_uptime_get_32() - tstamp < 100, "");
	zassert_equal(res, 1, "");
	zassert_equal(pollout[0].revents, ZSOCK_POLLOUT, "");

	/* First test that TCP POLLOUT will not wait if there is enough
	 * room in TCP window
	 */
	memset(pollout, 0, sizeof(pollout));
	pollout[0].fd = c_sock_tcp;
	pollout[0].events = ZSOCK_POLLOUT;

	res = zsock_bind(s_sock_tcp, (struct net_sockaddr *)&s_addr, sizeof(s_addr));
	zassert_equal(res, 0, "");
	res = zsock_listen(s_sock_tcp, 0);
	zassert_equal(res, 0, "");

	res = zsock_connect(c_sock_tcp, (const struct net_sockaddr *)&s_addr,
			    sizeof(s_addr));
	zassert_equal(res, 0, "");

	tstamp = k_uptime_get_32();
	res = zsock_poll(pollout, ARRAY_SIZE(pollout), 200);
	zassert_true(k_uptime_get_32() - tstamp < 100, "");
	zassert_equal(res, 1, "");
	zassert_equal(pollout[0].revents, ZSOCK_POLLOUT, "");

	res = zsock_close(c_sock_tcp);
	zassert_equal(res, 0, "close failed");

	res = zsock_close(s_sock_tcp);
	zassert_equal(res, 0, "close failed");

	/* Close one socket and ensure POLLNVAL happens */
	res = zsock_close(c_sock);
	zassert_equal(res, 0, "close failed");

	tstamp = k_uptime_get_32();
	res = zsock_poll(pollfds, ARRAY_SIZE(pollfds), 0);
	zassert_true(k_uptime_get_32() - tstamp <= FUZZ, "");
	zassert_equal(res, 1, "");
	zassert_equal(pollfds[0].revents, ZSOCK_POLLNVAL, "");
	zassert_equal(pollfds[1].revents, 0, "");

	res = zsock_close(s_sock);
	zassert_equal(res, 0, "close failed");

	k_sleep(TCP_TEARDOWN_TIMEOUT);
}

#define TEST_SNDBUF_SIZE CONFIG_NET_TCP_MAX_RECV_WINDOW_SIZE

ZTEST(net_socket_poll, test_pollout_tcp)
{
	int res;
	int c_sock;
	int s_sock;
	int new_sock;
	struct net_sockaddr_in6 c_addr;
	struct net_sockaddr_in6 s_addr;
	struct zsock_pollfd pollout[1];
	char buf[TEST_SNDBUF_SIZE] = { };

	prepare_sock_tcp_v6(MY_IPV6_ADDR, CLIENT_PORT, &c_sock, &c_addr);
	prepare_sock_tcp_v6(MY_IPV6_ADDR, SERVER_PORT, &s_sock, &s_addr);

	res = zsock_bind(s_sock, (struct net_sockaddr *)&s_addr, sizeof(s_addr));
	zassert_equal(res, 0, "");
	res = zsock_listen(s_sock, 0);
	zassert_equal(res, 0, "");
	res = zsock_connect(c_sock, (const struct net_sockaddr *)&s_addr,
			    sizeof(s_addr));
	zassert_equal(res, 0, "");
	new_sock = zsock_accept(s_sock, NULL, NULL);
	zassert_true(new_sock >= 0, "");

	k_msleep(10);

	/* POLLOUT should be reported after connecting */
	memset(pollout, 0, sizeof(pollout));
	pollout[0].fd = c_sock;
	pollout[0].events = ZSOCK_POLLOUT;

	res = zsock_poll(pollout, ARRAY_SIZE(pollout), 10);
	zassert_equal(res, 1, "");
	zassert_equal(pollout[0].revents, ZSOCK_POLLOUT, "");

	/* POLLOUT should not be reported after filling the window */
	res = zsock_send(c_sock, buf, sizeof(buf), 0);
	zassert_equal(res, sizeof(buf), "");

	memset(pollout, 0, sizeof(pollout));
	pollout[0].fd = c_sock;
	pollout[0].events = ZSOCK_POLLOUT;

	res = zsock_poll(pollout, ARRAY_SIZE(pollout), 10);
	zassert_equal(res, 0, "%d", pollout[0].revents);
	zassert_equal(pollout[0].revents, 0, "");

	/* POLLOUT should be reported again after consuming the data server
	 * side.
	 */
	res = zsock_recv(new_sock, buf, sizeof(buf), 0);
	zassert_equal(res, sizeof(buf), "");

	memset(pollout, 0, sizeof(pollout));
	pollout[0].fd = c_sock;
	pollout[0].events = ZSOCK_POLLOUT;

	/* Wait longer this time to give TCP stack a chance to send ZWP. */
	res = zsock_poll(pollout, ARRAY_SIZE(pollout), 500);
	zassert_equal(res, 1, "");
	zassert_equal(pollout[0].revents, ZSOCK_POLLOUT, "");

	k_msleep(10);

	/* Finalize the test */
	res = zsock_close(c_sock);
	zassert_equal(res, 0, "close failed");
	res = zsock_close(s_sock);
	zassert_equal(res, 0, "close failed");
	res = zsock_close(new_sock);
	zassert_equal(res, 0, "close failed");
}

/* Emulation of a "registering" poll backend.
 *
 * Such a backend arms real state in POLL_PREPARE and tears it down again in
 * POLL_UPDATE - the native offloaded sockets driver, for instance, registers
 * the socket with the host poller, initialises a k_poll_signal and arms a
 * callback in POLL_PREPARE, then deregisters and disarms all of it in
 * POLL_UPDATE. Only POLL_PREPARE can restore that state; in particular
 * k_poll_signal_init() is the sole thing that clears a previous raise.
 *
 * The backends built into the native network stack are purely declarative
 * (POLL_PREPARE only fills in a k_poll_event slot, POLL_UPDATE only inspects
 * it), so they cannot exercise this path - hence the behaviour is modelled
 * here directly.
 */
struct poll_retry_backend {
	struct k_poll_signal signal;
	bool armed;
	bool raise_on_arm;
	bool raise_once;
	int eagain_rounds;
	int prepare_count;
	int update_count;
};

static int poll_retry_ioctl(void *obj, unsigned int request, va_list args)
{
	struct poll_retry_backend *backend = obj;
	struct k_poll_event **pev;
	struct zsock_pollfd *pfd;

	switch (request) {
	case ZFD_IOCTL_POLL_PREPARE: {
		struct k_poll_event *pev_end;

		pfd = va_arg(args, struct zsock_pollfd *);
		pev = va_arg(args, struct k_poll_event **);
		pev_end = va_arg(args, struct k_poll_event *);

		if (!(pfd->events & ZSOCK_POLLIN)) {
			return 0;
		}

		if (*pev == pev_end) {
			return -ENOMEM;
		}

		backend->prepare_count++;

		/* Re-initialising the signal is what clears an earlier raise. */
		k_poll_signal_init(&backend->signal);
		k_poll_event_init(*pev, K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY,
				  &backend->signal);
		(*pev)++;
		backend->armed = true;

		if (backend->raise_on_arm) {
			if (backend->raise_once) {
				backend->raise_on_arm = false;
			}

			k_poll_signal_raise(&backend->signal, ZSOCK_POLLIN);
		}

		return 0;
	}
	case ZFD_IOCTL_POLL_UPDATE: {
		unsigned int signaled = 0;
		int flags = 0;

		pfd = va_arg(args, struct zsock_pollfd *);
		pev = va_arg(args, struct k_poll_event **);

		if (!(pfd->events & ZSOCK_POLLIN)) {
			return 0;
		}

		(*pev)++;
		backend->update_count++;

		if (!backend->armed) {
			/* Torn down by an earlier POLL_UPDATE and never
			 * re-prepared, so nothing can be reported.
			 */
			return 0;
		}

		/* Tear the registration down, deliberately leaving any raise
		 * on the signal in place.
		 */
		backend->armed = false;

		k_poll_signal_check(&backend->signal, &signaled, &flags);

		if (backend->eagain_rounds > 0) {
			backend->eagain_rounds--;
			/* Input was consumed but produced nothing for the
			 * caller (e.g. the TLS layer decrypting a
			 * post-handshake record) - ask poll() to keep waiting.
			 */
			return -EAGAIN;
		}

		if (signaled) {
			pfd->revents |= ZSOCK_POLLIN;
		}

		return 0;
	}
	default:
		errno = EOPNOTSUPP;
		return -1;
	}
}

static int poll_retry_close(void *obj)
{
	ARG_UNUSED(obj);

	return 0;
}

static const struct fd_op_vtable poll_retry_vtable = {
	.close = poll_retry_close,
	.ioctl = poll_retry_ioctl,
};

static int poll_retry_open(struct poll_retry_backend *backend)
{
	int fd = zvfs_reserve_fd();

	zassert_true(fd >= 0, "no free fd");
	zvfs_finalize_fd(fd, backend, &poll_retry_vtable);

	return fd;
}

/* A POLL_UPDATE returning -EAGAIN means "I consumed something, but it produced
 * nothing for the caller - keep waiting". poll() must honour the remaining
 * timeout instead of reporting a spurious timeout straight away.
 *
 * Without re-arming, the backend's stale raised signal makes the next k_poll()
 * return immediately, the torn-down backend reports nothing, and poll() would
 * return 0 within microseconds of a 100 ms deadline.
 */
ZTEST(net_socket_poll, test_poll_update_retry_waits_for_timeout)
{
	struct poll_retry_backend backend = {
		.raise_on_arm = true,
		.raise_once = true,
		.eagain_rounds = 1,
	};
	struct zsock_pollfd pollfds[1];
	uint32_t tstamp;
	int fd;
	int res;

	fd = poll_retry_open(&backend);

	memset(pollfds, 0, sizeof(pollfds));
	pollfds[0].fd = fd;
	pollfds[0].events = ZSOCK_POLLIN;

	tstamp = k_uptime_get_32();
	res = zsock_poll(pollfds, ARRAY_SIZE(pollfds), 100);
	tstamp = k_uptime_get_32() - tstamp;

	/* The only event was consumed by the retry and nothing else arrived,
	 * so poll() has to block for the full timeout before reporting 0.
	 */
	zassert_equal(res, 0);
	zassert_equal(pollfds[0].revents, 0);
	zassert_true(tstamp >= 100U, "poll() returned after %u ms, expected >= 100",
		     tstamp);

	/* The retry has to go through POLL_PREPARE again. */
	zassert_true(backend.prepare_count >= 2, "prepare_count %d",
		     backend.prepare_count);

	res = zvfs_close(fd);
	zassert_equal(res, 0, "close failed");
}

/* After a -EAGAIN retry, poll() must re-arm the descriptor so that a backend
 * which tears its state down in POLL_UPDATE can still deliver a later event.
 * Without the re-arm the backend stays disarmed and the event is lost, making
 * poll() report a timeout despite the data being available.
 */
ZTEST(net_socket_poll, test_poll_update_retry_rearms_backend)
{
	struct poll_retry_backend backend = {
		.raise_on_arm = true,
		.raise_once = false,
		.eagain_rounds = 1,
	};
	struct zsock_pollfd pollfds[1];
	int fd;
	int res;

	fd = poll_retry_open(&backend);

	memset(pollfds, 0, sizeof(pollfds));
	pollfds[0].fd = fd;
	pollfds[0].events = ZSOCK_POLLIN;

	res = zsock_poll(pollfds, ARRAY_SIZE(pollfds), 100);

	zassert_equal(res, 1);
	zassert_equal(pollfds[0].revents, ZSOCK_POLLIN);
	zassert_true(backend.prepare_count >= 2, "prepare_count %d",
		     backend.prepare_count);

	res = zvfs_close(fd);
	zassert_equal(res, 0, "close failed");
}

ZTEST_SUITE(net_socket_poll, NULL, NULL, NULL, NULL, NULL);
