/*
 * Copyright (c) 2021, Krisna Pranav
 *
 * SPDX-License-Identifier: BSD-2-Clause
*/

#pragma once

// includes
#include <kernel/devices/CharacterDevice.h>

namespace Kernel {

class NullDevice final : public CharacterDevice {
    BASE_MAKE_ETERNAL
public:
    NullDevice();
    virtual ~NullDevice() override;

    static void initialize();
    static NullDevice& the();

    virtual mode_t required_mode() const override { return 0666; }
    virtual String device_name() const override { return "null"; }

private:

    virtual KResultOr<size_t> read(FileDescription&, u64, UserOrKernelBuffer&, size_t) override;
    virtual KResultOr<size_t> write(FileDescription&, u64, const UserOrKernelBuffer&, size_t) override;
    virtual bool can_write(const FileDescription&, size_t) const override { return true; }
    virtual bool can_read(const FileDescription&, size_t) const override;
    virtual StringView class_name() const override { return "NullDevice"; }
    virtual bool is_seekable() const override { return true; }
};

}