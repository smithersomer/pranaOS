/*
 * Copyright (c) 2021, Krisna Pranav
 *
 * SPDX-License-Identifier: BSD-2-Clause
*/

#pragma once

// includes
#include <base/NonnullRefPtr.h>
#include <base/NonnullRefPtrVector.h>
#include <base/Types.h>
#include <kernel/tty/VirtualConsole.h>

namespace Kernel {

class ConsoleManagement {
    BASE_MAKE_ETERNAL;
    friend class VirtualConsole;

public:
    ConsoleManagement();

    static constexpr size_t s_max_virtual_consoles = 6;

    static bool is_initialized();
    static ConsoleManagement& the();

    void switch_to(unsigned);
    void initialize();

    void resolution_was_changed();

    void switch_to_debug() { switch_to(1); }

    NonnullRefPtr<VirtualConsole> first_tty() const { return m_consoles[0]; }
    NonnullRefPtr<VirtualConsole> debug_tty() const { return m_consoles[1]; }

    RecursiveSpinLock& tty_write_lock() { return m_tty_write_lock; }

private:
    NonnullRefPtrVector<VirtualConsole, s_max_virtual_consoles> m_consoles;
    VirtualConsole* m_active_console { nullptr };
    SpinLock<u8> m_lock;
    RecursiveSpinLock m_tty_write_lock;
};

};