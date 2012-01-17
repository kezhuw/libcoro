/*
 * Coroutine
 *
 * andrewli@tencent.com
 * 2012-01-18 00:25:07
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <ucontext.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <event.h>
#include <assert.h>
#include <sys/eventfd.h>

#define	SEE_HERE	fprintf(stderr, "HERE %s:%d\n", __func__, __LINE__)
struct coro_ctx;

struct coro_task {
	struct coro_ctx *ctx;
	ucontext_t uc;
	uint32_t flags;
	struct timeval timeout;
	struct event ev;
	int efd;
};

struct coro_ctx {
	struct coro_task *last_tsk;
	struct coro_task *curr_tsk;
	struct coro_task *idle_tsk;
	struct event_base *eb;
	/* struct list_head rdlist; */
	/* struct list_head waitlist; */
	/* struct list_head tsklist; */
};

/* hole for catch stack overflow */
#define	STACK_HOLE	4096

#define	CORO_WAIT	0x01
#define	SET_F_WAIT(t)	((t)->flags |=  CORO_WAIT)
#define	CLR_F_WAIT(t)	((t)->flags &= ~CORO_WAIT)
#define	TST_F_WAIT(t)	((t)->flags & CORO_WAIT)

#define	CORO_SCHED	0x02
#define	SET_F_SCHED(t)	((t)->flags |=  CORO_SCHED)
#define	CLR_F_SCHED(t)	((t)->flags &= ~CORO_SCHED)
#define	TST_F_SCHED(t)	((t)->flags & CORO_SCHED)

static void ctx_prepare_change_task(struct coro_ctx *ctx,
				    struct coro_task *next)
{
	ctx->last_tsk = ctx->curr_tsk;
	ctx->curr_tsk = next;
}

void coro_run_task(struct coro_ctx *ctx, struct coro_task *tsk)
{
#ifndef	REG_RSP
#define	REG_RSP	15
#endif
	unsigned long *regs = (unsigned long *)&tsk->uc.uc_mcontext.gregs;
	unsigned long stack_base = (unsigned long)tsk->uc.uc_stack.ss_sp + \
				    tsk->uc.uc_stack.ss_size;

	/* check stack overflow */
	if ((stack_base - regs[REG_RSP]) > SIGSTKSZ) {
		fprintf(stderr, "stack overflow %lx %lx\n",
			stack_base, regs[REG_RSP]);
	} else {
//		fprintf(stderr, "stack not overflow %lx %lx\n",
//			stack_base, regs[REG_RSP]);
	}

	CLR_F_WAIT(tsk);
	CLR_F_SCHED(tsk);

	ctx_prepare_change_task(ctx, tsk);
	setcontext(&tsk->uc);
}

static void coro_task_event_cb(int fd, short event, void *arg)
{
	struct coro_task *tsk = (struct coro_task *)arg;
	struct coro_ctx *ctx = tsk->ctx;

	assert(ctx->curr_tsk == ctx->idle_tsk);
	printf("event %d\n", event);

	SET_F_SCHED(tsk);

	/* save current uc */
	getcontext(&ctx->idle_tsk->uc);
	SEE_HERE;

	printf("curr %p, idle %p, next %p %x\n",
	       ctx->curr_tsk, ctx->idle_tsk, tsk, tsk->flags);
	if (TST_F_SCHED(tsk)) {
		printf("switch to task %p\n", tsk);
		coro_run_task(ctx, tsk);
	}

	printf("return to loop\n");
	/* return to loop */
	return;
}

static void switch_idle(struct coro_ctx *ctx)
{
	ctx_prepare_change_task(ctx, ctx->idle_tsk);
	setcontext(&ctx->curr_tsk->uc);
}

void coro_schedule(struct coro_ctx *ctx)
{
	struct coro_task *tsk = ctx->curr_tsk;

	printf("prepare schedule %p\n", tsk);
	SET_F_WAIT(tsk);

	getcontext(&tsk->uc);
	if (tsk->flags & CORO_WAIT) {
		/* goto idle */
		switch_idle(ctx);
		assert(0);	/* never reached */
	}
}

int coro_task_wait(struct coro_task *tsk, int fd, int event,
		   struct timeval *timeout)
{
	struct coro_ctx *ctx = tsk->ctx;
	int err;

	assert(ctx->curr_tsk == tsk);

	/* init event */
	err = event_assign(&tsk->ev, ctx->eb, tsk->efd, EV_READ,
			   coro_task_event_cb, tsk);
	if (err != 0) {
		fprintf(stderr, "event_assign error %d\n", err);
		return err;
	}

	event_add(&tsk->ev, timeout);

	/* schedule */
	coro_schedule(ctx);
	return 0;
}

struct coro_task * coro_task_new(struct coro_ctx *ctx,
				 void (*func)(struct coro_task *tsk, void *),
				 void *arg)
{
	struct coro_task *tsk = (struct coro_task *)calloc(1, sizeof(*tsk));
	ucontext_t *ucp = &tsk->uc;

	/* init context */
	if (ctx->idle_tsk)
		ucp->uc_link = &ctx->idle_tsk->uc;
	else
		/* TODO(andrewli) ... */
		ucp->uc_link = NULL;

	ucp->uc_stack.ss_sp = malloc(SIGSTKSZ);
	ucp->uc_stack.ss_size = (SIGSTKSZ + STACK_HOLE);
	getcontext(ucp);
	makecontext(ucp, (void (*)())func, 2, tsk, arg);
	tsk->ctx = ctx;

	tsk->efd = eventfd(0, 0);
	return tsk;
}

void coro_task_free(struct coro_task *tsk)
{
	free(tsk->uc.uc_stack.ss_sp);
	free(tsk);
}

static void coro_idle(struct coro_task *tsk, void *arg)
{
	struct coro_ctx *ctx = tsk->ctx;

	SEE_HERE;
	event_base_loop(ctx->eb, 0);

	/* TODO(andrewli) ... */
	exit(0);
}

int coro_init(struct coro_ctx **ctx1)
{
	struct coro_ctx *ctx;

	ctx = (struct coro_ctx *)calloc(1, sizeof(*ctx));
	if (!ctx)
		return -ENOMEM;

	ctx->eb = event_base_new();
	ctx->idle_tsk = coro_task_new(ctx, coro_idle, NULL);
	*ctx1 = ctx;
	return 0;
}

/* test code starts here */

static void coro_test_func(struct coro_task *tsk, void *arg)
{
	struct timeval tv = {0, 1000 * 500};

	while (1) {
		printf("%s 1\n", __func__);
		coro_task_wait(tsk, 1, 0, &tv);
		printf("%s 2\n", __func__);
	}
}

int main(int arg, char **argv)
{
	struct coro_ctx *ctx;
	struct coro_task *tsk;

	coro_init(&ctx);
	tsk = coro_task_new(ctx, coro_test_func, NULL);
	coro_run_task(ctx, tsk);
	return 0;
}
