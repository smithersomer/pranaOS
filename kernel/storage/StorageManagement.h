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
#include <kernel/filesystem/FileSystem.h>
#include <kernel/storage/Partition/DiskPartition.h>
#include <kernel/storage/StorageController.h>
#include <kernel/storage/StorageDevice.h>

namespace Kernel {

class PartitionTable;
class StorageManagement {
    BASE_MAKE_ETERNAL;

public:
    StorageManagement(String boot_argument, bool force_pio);
    static bool initialized();
    static void initialize(String boot_argument, bool force_pio);
    static StorageManagement& the();

    NonnullRefPtr<FileSystem> root_filesystem() const;

    static int major_number();
    static int minor_number();

private:
    bool boot_argument_contains_partition_uuid();

    NonnullRefPtrVector<StorageController> enumerate_controllers(bool force_pio) const;
    NonnullRefPtrVector<StorageDevice> enumerate_storage_devices() const;
    NonnullRefPtrVector<DiskPartition> enumerate_disk_partitions() const;

    void determine_boot_device();
    void determine_boot_device_with_partition_uuid();

    OwnPtr<PartitionTable> try_to_initialize_partition_table(const StorageDevice&) const;

    RefPtr<BlockDevice> boot_block_device() const;

    String m_boot_argument;
    RefPtr<BlockDevice> m_boot_block_device { nullptr };
    NonnullRefPtrVector<StorageController> m_controllers;
    NonnullRefPtrVector<StorageDevice> m_storage_devices;
    NonnullRefPtrVector<DiskPartition> m_disk_partitions;
};

}