/* Copyright (c) 2012, Bastien Dejean
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <time.h>
#include "bspwm.h"
#include "desktop.h"
#include "monitor.h"
#include "settings.h"
#include "tree.h"
#include "autohide.h"

typedef enum {
	AUTOHIDE_NONE = 0,
	AUTOHIDE_BLUR,
	AUTOHIDE_DESKTOP
} autohide_pending_t;

static autohide_pending_t autohide_pending = AUTOHIDE_NONE;
static struct timespec autohide_deadline;

static int timespec_cmp(const struct timespec *a, const struct timespec *b)
{
	if (a->tv_sec != b->tv_sec) {
		return a->tv_sec < b->tv_sec ? -1 : 1;
	}
	if (a->tv_nsec == b->tv_nsec) {
		return 0;
	}
	return a->tv_nsec < b->tv_nsec ? -1 : 1;
}

static void timespec_add_ms(struct timespec *ts, unsigned int ms)
{
	ts->tv_sec += (time_t) (ms / 1000);
	ts->tv_nsec += (long) ((ms % 1000) * 1000000L);
	if (ts->tv_nsec >= 1000000000L) {
		ts->tv_sec++;
		ts->tv_nsec -= 1000000000L;
	}
}

static void hide_on_blur(void)
{
	node_t *focused = (mon != NULL && mon->desk != NULL) ? mon->desk->focus : NULL;
	for (monitor_t *m = mon_head; m != NULL; m = m->next) {
		for (desktop_t *d = m->desk_head; d != NULL; d = d->next) {
			for (node_t *n = first_extrema(d->root); n != NULL; n = next_leaf(n, d->root)) {
				if (n->client == NULL || !n->scratch || n->hidden) {
					continue;
				}
				if (focused != NULL && n == focused) {
					continue;
				}
				set_hidden(m, d, n, true);
			}
		}
	}
}

static void hide_on_desktop(void)
{
	for (monitor_t *m = mon_head; m != NULL; m = m->next) {
		for (desktop_t *d = m->desk_head; d != NULL; d = d->next) {
			for (node_t *n = first_extrema(d->root); n != NULL; n = next_leaf(n, d->root)) {
				if (n->client != NULL && n->scratch && !n->hidden) {
					set_hidden(m, d, n, true);
				}
			}
		}
	}
}

static void run_pending(void)
{
	if (autohide_pending == AUTOHIDE_DESKTOP) {
		hide_on_desktop();
	} else if (autohide_pending == AUTOHIDE_BLUR) {
		hide_on_blur();
	}
	autohide_pending = AUTOHIDE_NONE;
}

static void schedule_blur(void)
{
	if (!scratch_autohide) {
		return;
	}
	if (autohide_pending == AUTOHIDE_DESKTOP) {
		return;
	}
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	autohide_pending = AUTOHIDE_BLUR;
	autohide_deadline = now;
	timespec_add_ms(&autohide_deadline, scratch_autohide_blur_delay_ms);
}

static void schedule_desktop(void)
{
	if (!scratch_autohide) {
		return;
	}
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	autohide_pending = AUTOHIDE_DESKTOP;
	autohide_deadline = now;
	timespec_add_ms(&autohide_deadline, scratch_autohide_desktop_delay_ms);
}

void autohide_on_put_status(subscriber_mask_t mask)
{
	if (!scratch_autohide) {
		return;
	}
	if (mask & (SBSC_MASK_DESKTOP_FOCUS | SBSC_MASK_DESKTOP_ACTIVATE | SBSC_MASK_MONITOR_FOCUS)) {
		schedule_desktop();
	} else if (mask & SBSC_MASK_NODE_FOCUS) {
		schedule_blur();
	}
}

void autohide_prepare_select(struct timeval **tvp, struct timeval *tv_buf)
{
	*tvp = NULL;
	autohide_pump();
	if (!scratch_autohide || autohide_pending == AUTOHIDE_NONE) {
		return;
	}
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	if (timespec_cmp(&now, &autohide_deadline) >= 0) {
		return;
	}
	long long ns = (long long) (autohide_deadline.tv_sec - now.tv_sec) * 1000000000LL
		+ (autohide_deadline.tv_nsec - now.tv_nsec);
	if (ns <= 0) {
		return;
	}
	tv_buf->tv_sec = (time_t) (ns / 1000000000LL);
	tv_buf->tv_usec = (suseconds_t) ((ns % 1000000000LL) / 1000LL);
	*tvp = tv_buf;
}

void autohide_pump(void)
{
	if (!scratch_autohide || autohide_pending == AUTOHIDE_NONE) {
		return;
	}
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	if (timespec_cmp(&now, &autohide_deadline) < 0) {
		return;
	}
	run_pending();
}
