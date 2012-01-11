#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <ucontext.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

struct coro_task;

struct coro_ctx {
	ucontext_t idle;
	struct coro_task *last_tsk;
	/* struct list_head rdlist; */
	/* struct list_head waitlist; */
	/* struct list_head tsklist; */
};

#define	CORO_WAIT	0x01
struct coro_task {
	struct coro_ctx *ctx;
	ucontext_t uc;
	uint32_t flags;
	struct timeval timeout;
	int efd;
};

/* hole for cache stack overflow */
#define	STACK_HOLE	4096

#define	SET_F_WAIT(t)	((t)->flags |=  CORO_WAIT)
#define	CLR_F_WAIT(t)	((t)->flags &= ~CORO_WAIT)
#define	TST_F_WAIT(t)	((t)->flags & CORO_WAIT)

int coro_task_wait(struct coro_task *tsk, int fd, int event,
		   struct timeval *timeout)
{
	struct coro_ctx *ctx = tsk->ctx;
	int err;

	SET_F_WAIT(tsk);
	getcontext(&tsk->uc);
	if (tsk->flags & CORO_WAIT) {
		fprintf(stderr, "tsk wait\n");
		err = setcontext(&ctx->idle);
		fprintf(stderr, "tsk wait 2\n");
	}

	return 0;
}

struct coro_task * coro_task_new(struct coro_ctx *ctx,
				 void (*func)(struct coro_task *tsk, void *),
				 void *arg)
{
	struct coro_task *tsk = (struct coro_task *)calloc(1, sizeof(*tsk));
	ucontext_t *ucp = &tsk->uc;

	/* init context */
	ucp->uc_link = &ctx->idle;
	ucp->uc_stack.ss_sp = malloc(SIGSTKSZ);
	ucp->uc_stack.ss_size = (SIGSTKSZ + STACK_HOLE);
	getcontext(ucp);
	makecontext(ucp, (void (*)())func, 2, tsk, arg);
	tsk->ctx = ctx;
	return tsk;
}

void coro_task_free(struct coro_task *tsk)
{
	free(tsk->uc.uc_stack.ss_sp);
	free(tsk);
}

int coro_init(struct coro_ctx **ctx1)
{
	struct coro_ctx *ctx;

	ctx = (struct coro_ctx *)calloc(1, sizeof(*ctx));
	if (!ctx)
		return -ENOMEM;

	*ctx1 = ctx;
	return 0;
}

void coro_run_task(struct coro_ctx *ctx, struct coro_task *tsk)
{
#ifndef	REG_RSP
#define	REG_RSP	15
#endif
	unsigned long *regs = (unsigned long *)&tsk->uc.uc_mcontext.gregs;
	unsigned long stack_base = (unsigned long)tsk->uc.uc_stack.ss_sp + \
				    tsk->uc.uc_stack.ss_size;
	int i = 0;

	/* check stack overflow */
	if ((stack_base - regs[REG_RSP]) > SIGSTKSZ) {
		fprintf(stderr, "stack overflow %lx %lx\n",
			stack_base, regs[REG_RSP]);
	} else {
//		fprintf(stderr, "stack not overflow %lx %lx\n",
//			stack_base, regs[REG_RSP]);
	}

	CLR_F_WAIT(tsk);
	setcontext(&tsk->uc);
}

/* test code starts here */

static void coro_test_func(struct coro_task *tsk, void *arg)
{
	while (1) {
		printf("%s 1\n", __func__);
		sleep(1);
		coro_task_wait(tsk, 1, 0, NULL);
		printf("%s 2\n", __func__);
	}
}

void coro_testcase(void)
{
	struct coro_ctx *ctx;
	struct coro_task *tsk;

	coro_init(&ctx);
	tsk = coro_task_new(ctx, coro_test_func, NULL);

	getcontext(&ctx->idle);
	while (1) {
		printf("call tsk\n");
		coro_run_task(ctx, tsk);
	}
}

int main(int arg, char **argv)
{
	coro_testcase();
	return 0;
}
