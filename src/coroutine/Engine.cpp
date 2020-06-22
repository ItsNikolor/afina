#include <afina/coroutine/Engine.h>

#include <setjmp.h>
#include <stdio.h>
#include <string.h>

namespace Afina {
namespace Coroutine {

void Engine::Store(context &ctx) {
    char StackEnd;
    ctx.Hight = &StackEnd;
    size_t stack_size = abs(ctx.Hight - this->StackBottom + 1);
    char *stack_adr = new char[stack_size];
    memcpy(stack_adr, this->StackBottom, stack_size);

    auto old_stack = std::get<0>(ctx.Stack);
    if (old_stack) {
        delete old_stack;
    }

    ctx.Stack = std::make_tuple(stack_adr, stack_size);
}

void Engine::Restore(context &ctx) {
    char StackEnd;
    if ((&StackEnd >= this->StackBottom && &StackEnd <= ctx.Hight) ||
        (&StackEnd <= this->StackBottom && &StackEnd >= ctx.Hight)) {
        Restore(ctx);
    }
    memcpy(this->StackBottom, std::get<0>(ctx.Stack), std::get<1>(ctx.Stack));
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
