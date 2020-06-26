#include <afina/coroutine/Engine.h>

#include <setjmp.h>
#include <stdio.h>
#include <string.h>

namespace Afina {
namespace Coroutine {

void Engine::Store(context &ctx) {
    volatile char StackEnd;
    ctx.Hight = std::max(const_cast<char *>(&StackEnd), ctx.Hight);
    ctx.Low = std::min(const_cast<char *>(&StackEnd), ctx.Low);

    size_t stack_size = ctx.Hight - ctx.Low;
    char *stack_adr = std::get<0>(ctx.Stack);

    if (stack_size > std::get<1>(ctx.Stack)) {
        delete stack_adr;
        stack_adr = new char[stack_size];
        ctx.Stack = std::make_tuple(stack_adr, stack_size);
    }
    memcpy(stack_adr, ctx.Low, stack_size);
    return;
}

void Engine::Restore(context &ctx) {
    volatile char StackEnd;
    if (&StackEnd >= ctx.Low && &StackEnd <= ctx.Hight) {
        Restore(ctx);
    }
    memcpy(ctx.Low, std::get<0>(ctx.Stack), ctx.Hight - ctx.Low);
    longjmp(ctx.Environment, 1);
}

void Engine::yield() {
    if (alive->next == nullptr) {
        return;
    }
    if (setjmp(cur_routine->Environment) > 0) {
        return;
    }
    Store(*cur_routine);
    Restore(*alive->next);
}

void Engine::sched(void *routine_) {
    if (routine_ == nullptr) {
        yield();
    }
    if (routine_ == cur_routine) {
        return;
    }

    if (setjmp(cur_routine->Environment) > 0) {
        return;
    }
    Store(*cur_routine);
    Restore(*static_cast<context *>(routine_));
}

} // namespace Coroutine
} // namespace Afina
