#include "coroutine.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stddef.h>
#include <string.h>
#include <stdint.h>

#if __APPLE__ && __MACH__
	#include <sys/ucontext.h>
#else
	#include <ucontext.h>
#endif

#define STACK_SIZE (1024*1024)
#define DEFAULT_COROUTINE 16

struct coroutine;

struct schedule {
	char stack[STACK_SIZE];   // 栈空间
	ucontext_t main;          // 调度器上下文
	int nco;                  // 当前协程数
	int cap;                  // 最大支持的协程数
	int running;              // 当前运行的协程id
	struct coroutine **co;    // 保存所有协程的数据结构
};

struct coroutine {
	coroutine_func func;    // 协程运行的函数
	void *ud;               // 函数参数
	ucontext_t ctx;         // 协程的运行上下文
	struct schedule * sch;  // 协程调度
	ptrdiff_t cap;          // 协程的堆栈能力
	ptrdiff_t size;
	int status;             // 协程的状态
	char *stack;            // 协程运行的栈空间
};

// 初始化一个协程的数据结构
struct coroutine *
_co_new(struct schedule *S , coroutine_func func, void *ud) {
	struct coroutine * co = malloc(sizeof(*co));
	co->func = func;
	co->ud = ud;
	co->sch = S;
	co->cap = 0;
	co->size = 0;
	co->status = COROUTINE_READY;
	co->stack = NULL;
	return co;
}

void
_co_delete(struct coroutine *co) {
	free(co->stack);
	free(co);
}

// 初始化schedule调度，初始化运行的状态，最大支持的协程数量，初始化协程数组(保存所有的协程数据结构指针)
struct schedule *
coroutine_open(void) {
	struct schedule *S = malloc(sizeof(*S));
	S->nco = 0;
	S->cap = DEFAULT_COROUTINE;
	S->running = -1;
	S->co = malloc(sizeof(struct coroutine *) * S->cap);
	memset(S->co, 0, sizeof(struct coroutine *) * S->cap);
	return S;
}


// 释放协程相关的数据结构信息
void
coroutine_close(struct schedule *S) {
	int i;
	for (i=0;i<S->cap;i++) {
		struct coroutine * co = S->co[i];
		if (co) {
			_co_delete(co);
		}
	}
	free(S->co);
	S->co = NULL;
	free(S);
}

// 创建一个协程
int
coroutine_new(struct schedule *S, coroutine_func func, void *ud) {
	struct coroutine *co = _co_new(S, func , ud); // 创建协程的数据结构
	if (S->nco >= S->cap) { // 是否超过支持的最大协程数
		int id = S->cap;      // 自动扩容，成倍数扩展，更新当前的协程数S->nco，返回协程id
		S->co = realloc(S->co, S->cap * 2 * sizeof(struct coroutine *));
		memset(S->co + S->cap , 0 , sizeof(struct coroutine *) * S->cap);
		S->co[S->cap] = co;
		S->cap *= 2;
		++S->nco;
		return id;
	} else {
		int i;
		for (i=0;i<S->cap;i++) {
			int id = (i+S->nco) % S->cap;
			if (S->co[id] == NULL) {    // 找到空余的位置放在，然后把数组下标当作协程的id返回
				S->co[id] = co;
				++S->nco;
				return id;
			}
		}
	}
	assert(0);
	return -1;
}

static void
mainfunc(uint32_t low32, uint32_t hi32) {
	uintptr_t ptr = (uintptr_t)low32 | ((uintptr_t)hi32 << 32);
	struct schedule *S = (struct schedule *)ptr;    // 拿到调度器
	int id = S->running;                            // 拿到当前协程id，并通过id拿到协程的数据结构，然后执行入口函数，执行玩后协程协程上下文，清空数组。
	struct coroutine *C = S->co[id];
	C->func(S,C->ud);
	_co_delete(C);
	S->co[id] = NULL;
	--S->nco;
	S->running = -1;
}

// 恢复指定id所对应的协程
void
coroutine_resume(struct schedule * S, int id) {
	assert(S->running == -1);
	assert(id >=0 && id < S->cap);
	struct coroutine *C = S->co[id];  // 根据id到数组中拿到协程的数据结构，然后进入状态机
	if (C == NULL)
		return;
	int status = C->status;
	switch(status) {
	case COROUTINE_READY: // 准备状态的话就拿到上下文，设置共享栈
		getcontext(&C->ctx);
		C->ctx.uc_stack.ss_sp = S->stack;
		C->ctx.uc_stack.ss_size = STACK_SIZE;
		C->ctx.uc_link = &S->main;  // C 这个协程运行结束后自动运行S-main这个上下文对应的协程
		S->running = id;    // 保存当前运行的协程的id
		C->status = COROUTINE_RUNNING;  // 设置协程为运行状态
		uintptr_t ptr = (uintptr_t)S;
		makecontext(&C->ctx, (void (*)(void)) mainfunc, 2, (uint32_t)ptr, (uint32_t)(ptr>>32)); // 初始化ctx，指定ctx这个上下文的入口函数maninfunc
		swapcontext(&S->main, &C->ctx);   // 上下文切换，将当前上下文保存在S-main，然后切换到C-ctx这个上下文
		break;
	case COROUTINE_SUSPEND:
		memcpy(S->stack + STACK_SIZE - C->size, C->stack, C->size); // 把协程的堆栈拷贝到全局的共享栈上
		S->running = id;                                            // 指定要运行的协程，设置运行状态，开始上下文切换
		C->status = COROUTINE_RUNNING;
		swapcontext(&S->main, &C->ctx);
		break;
	default:
		assert(0);
	}
}

//
static void
_save_stack(struct coroutine *C, char *top) {
	char dummy = 0;
	assert(top - &dummy <= STACK_SIZE);
	if (C->cap < top - &dummy) {  // top是当前协程的栈顶，dummy是个hack 方法，表示当前栈尾，两者相减就是当前栈大小，C-cap
		free(C->stack);
		C->cap = top-&dummy;        // 保存这个协程的栈大小，
		C->stack = malloc(C->cap);  // 分配这么大的栈
	}
  // 设置栈大小，然后拷贝这个协程的运行栈到C-stack上
	C->size = top - &dummy;
	memcpy(C->stack, &dummy, C->size);
}

// 协程让出CPU，把自己设置为COROUTINE_SUSPEND状态
void
coroutine_yield(struct schedule * S) {
	int id = S->running;
	assert(id >= 0);
	struct coroutine * C = S->co[id];
	assert((char *)&C > S->stack);
	_save_stack(C,S->stack + STACK_SIZE); // 进行堆栈保存的操作
	C->status = COROUTINE_SUSPEND;
	S->running = -1;
	swapcontext(&C->ctx , &S->main);      // 上下文切换
}


// 返回指定id对应协程的状态
int
coroutine_status(struct schedule * S, int id) {
	assert(id>=0 && id < S->cap);
	if (S->co[id] == NULL) {
		return COROUTINE_DEAD;
	}
	return S->co[id]->status;
}

// 返回当前运行的协程id
int
coroutine_running(struct schedule * S) {
	return S->running;
}

