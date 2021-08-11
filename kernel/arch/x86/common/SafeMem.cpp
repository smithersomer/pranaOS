/*
 * Copyright (c) 2021, Krisna Pranav
 *
 * SPDX-License-Identifier: BSD-2-Clause
*/

// includes
#include <kernel/arch/x86/RegisterState.h>
#include <kernel/arch/x86/SafeMem.h>

#define CODE_SECTION(section_name) __attribute__((section(section_name)))

extern "C" u8 start_of_safemem_text[];
extern "C" u8 end_of_safemem_text[];

extern "C" u8 safe_memcpy_ins_1[];
extern "C" u8 safe_memcpy_1_faulted[];
extern "C" u8 safe_memcpy_ins_2[];
extern "C" u8 safe_memcpy_2_faulted[];
extern "C" u8 safe_strnlen_ins[];
extern "C" u8 safe_strnlen_faulted[];
extern "C" u8 safe_memset_ins_1[];
extern "C" u8 safe_memset_1_faulted[];
extern "C" u8 safe_memset_ins_2[];
extern "C" u8 safe_memset_2_faulted[];

extern "C" u8 start_of_safemem_atomic_text[];
extern "C" u8 end_of_safemem_atomic_text[];

extern "C" u8 safe_atomic_fetch_add_relaxed_ins[];
extern "C" u8 safe_atomic_fetch_add_relaxed_faulted[];
extern "C" u8 safe_atomic_exchange_relaxed_ins[];
extern "C" u8 safe_atomic_exchange_relaxed_faulted[];
extern "C" u8 safe_atomic_load_relaxed_ins[];
extern "C" u8 safe_atomic_load_relaxed_faulted[];
extern "C" u8 safe_atomic_store_relaxed_ins[];
extern "C" u8 safe_atomic_store_relaxed_faulted[];
extern "C" u8 safe_atomic_compare_exchange_relaxed_ins[];
extern "C" u8 safe_atomic_compare_exchange_relaxed_faulted[];

namespace Kernel {

CODE_SECTION(".text.safemem")
NEVER_INLINE bool safe_memcpy(void* dest_ptr, const void* src_ptr, size_t n, void*& fault_at)
{
    fault_at = nullptr;
    size_t dest = (size_t)dest_ptr;
    size_t src = (size_t)src_ptr;
    size_t remainder;

    if (!(dest & 0x3) && !(src & 0x3) && n >= 12) {
        size_t size_ts = n / sizeof(size_t);
        asm volatile(
            ".globl safe_memcpy_ins_1 \n"
            "safe_memcpy_ins_1: \n"
#if ARCH(I386)
            "rep movsl \n"
#else
            "rep movsq \n"
#endif
            ".globl safe_memcpy_1_faulted \n"
            "safe_memcpy_1_faulted: \n" 
            : "=S"(src),
            "=D"(dest),
            "=c"(remainder),
            [fault_at] "=d"(fault_at)
            : "S"(src),
            "D"(dest),
            "c"(size_ts)
            : "memory");
        if (remainder != 0)
            return false; 
        n -= size_ts * sizeof(size_t);
        if (n == 0) {
            fault_at = nullptr;
            return true;
        }
    }
    asm volatile(
        ".globl safe_memcpy_ins_2 \n"
        "safe_memcpy_ins_2: \n"
        "rep movsb \n"
        ".globl safe_memcpy_2_faulted \n"
        "safe_memcpy_2_faulted: \n" 
        : "=c"(remainder),
        [fault_at] "=d"(fault_at)
        : "S"(src),
        "D"(dest),
        "c"(n)
        : "memory");
    if (remainder != 0)
        return false; 
    fault_at = nullptr;
    return true;
}

CODE_SECTION(".text.safemem")
NEVER_INLINE ssize_t safe_strnlen(const char* str, size_t max_n, void*& fault_at)
{
    ssize_t count = 0;
    fault_at = nullptr;
    asm volatile(
        "1: \n"
        "test %[max_n], %[max_n] \n"
        "je 2f \n"
        "dec %[max_n] \n"
        ".globl safe_strnlen_ins \n"
        "safe_strnlen_ins: \n"
        "cmpb $0,(%[str], %[count], 1) \n"
        "je 2f \n"
        "inc %[count] \n"
        "jmp 1b \n"
        ".globl safe_strnlen_faulted \n"
        "safe_strnlen_faulted: \n" 
        "xor %[count_on_error], %[count_on_error] \n"
        "dec %[count_on_error] \n" 
        "2:"
        : [count_on_error] "=c"(count),
        [fault_at] "=d"(fault_at)
        : [str] "b"(str),
        [count] "c"(count),
        [max_n] "d"(max_n));
    if (count >= 0)
        fault_at = nullptr;
    return count;
}

CODE_SECTION(".text.safemem")
NEVER_INLINE bool safe_memset(void* dest_ptr, int c, size_t n, void*& fault_at)
{
    fault_at = nullptr;
    size_t dest = (size_t)dest_ptr;
    size_t remainder;
    
    if (!(dest & 0x3) && n >= 12) {
        size_t size_ts = n / sizeof(size_t);
        size_t expanded_c = (u8)c;
        expanded_c |= expanded_c << 8;
        expanded_c |= expanded_c << 16;
        asm volatile(
            ".globl safe_memset_ins_1 \n"
            "safe_memset_ins_1: \n"
#if ARCH(I386)
            "rep stosl \n"
#else
            "rep stosq \n"
#endif
            ".globl safe_memset_1_faulted \n"
            "safe_memset_1_faulted: \n" 
            : "=D"(dest),
            "=c"(remainder),
            [fault_at] "=d"(fault_at)
            : "D"(dest),
            "a"(expanded_c),
            "c"(size_ts)
            : "memory");
        if (remainder != 0)
            return false; 
        n -= size_ts * sizeof(size_t);
        if (n == 0) {
            fault_at = nullptr;
            return true;
        }
    }
    asm volatile(
        ".globl safe_memset_ins_2 \n"
        "safe_memset_ins_2: \n"
        "rep stosb \n"
        ".globl safe_memset_2_faulted \n"
        "safe_memset_2_faulted: \n" 
        : "=D"(dest),
        "=c"(remainder),
        [fault_at] "=d"(fault_at)
        : "D"(dest),
        "c"(n),
        "a"(c)
        : "memory");
    if (remainder != 0)
        return false; 
    fault_at = nullptr;
    return true;
}

CODE_SECTION(".text.safemem.atomic")
NEVER_INLINE Optional<u32> safe_atomic_fetch_add_relaxed(volatile u32* var, u32 val)
{
    u32 result;
    bool error;
    asm volatile(
        "xor %[error], %[error] \n"
        ".globl safe_atomic_fetch_add_relaxed_ins \n"
        "safe_atomic_fetch_add_relaxed_ins: \n"
        "lock xadd %[result], %[var] \n"
        ".globl safe_atomic_fetch_add_relaxed_faulted \n"
        "safe_atomic_fetch_add_relaxed_faulted: \n"
        : [error] "=d"(error), [result] "=a"(result), [var] "=m"(*var)
        : [val] "a"(val)
        : "memory");
    if (error)
        return {};
    return result;
}

CODE_SECTION(".text.safemem.atomic")
NEVER_INLINE Optional<u32> safe_atomic_exchange_relaxed(volatile u32* var, u32 val)
{
    u32 result;
    bool error;
    asm volatile(
        "xor %[error], %[error] \n"
        ".globl safe_atomic_exchange_relaxed_ins \n"
        "safe_atomic_exchange_relaxed_ins: \n"
        "xchg %[val], %[var] \n"
        ".globl safe_atomic_exchange_relaxed_faulted \n"
        "safe_atomic_exchange_relaxed_faulted: \n"
        : [error] "=d"(error), "=a"(result), [var] "=m"(*var)
        : [val] "a"(val)
        : "memory");
    if (error)
        return {};
    return result;
}

CODE_SECTION(".text.safemem.atomic")
NEVER_INLINE Optional<u32> safe_atomic_load_relaxed(volatile u32* var)
{
    u32 result;
    bool error;
    asm volatile(
        "xor %[error], %[error] \n"
        ".globl safe_atomic_load_relaxed_ins \n"
        "safe_atomic_load_relaxed_ins: \n"
        "mov (%[var]), %[result] \n"
        ".globl safe_atomic_load_relaxed_faulted \n"
        "safe_atomic_load_relaxed_faulted: \n"
        : [error] "=d"(error), [result] "=c"(result)
        : [var] "b"(var)
        : "memory");
    if (error)
        return {};
    return result;
}

CODE_SECTION(".text.safemem.atomic")
NEVER_INLINE bool safe_atomic_store_relaxed(volatile u32* var, u32 val)
{
    bool error;
    asm volatile(
        "xor %[error], %[error] \n"
        ".globl safe_atomic_store_relaxed_ins \n"
        "safe_atomic_store_relaxed_ins: \n"
        "xchg %[val], %[var] \n"
        ".globl safe_atomic_store_relaxed_faulted \n"
        "safe_atomic_store_relaxed_faulted: \n"
        : [error] "=d"(error), [var] "=m"(*var)
        : [val] "r"(val)
        : "memory");
    return !error;
}

CODE_SECTION(".text.safemem.atomic")
NEVER_INLINE Optional<bool> safe_atomic_compare_exchange_relaxed(volatile u32* var, u32& expected, u32 val)
{
    bool error;
    bool did_exchange;
    asm volatile(
        "xor %[error], %[error] \n"
        ".globl safe_atomic_compare_exchange_relaxed_ins \n"
        "safe_atomic_compare_exchange_relaxed_ins: \n"
        "lock cmpxchg %[val], %[var] \n"
        ".globl safe_atomic_compare_exchange_relaxed_faulted \n"
        "safe_atomic_compare_exchange_relaxed_faulted: \n"
        : [error] "=d"(error), "=a"(expected), [var] "=m"(*var), "=@ccz"(did_exchange)
        : "a"(expected), [val] "b"(val)
        : "memory");
    if (error)
        return {};
    return did_exchange;
}

bool handle_safe_access_fault(RegisterState& regs, FlatPtr fault_address)
{
    FlatPtr ip = regs.ip();
    ;
    if (ip >= (FlatPtr)&start_of_safemem_text && ip < (FlatPtr)&end_of_safemem_text) {

        if (ip == (FlatPtr)safe_memcpy_ins_1)
            ip = (FlatPtr)safe_memcpy_1_faulted;
        else if (ip == (FlatPtr)safe_memcpy_ins_2)
            ip = (FlatPtr)safe_memcpy_2_faulted;
        else if (ip == (FlatPtr)safe_strnlen_ins)
            ip = (FlatPtr)safe_strnlen_faulted;
        else if (ip == (FlatPtr)safe_memset_ins_1)
            ip = (FlatPtr)safe_memset_1_faulted;
        else if (ip == (FlatPtr)safe_memset_ins_2)
            ip = (FlatPtr)safe_memset_2_faulted;
        else
            return false;

#if ARCH(I386)
        regs.eip = ip;
        regs.edx = fault_address;
#else
        regs.rip = ip;
        regs.rdx = fault_address;
#endif
        return true;
    }
    if (ip >= (FlatPtr)&start_of_safemem_atomic_text && ip < (FlatPtr)&end_of_safemem_atomic_text) {

        if (ip == (FlatPtr)safe_atomic_fetch_add_relaxed_ins)
            ip = (FlatPtr)safe_atomic_fetch_add_relaxed_faulted;
        else if (ip == (FlatPtr)safe_atomic_exchange_relaxed_ins)
            ip = (FlatPtr)safe_atomic_exchange_relaxed_faulted;
        else if (ip == (FlatPtr)safe_atomic_load_relaxed_ins)
            ip = (FlatPtr)safe_atomic_load_relaxed_faulted;
        else if (ip == (FlatPtr)safe_atomic_store_relaxed_ins)
            ip = (FlatPtr)safe_atomic_store_relaxed_faulted;
        else if (ip == (FlatPtr)safe_atomic_compare_exchange_relaxed_ins)
            ip = (FlatPtr)safe_atomic_compare_exchange_relaxed_faulted;
        else
            return false;

#if ARCH(I386)
        regs.eip = ip;
        regs.edx = 1;
#else
        regs.rip = ip;
        regs.rdx = 1;
#endif
        return true;
    }
    return false;
}

}