/*
 * Copyright (c) 2021, Krisna Pranav
 *
 * SPDX-License-Identifier: BSD-2-Clause
*/

// includes
#include <base/Memory.h>
#include <base/StringView.h>
#include <kernel/Debug.h>
#include <kernel/filesystem/FileDescription.h>
#include <kernel/storage/StorageDevice.h>
#include <kernel/storage/StorageManagement.h>

namespace Kernel {

StorageDevice::StorageDevice(const StorageController& controller, size_t sector_size, u64 max_addressable_block)
    : BlockDevice(StorageManagement::major_number(), StorageManagement::minor_number(), sector_size)
    , m_storage_controller(controller)
    , m_max_addressable_block(max_addressable_block)
{
}

StorageDevice::StorageDevice(const StorageController& controller, int major, int minor, size_t sector_size, u64 max_addressable_block)
    : BlockDevice(major, minor, sector_size)
    , m_storage_controller(controller)
    , m_max_addressable_block(max_addressable_block)
{
}

StringView StorageDevice::class_name() const
{
    return "StorageDevice";
}

NonnullRefPtr<StorageController> StorageDevice::controller() const
{
    return m_storage_controller;
}

KResultOr<size_t> StorageDevice::read(FileDescription&, u64 offset, UserOrKernelBuffer& outbuf, size_t len)
{
    unsigned index = offset / block_size();
    u16 whole_blocks = len / block_size();
    size_t remaining = len % block_size();

    unsigned blocks_per_page = PAGE_SIZE / block_size();

    if (whole_blocks >= blocks_per_page) {
        whole_blocks = blocks_per_page;
        remaining = 0;
    }

    dbgln_if(STORAGE_DEVICE_DEBUG, "StorageDevice::read() index={}, whole_blocks={}, remaining={}", index, whole_blocks, remaining);

    if (whole_blocks > 0) {
        auto read_request = make_request<AsyncBlockDeviceRequest>(AsyncBlockDeviceRequest::Read, index, whole_blocks, outbuf, whole_blocks * block_size());
        auto result = read_request->wait();
        if (result.wait_result().was_interrupted())
            return EINTR;
        switch (result.request_result()) {
        case AsyncDeviceRequest::Failure:
        case AsyncDeviceRequest::Cancelled:
            return EIO;
        case AsyncDeviceRequest::MemoryFault:
            return EFAULT;
        default:
            break;
        }
    }

    off_t pos = whole_blocks * block_size();

    if (remaining > 0) {
        auto data = ByteBuffer::create_uninitialized(block_size());
        auto data_buffer = UserOrKernelBuffer::for_kernel_buffer(data.data());
        auto read_request = make_request<AsyncBlockDeviceRequest>(AsyncBlockDeviceRequest::Read, index + whole_blocks, 1, data_buffer, block_size());
        auto result = read_request->wait();
        if (result.wait_result().was_interrupted())
            return EINTR;
        switch (result.request_result()) {
        case AsyncDeviceRequest::Failure:
            return pos;
        case AsyncDeviceRequest::Cancelled:
            return EIO;
        case AsyncDeviceRequest::MemoryFault:

            VERIFY_NOT_REACHED();
        default:
            break;
        }
        if (!outbuf.write(data.data(), pos, remaining))
            return EFAULT;
    }

    return pos + remaining;
}

bool StorageDevice::can_read(const FileDescription&, size_t offset) const
{
    return offset < (max_addressable_block() * block_size());
}

KResultOr<size_t> StorageDevice::write(FileDescription&, u64 offset, const UserOrKernelBuffer& inbuf, size_t len)
{
    unsigned index = offset / block_size();
    u16 whole_blocks = len / block_size();
    size_t remaining = len % block_size();

    unsigned blocks_per_page = PAGE_SIZE / block_size();

    if (whole_blocks >= blocks_per_page) {
        whole_blocks = blocks_per_page;
        remaining = 0;
    }

    dbgln_if(STORAGE_DEVICE_DEBUG, "StorageDevice::write() index={}, whole_blocks={}, remaining={}", index, whole_blocks, remaining);

    if (whole_blocks > 0) {
        auto write_request = make_request<AsyncBlockDeviceRequest>(AsyncBlockDeviceRequest::Write, index, whole_blocks, inbuf, whole_blocks * block_size());
        auto result = write_request->wait();
        if (result.wait_result().was_interrupted())
            return EINTR;
        switch (result.request_result()) {
        case AsyncDeviceRequest::Failure:
        case AsyncDeviceRequest::Cancelled:
            return EIO;
        case AsyncDeviceRequest::MemoryFault:
            return EFAULT;
        default:
            break;
        }
    }

    off_t pos = whole_blocks * block_size();

    if (remaining > 0) {
        auto data = ByteBuffer::create_zeroed(block_size());
        auto data_buffer = UserOrKernelBuffer::for_kernel_buffer(data.data());

        {
            auto read_request = make_request<AsyncBlockDeviceRequest>(AsyncBlockDeviceRequest::Read, index + whole_blocks, 1, data_buffer, block_size());
            auto result = read_request->wait();
            if (result.wait_result().was_interrupted())
                return EINTR;
            switch (result.request_result()) {
            case AsyncDeviceRequest::Failure:
                return pos;
            case AsyncDeviceRequest::Cancelled:
                return EIO;
            case AsyncDeviceRequest::MemoryFault:

                VERIFY_NOT_REACHED();
            default:
                break;
            }
        }

        if (!inbuf.read(data.data(), pos, remaining))
            return EFAULT;

        {
            auto write_request = make_request<AsyncBlockDeviceRequest>(AsyncBlockDeviceRequest::Write, index + whole_blocks, 1, data_buffer, block_size());
            auto result = write_request->wait();
            if (result.wait_result().was_interrupted())
                return EINTR;
            switch (result.request_result()) {
            case AsyncDeviceRequest::Failure:
                return pos;
            case AsyncDeviceRequest::Cancelled:
                return EIO;
            case AsyncDeviceRequest::MemoryFault:

                VERIFY_NOT_REACHED();
            default:
                break;
            }
        }
    }

    return pos + remaining;
}

bool StorageDevice::can_write(const FileDescription&, size_t offset) const
{
    return offset < (max_addressable_block() * block_size());
}

}