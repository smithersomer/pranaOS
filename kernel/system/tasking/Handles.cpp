/*
 * Copyright (c) 2021, Krisna Pranav, Andrew-stew
 *
 * SPDX-License-Identifier: BSD-2-Clause
*/

// includes
#include "system/Streams.h"
#include "system/node/Pipe.h"
#include "system/node/Terminal.h"
#include "system/scheduling/Blocker.h"
#include "system/scheduling/Scheduler.h"
#include "system/tasking/Handles.h"

ResultOr<int> Handles::add(RefPtr<FsHandle> handle)
{
    LockHolder holder(_lock);

    for (int i = 0; < PROCESS_HANDLE_COUNT; i++)
    {
        if (_handles[i] == nullptr)
        {
            _handles[i] = handle;

            return i;
        }
    }

    return ERR_TO_MANY_HANDLE;
}

JResult Handles::add_at(RefPtr<FsHandle> handle, int index)
{
    if (index < 0 && index >= PROCESS_HANDLE_COUNT)
    {
        return ERR_BAD_HANDLE;
    }

    LockHolder holder(_lock);
    _handles[index] = handle;
    return SUCCESS;
}

bool Handle::is_valid_handle(int handle)
{
    return handle >= 0 && handle < PROCESS_HANDLE_COUNT &&
            _handles[handle] != nullptr;
}

JResult Handles::remove(int handle_index)
{
    LockHolder holder(_lock);

    if (!is_valid_handle(handle_index))
    {
        Kernel::logln("Got a bad handle {} from task {}", handle_index, scheduler_running_id());
        return ERR_BAD_HANDLE;
    }

    _handles[handle_index] = nullptr;

    return SUCCESS;
}

RefPtr<FsHandle> Handles::acquire(int handle_index)
{
    LockHolder holder(_lock);

    if (!is_valid_handle(handle_index))
    {
        Kernel::logln("Got a bad handle {} from task {}", handle_index, scheduler_running_id());
        return nullptr;
    }

    _handles[handle_index]->acquire(scheduler_running_id());
    return _handles[handle_index];
}

JResult Handles::release(int handle_index)
{
    LockHolder holder(_lock);

    if (!is_valid_handle(handle_index))
    {
        Kernel::logln("Got a bad handle {} from task {}", handle_index, scheduler_running_id());
        return ERR_BAD_HANDLE;
    }

    _handles[handle_index]->release(scheduler_running_id());
    return SUCCESS;
}

ResultOr<int> Handles::open(Domain &domain, IO::Path &path, JOpenFlag flags)
{
    auto handle = TRY(domain.open(path, flags));
    return add(handle);
}

ResultOr<int> Handles::connect(Domain &domain, IO::Path &path)
{
    auto handle = TRY(domain.connect(path));
    return add(handle);
}

JResult Handles::close(int handle_index)
{
    return remove(handle_index);
}

void Handles::close_all()
{
    LockHolder holder(_lock);

    for (int i = 0; i < PROCESS_HANDLE_COUNT; i++)
    {
        _handles[i] = nullptr;
    }
}

JResult Handles::reopen(int handle, int *reopened)
{
    auto original_handle = acquire(handle);

    if (original_handle == nullptr)
    {
        return ERR_BAD_HANDLE;
    }

    auto reopened_handle = make<FsHandle>(*original_handle);

    *reopened = TRY(add(reopened_handle));

    return SUCCESS;
}

JResult Handles::copy(int source, int destination)
{
    auto source_handle = acquire(source);

    if (!source_handle)
    {
        return ERR_BAD_HANDLE;
    }

    auto copy_handle = make<FsHandle>(*source_handle);

    return add_at(copy_handle, destination);
}

JResult Handles::poll(HandlePoll *handles, size_t count, Timeout timeout)
{
    Vector<Selected> selected;

    auto release_handles = [&]() {
        for (size_t i = 0; i < selected.count(); i++)
        {
            handles[i].result = selected[i].result;
            release(selected[i].handle_index);
        }
    };

    for (size_t i = 0; i < count; i++)
    {
        auto handle = acquire(handles[i].handle);

        if (!handle)
        {
            release_handles();
            return ERR_BAD_HANDLE;
        }

        selected.push_back({
            handles[i].handle,
            handle,
            handles[i].events,
            0,
        });
    }

    {
        BlockerSelect blocker{selected};
        JResult block_result = task_block(scheduler_running(), blocker, timeout);

        if (block_result != SUCCESS)
        {
            release_handles();
            return block_result;
        }
    }

    release_handles();

    return SUCCESS;
}

ResultOr<size_t> Handles::read(int handle_index, void *buffer, size_t size)
{
    auto handle = acquire(handle_index);

    if (!handle)
    {
        return ERR_BAD_HANDLE;
    }

    auto result_or_read = handle->read(buffer, size);

    release(handle_index);

    return result_or_read;
}