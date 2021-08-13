/*
 * Copyright (c) 2021, Krisna Pranav
 *
 * SPDX-License-Identifier: BSD-2-Clause
*/

// includes
#include <base/ScopeGuard.h>
#include <base/StringBuilder.h>
#include <base/Time.h>
#include <kernel/arch/x86/SmapDisabler.h>
#include <kernel/arch/x86/TrapFrame.h>
#include <kernel/Debug.h>
#include <kernel/devices/KCOVDevice.h>
#include <kernel/filesystem/FileDescription.h>
#include <kernel/KSyms.h>
#include <kernel/memory/MemoryManager.h>
#include <kernel/memory/PageDirectory.h>
#include <kernel/memory/ProcessPagingScope.h>
#include <kernel/Panic.h>
#include <kernel/PerformanceEventBuffer.h>
#include <kernel/Process.h>
#include <kernel/ProcessExposed.h>
#include <kernel/Scheduler.h>
#include <kernel/Sections.h>
#include <kernel/Thread.h>
#include <kernel/ThreadTracer.h>
#include <kernel/TimerQueue.h>
#include <libc/signal_numbers.h>

namespace Kernel {

SpinLock<u8> Thread::g_tid_map_lock;
READONLY_AFTER_INIT HashMap<ThreadID, Thread*>* Thread::g_tid_map;

UNMAP_AFTER_INIT void Thread::initialize()
{
    g_tid_map = new HashMap<ThreadID, Thread*>();
}

KResultOr<NonnullRefPtr<Thread>> Thread::try_create(NonnullRefPtr<Process> process)
{
    auto kernel_stack_region = MM.allocate_kernel_region(default_kernel_stack_size, {}, Memory::Region::Access::ReadWrite, AllocationStrategy::AllocateNow);
    if (!kernel_stack_region)
        return ENOMEM;
    kernel_stack_region->set_stack(true);

    auto block_timer = Base::try_create<Timer>();
    if (!block_timer)
        return ENOMEM;

    auto name = KString::try_create(process->name());

    auto thread = adopt_ref_if_nonnull(new (nothrow) Thread(move(process), kernel_stack_region.release_nonnull(), block_timer.release_nonnull(), move(name)));
    if (!thread)
        return ENOMEM;

    return thread.release_nonnull();
}

Thread::Thread(NonnullRefPtr<Process> process, NonnullOwnPtr<Memory::Region> kernel_stack_region, NonnullRefPtr<Timer> block_timer, OwnPtr<KString> name)
    : m_process(move(process))
    , m_kernel_stack_region(move(kernel_stack_region))
    , m_name(move(name))
    , m_block_timer(block_timer)
    , m_global_procfs_inode_index(ProcFSComponentRegistry::the().allocate_inode_index())
{
    bool is_first_thread = m_process->add_thread(*this);
    if (is_first_thread) {
        
        m_tid = m_process->pid().value();
    } else {
        m_tid = Process::allocate_pid().value();
    }

    {
        auto string = String::formatted("Kernel stack (thread {})", m_tid.value());
        m_kernel_stack_region->set_name(KString::try_create(string));
    }

    {
        ScopedSpinLock lock(g_tid_map_lock);
        auto result = g_tid_map->set(m_tid, this);
        VERIFY(result == Base::HashSetResult::InsertedNewEntry);
    }
    if constexpr (THREAD_DEBUG)
        dbgln("Created new thread {}({}:{})", m_process->name(), m_process->pid().value(), m_tid.value());

    reset_fpu_state();

#if ARCH(I386)
    
    m_regs.eflags = 0x0202;

    if (m_process->is_kernel_process()) {
        m_regs.cs = GDT_SELECTOR_CODE0;
        m_regs.ds = GDT_SELECTOR_DATA0;
        m_regs.es = GDT_SELECTOR_DATA0;
        m_regs.fs = 0;
        m_regs.ss = GDT_SELECTOR_DATA0;
        m_regs.gs = GDT_SELECTOR_PROC;
    } else {
        m_regs.cs = GDT_SELECTOR_CODE3 | 3;
        m_regs.ds = GDT_SELECTOR_DATA3 | 3;
        m_regs.es = GDT_SELECTOR_DATA3 | 3;
        m_regs.fs = GDT_SELECTOR_DATA3 | 3;
        m_regs.ss = GDT_SELECTOR_DATA3 | 3;
        m_regs.gs = GDT_SELECTOR_TLS | 3;
    }
#else
    // Only IF is set when a process boots.
    m_regs.rflags = 0x0202;

    if (m_process->is_kernel_process())
        m_regs.cs = GDT_SELECTOR_CODE0;
    else
        m_regs.cs = GDT_SELECTOR_CODE3 | 3;
#endif

    m_regs.cr3 = m_process->address_space().page_directory().cr3();

    m_kernel_stack_base = m_kernel_stack_region->vaddr().get();
    m_kernel_stack_top = m_kernel_stack_region->vaddr().offset(default_kernel_stack_size).get() & ~(FlatPtr)0x7u;

    if (m_process->is_kernel_process()) {
#if ARCH(I386)
        m_regs.esp = m_regs.esp0 = m_kernel_stack_top;
#else
        m_regs.rsp = m_regs.rsp0 = m_kernel_stack_top;
#endif
    } else {
        // Ring 3 processes get a separate stack for ring 0.
        // The ring 3 stack will be assigned by exec().
#if ARCH(I386)
        m_regs.ss0 = GDT_SELECTOR_DATA0;
        m_regs.esp0 = m_kernel_stack_top;
#else
        m_regs.rsp0 = m_kernel_stack_top;
#endif
    }

    ref();
}

Thread::~Thread()
{
    {

        ScopedSpinLock lock(g_scheduler_lock);
        VERIFY(!m_process_thread_list_node.is_in_list());

        VERIFY(m_runnable_priority < 0);
    }
    {
        ScopedSpinLock lock(g_tid_map_lock);
        auto result = g_tid_map->remove(m_tid);
        VERIFY(result);
    }
}

void Thread::block(Kernel::Mutex& lock, ScopedSpinLock<SpinLock<u8>>& lock_lock, u32 lock_count)
{
    VERIFY(!Processor::current().in_irq());
    VERIFY(this == Thread::current());
    ScopedCritical critical;
    VERIFY(!Memory::s_mm_lock.own_lock());

    ScopedSpinLock block_lock(m_block_lock);

    ScopedSpinLock scheduler_lock(g_scheduler_lock);

    switch (state()) {
    case Thread::Stopped:
        
        break;
    case Thread::Running:
        VERIFY(m_blocker == nullptr);
        break;
    default:
        VERIFY_NOT_REACHED();
    }

    auto& big_lock = process().big_lock();
    VERIFY((&lock == &big_lock && m_blocking_lock != &big_lock) || !m_blocking_lock);

    auto previous_blocking_lock = m_blocking_lock;
    m_blocking_lock = &lock;
    m_lock_requested_count = lock_count;

    set_state(Thread::Blocked);

    scheduler_lock.unlock();
    block_lock.unlock();

    lock_lock.unlock();

    dbgln_if(THREAD_DEBUG, "Thread {} blocking on Mutex {}", *this, &lock);

    for (;;) {

        VERIFY(!g_scheduler_lock.own_lock());
        VERIFY(Processor::in_critical());
        if (&lock != &big_lock && big_lock.own_lock()) {
            
            yield_and_release_relock_big_lock();
        } else {
            
            yield_without_releasing_big_lock(VerifyLockNotHeld::No);
        }
        VERIFY(Processor::in_critical());

        ScopedSpinLock block_lock2(m_block_lock);
        if (should_be_stopped() || state() == Stopped) {
            dbgln("Thread should be stopped, current state: {}", state_string());
            set_state(Thread::Blocked);
            continue;
        }

        VERIFY(!m_blocking_lock);
        m_blocking_lock = previous_blocking_lock;
        break;
    }

    lock_lock.lock();
}

u32 Thread::unblock_from_lock(Kernel::Mutex& lock)
{
    ScopedSpinLock block_lock(m_block_lock);
    VERIFY(m_blocking_lock == &lock);
    auto requested_count = m_lock_requested_count;
    block_lock.unlock();

    auto do_unblock = [&]() {
        ScopedSpinLock scheduler_lock(g_scheduler_lock);
        ScopedSpinLock block_lock(m_block_lock);
        VERIFY(m_blocking_lock == &lock);
        VERIFY(!Processor::current().in_irq());
        VERIFY(g_scheduler_lock.own_lock());
        VERIFY(m_block_lock.own_lock());
        VERIFY(m_blocking_lock == &lock);
        dbgln_if(THREAD_DEBUG, "Thread {} unblocked from Mutex {}", *this, &lock);
        m_blocking_lock = nullptr;
        if (Thread::current() == this) {
            set_state(Thread::Running);
            return;
        }
        VERIFY(m_state != Thread::Runnable && m_state != Thread::Running);
        set_state(Thread::Runnable);
    };
    if (Processor::current().in_irq()) {
        Processor::current().deferred_call_queue([do_unblock = move(do_unblock), self = make_weak_ptr()]() {
            if (auto this_thread = self.strong_ref())
                do_unblock();
        });
    } else {
        do_unblock();
    }
    return requested_count;
}

void Thread::unblock_from_blocker(Blocker& blocker)
{
    auto do_unblock = [&]() {
        ScopedSpinLock scheduler_lock(g_scheduler_lock);
        ScopedSpinLock block_lock(m_block_lock);
        if (m_blocker != &blocker)
            return;
        if (!should_be_stopped() && !is_stopped())
            unblock();
    };
    if (Processor::current().in_irq()) {
        Processor::current().deferred_call_queue([do_unblock = move(do_unblock), self = make_weak_ptr()]() {
            if (auto this_thread = self.strong_ref())
                do_unblock();
        });
    } else {
        do_unblock();
    }
}

void Thread::unblock(u8 signal)
{
    VERIFY(!Processor::current().in_irq());
    VERIFY(g_scheduler_lock.own_lock());
    VERIFY(m_block_lock.own_lock());
    if (m_state != Thread::Blocked)
        return;
    if (m_blocking_lock)
        return;
    VERIFY(m_blocker);
    if (signal != 0) {
        if (is_handling_page_fault()) {
            
            return;
        }
        if (!m_blocker->can_be_interrupted() && !m_should_die)
            return;
        m_blocker->set_interrupted_by_signal(signal);
    }
    m_blocker = nullptr;
    if (Thread::current() == this) {
        set_state(Thread::Running);
        return;
    }
    VERIFY(m_state != Thread::Runnable && m_state != Thread::Running);
    set_state(Thread::Runnable);
}

void Thread::set_should_die()
{
    if (m_should_die) {
        dbgln("{} Should already die", *this);
        return;
    }
    ScopedCritical critical;

    // Remember that we should die instead of returning to
    // the userspace.
    ScopedSpinLock lock(g_scheduler_lock);
    m_should_die = true;

    // NOTE: Even the current thread can technically be in "Stopped"
    // state! This is the case when another thread sent a SIGSTOP to
    // it while it was running and it calls e.g. exit() before
    // the scheduler gets involved again.
    if (is_stopped()) {
        // If we were stopped, we need to briefly resume so that
        // the kernel stacks can clean up. We won't ever return back
        // to user mode, though
        VERIFY(!process().is_stopped());
        resume_from_stopped();
    }
    if (is_blocked()) {
        ScopedSpinLock block_lock(m_block_lock);
        if (m_blocker) {
            // We're blocked in the kernel.
            m_blocker->set_interrupted_by_death();
            unblock();
        }
    }
}

void Thread::die_if_needed()
{
    VERIFY(Thread::current() == this);

    if (!m_should_die)
        return;

    u32 unlock_count;
    [[maybe_unused]] auto rc = unlock_process_if_locked(unlock_count);

    dbgln_if(THREAD_DEBUG, "Thread {} is dying", *this);

    {
        ScopedSpinLock lock(g_scheduler_lock);

        set_state(Thread::Dying);
    }

    ScopedCritical critical;

    Scheduler::yield();

    Processor::clear_critical();
    dbgln("die_if_needed returned from clear_critical!!! in irq: {}", Processor::current().in_irq());
    
    VERIFY_NOT_REACHED();
}

void Thread::exit(void* exit_value)
{
    VERIFY(Thread::current() == this);
    m_join_condition.thread_did_exit(exit_value);
    set_should_die();
    u32 unlock_count;
    [[maybe_unused]] auto rc = unlock_process_if_locked(unlock_count);
    if (m_thread_specific_range.has_value()) {
        auto* region = process().address_space().find_region_from_range(m_thread_specific_range.value());
        process().address_space().deallocate_region(*region);
    }
#ifdef ENABLE_KERNEL_COVERAGE_COLLECTION
    KCOVDevice::free_thread();
#endif
    die_if_needed();
}

void Thread::yield_without_releasing_big_lock(VerifyLockNotHeld verify_lock_not_held)
{
    VERIFY(!g_scheduler_lock.own_lock());
    VERIFY(verify_lock_not_held == VerifyLockNotHeld::No || !process().big_lock().own_lock());
    // Disable interrupts here. This ensures we don't accidentally switch contexts twice
    InterruptDisabler disable;
    Scheduler::yield(); // flag a switch
    u32 prev_critical = Processor::clear_critical();
    // NOTE: We may be on a different CPU now!
    Processor::restore_critical(prev_critical);
}

void Thread::yield_and_release_relock_big_lock()
{
    VERIFY(!g_scheduler_lock.own_lock());
    // Disable interrupts here. This ensures we don't accidentally switch contexts twice
    InterruptDisabler disable;
    Scheduler::yield(); // flag a switch
    u32 lock_count_to_restore = 0;
    auto previous_locked = unlock_process_if_locked(lock_count_to_restore);
    // NOTE: Even though we call Scheduler::yield here, unless we happen
    // to be outside of a critical section, the yield will be postponed
    // until leaving it in relock_process.
    relock_process(previous_locked, lock_count_to_restore);
}

LockMode Thread::unlock_process_if_locked(u32& lock_count_to_restore)
{
    return process().big_lock().force_unlock_if_locked(lock_count_to_restore);
}

void Thread::relock_process(LockMode previous_locked, u32 lock_count_to_restore)
{
    // Clearing the critical section may trigger the context switch
    // flagged by calling Scheduler::yield above.
    // We have to do it this way because we intentionally
    // leave the critical section here to be able to switch contexts.
    u32 prev_critical = Processor::clear_critical();

    // CONTEXT SWITCH HAPPENS HERE!

    // NOTE: We may be on a different CPU now!
    Processor::restore_critical(prev_critical);

    if (previous_locked != LockMode::Unlocked) {
        // We've unblocked, relock the process if needed and carry on.
        process().big_lock().restore_lock(previous_locked, lock_count_to_restore);
    }
}

auto Thread::sleep(clockid_t clock_id, const Time& duration, Time* remaining_time) -> BlockResult
{
    VERIFY(state() == Thread::Running);
    return Thread::current()->block<Thread::SleepBlocker>({}, Thread::BlockTimeout(false, &duration, nullptr, clock_id), remaining_time);
}

auto Thread::sleep_until(clockid_t clock_id, const Time& deadline) -> BlockResult
{
    VERIFY(state() == Thread::Running);
    return Thread::current()->block<Thread::SleepBlocker>({}, Thread::BlockTimeout(true, &deadline, nullptr, clock_id));
}

StringView Thread::state_string() const
{
    switch (state()) {
    case Thread::Invalid:
        return "Invalid"sv;
    case Thread::Runnable:
        return "Runnable"sv;
    case Thread::Running:
        return "Running"sv;
    case Thread::Dying:
        return "Dying"sv;
    case Thread::Dead:
        return "Dead"sv;
    case Thread::Stopped:
        return "Stopped"sv;
    case Thread::Blocked: {
        ScopedSpinLock block_lock(m_block_lock);
        if (m_blocking_lock)
            return "Mutex"sv;
        if (m_blocker)
            return m_blocker->state_string();
        VERIFY_NOT_REACHED();
    }
    }
    PANIC("Thread::state_string(): Invalid state: {}", (int)state());
}

void Thread::finalize()
{
    VERIFY(Thread::current() == g_finalizer);
    VERIFY(Thread::current() != this);

#if LOCK_DEBUG
    VERIFY(!m_lock.own_lock());
    if (lock_count() > 0) {
        dbgln("Thread {} leaking {} Locks!", *this, lock_count());
        ScopedSpinLock list_lock(m_holding_locks_lock);
        for (auto& info : m_holding_locks_list) {
            const auto& location = info.source_location;
            dbgln(" - Mutex: \"{}\" @ {} locked in function \"{}\" at \"{}:{}\" with a count of: {}", info.lock->name(), info.lock, location.function_name(), location.filename(), location.line_number(), info.count);
        }
        VERIFY_NOT_REACHED();
    }
#endif

    {
        ScopedSpinLock lock(g_scheduler_lock);
        dbgln_if(THREAD_DEBUG, "Finalizing thread {}", *this);
        set_state(Thread::State::Dead);
        m_join_condition.thread_finalizing();
    }

    if (m_dump_backtrace_on_finalization)
        dbgln("{}", backtrace());

    drop_thread_count(false);
}

void Thread::drop_thread_count(bool initializing_first_thread)
{
    bool is_last = process().remove_thread(*this);

    if (!initializing_first_thread && is_last)
        process().finalize();
}

void Thread::finalize_dying_threads()
{
    VERIFY(Thread::current() == g_finalizer);
    Vector<Thread*, 32> dying_threads;
    {
        ScopedSpinLock lock(g_scheduler_lock);
        for_each_in_state(Thread::State::Dying, [&](Thread& thread) {
            if (thread.is_finalizable())
                dying_threads.append(&thread);
        });
    }
    for (auto* thread : dying_threads) {
        RefPtr<Process> process = thread->process();
        dbgln_if(PROCESS_DEBUG, "Before finalization, {} has {} refs and its process has {}",
            *thread, thread->ref_count(), thread->process().ref_count());
        thread->finalize();
        dbgln_if(PROCESS_DEBUG, "After finalization, {} has {} refs and its process has {}",
            *thread, thread->ref_count(), thread->process().ref_count());
        // This thread will never execute again, drop the running reference
        // NOTE: This may not necessarily drop the last reference if anything
        //       else is still holding onto this thread!
        thread->unref();
    }
}

void Thread::update_time_scheduled(u64 current_scheduler_time, bool is_kernel, bool no_longer_running)
{
    if (m_last_time_scheduled.has_value()) {
        u64 delta;
        if (current_scheduler_time >= m_last_time_scheduled.value())
            delta = current_scheduler_time - m_last_time_scheduled.value();
        else
            delta = m_last_time_scheduled.value() - current_scheduler_time; // the unlikely event that the clock wrapped
        if (delta != 0) {
            // Add it to the global total *before* updating the thread's value!
            Scheduler::add_time_scheduled(delta, is_kernel);

            auto& total_time = is_kernel ? m_total_time_scheduled_kernel : m_total_time_scheduled_user;
            ScopedSpinLock scheduler_lock(g_scheduler_lock);
            total_time += delta;
        }
    }
    if (no_longer_running)
        m_last_time_scheduled = {};
    else
        m_last_time_scheduled = current_scheduler_time;
}

bool Thread::tick()
{
    if (previous_mode() == PreviousMode::KernelMode) {
        ++m_process->m_ticks_in_kernel;
        ++m_ticks_in_kernel;
    } else {
        ++m_process->m_ticks_in_user;
        ++m_ticks_in_user;
    }
    return --m_ticks_left;
}

void Thread::check_dispatch_pending_signal()
{
    auto result = DispatchSignalResult::Continue;
    {
        ScopedSpinLock scheduler_lock(g_scheduler_lock);
        if (pending_signals_for_state()) {
            ScopedSpinLock lock(m_lock);
            result = dispatch_one_pending_signal();
        }
    }

    switch (result) {
    case DispatchSignalResult::Yield:
        yield_without_releasing_big_lock();
        break;
    default:
        break;
    }
}

u32 Thread::pending_signals() const
{
    ScopedSpinLock lock(g_scheduler_lock);
    return pending_signals_for_state();
}

u32 Thread::pending_signals_for_state() const
{
    VERIFY(g_scheduler_lock.own_lock());
    constexpr u32 stopped_signal_mask = (1 << (SIGCONT - 1)) | (1 << (SIGKILL - 1)) | (1 << (SIGTRAP - 1));
    if (is_handling_page_fault())
        return 0;
    return m_state != Stopped ? m_pending_signals : m_pending_signals & stopped_signal_mask;
}

void Thread::send_signal(u8 signal, [[maybe_unused]] Process* sender)
{
    VERIFY(signal < 32);
    ScopedSpinLock scheduler_lock(g_scheduler_lock);

    if (should_ignore_signal(signal)) {
        dbgln_if(SIGNAL_DEBUG, "Signal {} was ignored by {}", signal, process());
        return;
    }

    if constexpr (SIGNAL_DEBUG) {
        if (sender)
            dbgln("Signal: {} sent {} to {}", *sender, signal, process());
        else
            dbgln("Signal: Kernel send {} to {}", signal, process());
    }

    m_pending_signals |= 1 << (signal - 1);
    m_have_any_unmasked_pending_signals.store(pending_signals_for_state() & ~m_signal_mask, Base::memory_order_release);

    if (m_state == Stopped) {
        ScopedSpinLock lock(m_lock);
        if (pending_signals_for_state()) {
            dbgln_if(SIGNAL_DEBUG, "Signal: Resuming stopped {} to deliver signal {}", *this, signal);
            resume_from_stopped();
        }
    } else {
        ScopedSpinLock block_lock(m_block_lock);
        dbgln_if(SIGNAL_DEBUG, "Signal: Unblocking {} to deliver signal {}", *this, signal);
        unblock(signal);
    }
}

u32 Thread::update_signal_mask(u32 signal_mask)
{
    ScopedSpinLock lock(g_scheduler_lock);
    auto previous_signal_mask = m_signal_mask;
    m_signal_mask = signal_mask;
    m_have_any_unmasked_pending_signals.store(pending_signals_for_state() & ~m_signal_mask, Base::memory_order_release);
    return previous_signal_mask;
}

u32 Thread::signal_mask() const
{
    ScopedSpinLock lock(g_scheduler_lock);
    return m_signal_mask;
}

u32 Thread::signal_mask_block(sigset_t signal_set, bool block)
{
    ScopedSpinLock lock(g_scheduler_lock);
    auto previous_signal_mask = m_signal_mask;
    if (block)
        m_signal_mask &= ~signal_set;
    else
        m_signal_mask |= signal_set;
    m_have_any_unmasked_pending_signals.store(pending_signals_for_state() & ~m_signal_mask, Base::memory_order_release);
    return previous_signal_mask;
}

void Thread::clear_signals()
{
    ScopedSpinLock lock(g_scheduler_lock);
    m_signal_mask = 0;
    m_pending_signals = 0;
    m_have_any_unmasked_pending_signals.store(false, Base::memory_order_release);
    m_signal_action_data.fill({});
}

void Thread::send_urgent_signal_to_self(u8 signal)
{
    VERIFY(Thread::current() == this);
    DispatchSignalResult result;
    {
        ScopedSpinLock lock(g_scheduler_lock);
        result = dispatch_signal(signal);
    }
    if (result == DispatchSignalResult::Yield)
        yield_and_release_relock_big_lock();
}

DispatchSignalResult Thread::dispatch_one_pending_signal()
{
    VERIFY(m_lock.own_lock());
    u32 signal_candidates = pending_signals_for_state() & ~m_signal_mask;
    if (signal_candidates == 0)
        return DispatchSignalResult::Continue;

    u8 signal = 1;
    for (; signal < 32; ++signal) {
        if (signal_candidates & (1 << (signal - 1))) {
            break;
        }
    }
    return dispatch_signal(signal);
}

DispatchSignalResult Thread::try_dispatch_one_pending_signal(u8 signal)
{
    VERIFY(signal != 0);
    ScopedSpinLock scheduler_lock(g_scheduler_lock);
    ScopedSpinLock lock(m_lock);
    u32 signal_candidates = pending_signals_for_state() & ~m_signal_mask;
    if (!(signal_candidates & (1 << (signal - 1))))
        return DispatchSignalResult::Continue;
    return dispatch_signal(signal);
}

enum class DefaultSignalAction {
    Terminate,
    Ignore,
    DumpCore,
    Stop,
    Continue,
};

static DefaultSignalAction default_signal_action(u8 signal)
{
    VERIFY(signal && signal < NSIG);

    switch (signal) {
    case SIGHUP:
    case SIGINT:
    case SIGKILL:
    case SIGPIPE:
    case SIGALRM:
    case SIGUSR1:
    case SIGUSR2:
    case SIGVTALRM:
    case SIGSTKFLT:
    case SIGIO:
    case SIGPROF:
    case SIGTERM:
        return DefaultSignalAction::Terminate;
    case SIGCHLD:
    case SIGURG:
    case SIGWINCH:
    case SIGINFO:
        return DefaultSignalAction::Ignore;
    case SIGQUIT:
    case SIGILL:
    case SIGTRAP:
    case SIGABRT:
    case SIGBUS:
    case SIGFPE:
    case SIGSEGV:
    case SIGXCPU:
    case SIGXFSZ:
    case SIGSYS:
        return DefaultSignalAction::DumpCore;
    case SIGCONT:
        return DefaultSignalAction::Continue;
    case SIGSTOP:
    case SIGTSTP:
    case SIGTTIN:
    case SIGTTOU:
        return DefaultSignalAction::Stop;
    }
    VERIFY_NOT_REACHED();
}

bool Thread::should_ignore_signal(u8 signal) const
{
    VERIFY(signal < 32);
    auto& action = m_signal_action_data[signal];
    if (action.handler_or_sigaction.is_null())
        return default_signal_action(signal) == DefaultSignalAction::Ignore;
    if (action.handler_or_sigaction.as_ptr() == SIG_IGN)
        return true;
    return false;
}

bool Thread::has_signal_handler(u8 signal) const
{
    VERIFY(signal < 32);
    auto& action = m_signal_action_data[signal];
    return !action.handler_or_sigaction.is_null();
}

static bool push_value_on_user_stack(FlatPtr& stack, FlatPtr data)
{
    stack -= sizeof(FlatPtr);
    return copy_to_user((FlatPtr*)stack, &data);
}

void Thread::resume_from_stopped()
{
    VERIFY(is_stopped());
    VERIFY(m_stop_state != State::Invalid);
    VERIFY(g_scheduler_lock.own_lock());
    if (m_stop_state == Blocked) {
        ScopedSpinLock block_lock(m_block_lock);
        if (m_blocker || m_blocking_lock) {
            // Hasn't been unblocked yet
            set_state(Blocked, 0);
        } else {
            // Was unblocked while stopped
            set_state(Runnable);
        }
    } else {
        set_state(m_stop_state, 0);
    }
}

DispatchSignalResult Thread::dispatch_signal(u8 signal)
{
    VERIFY_INTERRUPTS_DISABLED();
    VERIFY(g_scheduler_lock.own_lock());
    VERIFY(signal > 0 && signal <= 32);
    VERIFY(process().is_user_process());
    VERIFY(this == Thread::current());

    dbgln_if(SIGNAL_DEBUG, "Dispatch signal {} to {}, state: {}", signal, *this, state_string());

    if (m_state == Invalid || !is_initialized()) {
        // Thread has barely been created, we need to wait until it is
        // at least in Runnable state and is_initialized() returns true,
        // which indicates that it is fully set up an we actually have
        // a register state on the stack that we can modify
        return DispatchSignalResult::Deferred;
    }

    VERIFY(previous_mode() == PreviousMode::UserMode);

    auto& action = m_signal_action_data[signal];

    VERIFY(!(action.flags & SA_SIGINFO));

    // Mark this signal as handled.
    m_pending_signals &= ~(1 << (signal - 1));
    m_have_any_unmasked_pending_signals.store(m_pending_signals & ~m_signal_mask, Base::memory_order_release);

    auto& process = this->process();
    auto tracer = process.tracer();
    if (signal == SIGSTOP || (tracer && default_signal_action(signal) == DefaultSignalAction::DumpCore)) {
        dbgln_if(SIGNAL_DEBUG, "Signal {} stopping this thread", signal);
        set_state(State::Stopped, signal);
        return DispatchSignalResult::Yield;
    }

    if (signal == SIGCONT) {
        dbgln("signal: SIGCONT resuming {}", *this);
    } else {
        if (tracer) {
            // when a thread is traced, it should be stopped whenever it receives a signal
            // the tracer is notified of this by using waitpid()
            // only "pending signals" from the tracer are sent to the tracee
            if (!tracer->has_pending_signal(signal)) {
                dbgln("signal: {} stopping {} for tracer", signal, *this);
                set_state(Stopped, signal);
                return DispatchSignalResult::Yield;
            }
            tracer->unset_signal(signal);
        }
    }

    auto handler_vaddr = action.handler_or_sigaction;
    if (handler_vaddr.is_null()) {
        switch (default_signal_action(signal)) {
        case DefaultSignalAction::Stop:
            set_state(Stopped, signal);
            return DispatchSignalResult::Yield;
        case DefaultSignalAction::DumpCore:
            process.set_dump_core(true);
            process.for_each_thread([](auto& thread) {
                thread.set_dump_backtrace_on_finalization();
            });
            [[fallthrough]];
        case DefaultSignalAction::Terminate:
            m_process->terminate_due_to_signal(signal);
            return DispatchSignalResult::Terminate;
        case DefaultSignalAction::Ignore:
            VERIFY_NOT_REACHED();
        case DefaultSignalAction::Continue:
            return DispatchSignalResult::Continue;
        }
        VERIFY_NOT_REACHED();
    }

    if (handler_vaddr.as_ptr() == SIG_IGN) {
        dbgln_if(SIGNAL_DEBUG, "Ignored signal {}", signal);
        return DispatchSignalResult::Continue;
    }

    VERIFY(previous_mode() == PreviousMode::UserMode);
    VERIFY(current_trap());

    ProcessPagingScope paging_scope(m_process);

    u32 old_signal_mask = m_signal_mask;
    u32 new_signal_mask = action.mask;
    if (action.flags & SA_NODEFER)
        new_signal_mask &= ~(1 << (signal - 1));
    else
        new_signal_mask |= 1 << (signal - 1);

    m_signal_mask |= new_signal_mask;
    m_have_any_unmasked_pending_signals.store(m_pending_signals & ~m_signal_mask, Base::memory_order_release);

    auto setup_stack = [&](RegisterState& state) {
#if ARCH(I386)
        FlatPtr stack = state.userspace_esp;
        FlatPtr old_esp = stack;
        FlatPtr ret_eip = state.eip;
        FlatPtr ret_eflags = state.eflags;

        dbgln_if(SIGNAL_DEBUG, "Setting up user stack to return to EIP {:p}, ESP {:p}", ret_eip, old_esp);
#elif ARCH(X86_64)
        FlatPtr stack = state.userspace_rsp;
        FlatPtr old_rsp = stack;
        FlatPtr ret_rip = state.rip;
        FlatPtr ret_rflags = state.rflags;

        dbgln_if(SIGNAL_DEBUG, "Setting up user stack to return to RIP {:p}, RSP {:p}", ret_rip, old_rsp);
#endif

#if ARCH(I386)
        // Align the stack to 16 bytes.
        // Note that we push 56 bytes (4 * 14) on to the stack,
        // so we need to account for this here.
        // 56 % 16 = 8, so we only need to take 8 bytes into consideration for
        // the stack alignment.
        FlatPtr stack_alignment = (stack - 8) % 16;
        stack -= stack_alignment;

        push_value_on_user_stack(stack, ret_eflags);

        push_value_on_user_stack(stack, ret_eip);
        push_value_on_user_stack(stack, state.eax);
        push_value_on_user_stack(stack, state.ecx);
        push_value_on_user_stack(stack, state.edx);
        push_value_on_user_stack(stack, state.ebx);
        push_value_on_user_stack(stack, old_esp);
        push_value_on_user_stack(stack, state.ebp);
        push_value_on_user_stack(stack, state.esi);
        push_value_on_user_stack(stack, state.edi);
#else
        // Align the stack to 16 bytes.
        // Note that we push 176 bytes (8 * 22) on to the stack,
        // so we need to account for this here.
        // 22 % 2 = 0, so we dont need to take anything into consideration
        // for the alignment.
        // We also are not allowed to touch the thread's red-zone of 128 bytes
        FlatPtr stack_alignment = stack % 16;
        stack -= 128 + stack_alignment;

        push_value_on_user_stack(stack, ret_rflags);

        push_value_on_user_stack(stack, ret_rip);
        push_value_on_user_stack(stack, state.r15);
        push_value_on_user_stack(stack, state.r14);
        push_value_on_user_stack(stack, state.r13);
        push_value_on_user_stack(stack, state.r12);
        push_value_on_user_stack(stack, state.r11);
        push_value_on_user_stack(stack, state.r10);
        push_value_on_user_stack(stack, state.r9);
        push_value_on_user_stack(stack, state.r8);
        push_value_on_user_stack(stack, state.rax);
        push_value_on_user_stack(stack, state.rcx);
        push_value_on_user_stack(stack, state.rdx);
        push_value_on_user_stack(stack, state.rbx);
        push_value_on_user_stack(stack, old_rsp);
        push_value_on_user_stack(stack, state.rbp);
        push_value_on_user_stack(stack, state.rsi);
        push_value_on_user_stack(stack, state.rdi);
#endif

        // PUSH old_signal_mask
        push_value_on_user_stack(stack, old_signal_mask);

        push_value_on_user_stack(stack, signal);
        push_value_on_user_stack(stack, handler_vaddr.get());
        push_value_on_user_stack(stack, 0); // push fake return address

        // We write back the adjusted stack value into the register state.
        // We have to do this because we can't just pass around a reference to a packed field, as it's UB.
#if ARCH(I386)
        state.userspace_esp = stack;
#else
        state.userspace_rsp = stack;
#endif

        VERIFY((stack % 16) == 0);
    };

    // We now place the thread state on the userspace stack.
    // Note that we use a RegisterState.
    // Conversely, when the thread isn't blocking the RegisterState may not be
    // valid (fork, exec etc) but the tss will, so we use that instead.
    auto& regs = get_register_dump_from_stack();
    setup_stack(regs);
    auto signal_trampoline_addr = process.signal_trampoline().get();
    regs.set_ip_reg(signal_trampoline_addr);

    dbgln_if(SIGNAL_DEBUG, "Thread in state '{}' has been primed with signal handler {:#04x}:{:p} to deliver {}", state_string(), m_regs.cs, m_regs.ip(), signal);

    return DispatchSignalResult::Continue;
}

RegisterState& Thread::get_register_dump_from_stack()
{
    auto* trap = current_trap();

    // We should *always* have a trap. If we don't we're probably a kernel
    // thread that hasn't been pre-empted. If we want to support this, we
    // need to capture the registers probably into m_regs and return it
    VERIFY(trap);

    while (trap) {
        if (!trap->next_trap)
            break;
        trap = trap->next_trap;
    }
    return *trap->regs;
}

RefPtr<Thread> Thread::clone(Process& process)
{
    auto thread_or_error = Thread::try_create(process);
    if (thread_or_error.is_error())
        return {};
    auto& clone = thread_or_error.value();
    auto signal_action_data_span = m_signal_action_data.span();
    signal_action_data_span.copy_to(clone->m_signal_action_data.span());
    clone->m_signal_mask = m_signal_mask;
    clone->m_fpu_state = m_fpu_state;
    clone->m_thread_specific_data = m_thread_specific_data;
    return clone;
}

void Thread::set_state(State new_state, u8 stop_signal)
{
    State previous_state;
    VERIFY(g_scheduler_lock.own_lock());
    if (new_state == m_state)
        return;

    {
        ScopedSpinLock thread_lock(m_lock);
        previous_state = m_state;
        if (previous_state == Invalid) {
            // If we were *just* created, we may have already pending signals
            if (has_unmasked_pending_signals()) {
                dbgln_if(THREAD_DEBUG, "Dispatch pending signals to new thread {}", *this);
                dispatch_one_pending_signal();
            }
        }

        m_state = new_state;
        dbgln_if(THREAD_DEBUG, "Set thread {} state to {}", *this, state_string());
    }

    if (previous_state == Runnable) {
        Scheduler::dequeue_runnable_thread(*this);
    } else if (previous_state == Stopped) {
        m_stop_state = State::Invalid;
        auto& process = this->process();
        if (process.set_stopped(false) == true) {
            process.for_each_thread([&](auto& thread) {
                if (&thread == this)
                    return;
                if (!thread.is_stopped())
                    return;
                dbgln_if(THREAD_DEBUG, "Resuming peer thread {}", thread);
                thread.resume_from_stopped();
            });
            process.unblock_waiters(Thread::WaitBlocker::UnblockFlags::Continued);
            // Tell the parent process (if any) about this change.
            if (auto parent = Process::from_pid(process.ppid())) {
                [[maybe_unused]] auto result = parent->send_signal(SIGCHLD, &process);
            }
        }
    }

    if (m_state == Runnable) {
        Scheduler::enqueue_runnable_thread(*this);
        Processor::smp_wake_n_idle_processors(1);
    } else if (m_state == Stopped) {
        // We don't want to restore to Running state, only Runnable!
        m_stop_state = previous_state != Running ? previous_state : Runnable;
        auto& process = this->process();
        if (process.set_stopped(true) == false) {
            process.for_each_thread([&](auto& thread) {
                if (&thread == this)
                    return;
                if (thread.is_stopped())
                    return;
                dbgln_if(THREAD_DEBUG, "Stopping peer thread {}", thread);
                thread.set_state(Stopped, stop_signal);
            });
            process.unblock_waiters(Thread::WaitBlocker::UnblockFlags::Stopped, stop_signal);
            // Tell the parent process (if any) about this change.
            if (auto parent = Process::from_pid(process.ppid())) {
                [[maybe_unused]] auto result = parent->send_signal(SIGCHLD, &process);
            }
        }
    } else if (m_state == Dying) {
        VERIFY(previous_state != Blocked);
        if (this != Thread::current() && is_finalizable()) {
            // Some other thread set this thread to Dying, notify the
            // finalizer right away as it can be cleaned up now
            Scheduler::notify_finalizer();
        }
    }
}

struct RecognizedSymbol {
    FlatPtr address;
    const KernelSymbol* symbol { nullptr };
};

static bool symbolicate(RecognizedSymbol const& symbol, Process& process, StringBuilder& builder)
{
    if (!symbol.address)
        return false;

    bool mask_kernel_addresses = !process.is_superuser();
    if (!symbol.symbol) {
        if (!Memory::is_user_address(VirtualAddress(symbol.address))) {
            builder.append("0xdeadc0de\n");
        } else {
            if (auto* region = process.address_space().find_region_containing({ VirtualAddress(symbol.address), sizeof(FlatPtr) })) {
                size_t offset = symbol.address - region->vaddr().get();
                if (auto region_name = region->name(); !region_name.is_null() && !region_name.is_empty())
                    builder.appendff("{:p}  {} + {:#x}\n", (void*)symbol.address, region_name, offset);
                else
                    builder.appendff("{:p}  {:p} + {:#x}\n", (void*)symbol.address, region->vaddr().as_ptr(), offset);
            } else {
                builder.appendff("{:p}\n", symbol.address);
            }
        }
        return true;
    }
    unsigned offset = symbol.address - symbol.symbol->address;
    if (symbol.symbol->address == g_highest_kernel_symbol_address && offset > 4096) {
        builder.appendff("{:p}\n", (void*)(mask_kernel_addresses ? 0xdeadc0de : symbol.address));
    } else {
        builder.appendff("{:p}  {} + {:#x}\n", (void*)(mask_kernel_addresses ? 0xdeadc0de : symbol.address), symbol.symbol->name, offset);
    }
    return true;
}

String Thread::backtrace()
{
    Vector<RecognizedSymbol, 128> recognized_symbols;

    auto& process = const_cast<Process&>(this->process());
    auto stack_trace = Processor::capture_stack_trace(*this);
    VERIFY(!g_scheduler_lock.own_lock());
    ProcessPagingScope paging_scope(process);
    for (auto& frame : stack_trace) {
        if (Memory::is_user_range(VirtualAddress(frame), sizeof(FlatPtr) * 2)) {
            recognized_symbols.append({ frame });
        } else {
            recognized_symbols.append({ frame, symbolicate_kernel_address(frame) });
        }
    }

    StringBuilder builder;
    for (auto& symbol : recognized_symbols) {
        if (!symbolicate(symbol, process, builder))
            break;
    }
    return builder.to_string();
}

size_t Thread::thread_specific_region_alignment() const
{
    return max(process().m_master_tls_alignment, alignof(ThreadSpecificData));
}

size_t Thread::thread_specific_region_size() const
{
    return align_up_to(process().m_master_tls_size, thread_specific_region_alignment()) + sizeof(ThreadSpecificData);
}

KResult Thread::make_thread_specific_region(Badge<Process>)
{
    // The process may not require a TLS region, or allocate TLS later with sys$allocate_tls (which is what dynamically loaded programs do)
    if (!process().m_master_tls_region)
        return KSuccess;

    auto range = process().address_space().allocate_range({}, thread_specific_region_size());
    if (!range.has_value())
        return ENOMEM;

    auto region_or_error = process().address_space().allocate_region(range.value(), "Thread-specific", PROT_READ | PROT_WRITE);
    if (region_or_error.is_error())
        return region_or_error.error();

    m_thread_specific_range = range.value();

    SmapDisabler disabler;
    auto* thread_specific_data = (ThreadSpecificData*)region_or_error.value()->vaddr().offset(align_up_to(process().m_master_tls_size, thread_specific_region_alignment())).as_ptr();
    auto* thread_local_storage = (u8*)((u8*)thread_specific_data) - align_up_to(process().m_master_tls_size, process().m_master_tls_alignment);
    m_thread_specific_data = VirtualAddress(thread_specific_data);
    thread_specific_data->self = thread_specific_data;

    if (process().m_master_tls_size)
        memcpy(thread_local_storage, process().m_master_tls_region.unsafe_ptr()->vaddr().as_ptr(), process().m_master_tls_size);

    return KSuccess;
}

RefPtr<Thread> Thread::from_tid(ThreadID tid)
{
    RefPtr<Thread> found_thread;
    {
        ScopedSpinLock lock(g_tid_map_lock);
        if (auto it = g_tid_map->find(tid); it != g_tid_map->end()) {
            // We need to call try_ref() here as there is a window between
            // dropping the last reference and calling the Thread's destructor!
            // We shouldn't remove the threads from that list until it is truly
            // destructed as it may stick around past finalization in order to
            // be able to wait() on it!
            if (it->value->try_ref()) {
                found_thread = adopt_ref(*it->value);
            }
        }
    }
    return found_thread;
}

void Thread::reset_fpu_state()
{
    memcpy(&m_fpu_state, &Processor::current().clean_fpu_state(), sizeof(FPUState));
}

bool Thread::should_be_stopped() const
{
    return process().is_stopped();
}

}

void Base::Formatter<Kernel::Thread>::format(FormatBuilder& builder, const Kernel::Thread& value)
{
    return Base::Formatter<FormatString>::format(
        builder,
        "{}({}:{})", value.process().name(), value.pid().value(), value.tid().value());
}