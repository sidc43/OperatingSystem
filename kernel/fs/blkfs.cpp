/*
  blkfs.cpp - block filesystem that persists ramfs to a virtio-blk disk
  on init it reads the disk header and loads entries back into ramfs
  flush() rewrites the whole disk from the current ramfs state
  disk layout: sector 0 = header, sectors 1-16 = entry table, 17+ = data pool
*/
#include "kernel/fs/blkfs.hpp"
#include "kernel/drivers/virtio/blk.hpp"
#include "kernel/fs/ramfs.hpp"
#include "kernel/mm/heap.hpp"
#include "kernel/core/print.hpp"
#include <string.h>
#include <stdint.h>

static blkfs::DiskHeader s_header                                __attribute__((aligned(512)));
static blkfs::DiskEntry  s_table[blkfs::MAX_ENTRIES]             __attribute__((aligned(512)));

static_assert(sizeof(s_table) == blkfs::TABLE_SECS * 512,
              "s_table size must equal the table sector region");

namespace {

static bool g_ready  = false;
static bool g_loaded = false;

static uint32_t sectors_for(uint32_t bytes) {
    return (bytes + 511u) / 512u;
}

}

namespace blkfs {

bool ready() { return g_ready; }

bool init(const uintptr_t* bases, int n) {
    if (!vblk::init(bases, n)) {
        print("blkfs: no virtio-blk device found\n");
        return false;
    }
    g_ready = true;

    if (!vblk::read_sectors(HEADER_SEC, 1, &s_header)) {
        print("blkfs: header read failed\n");
        return false;
    }

    if (s_header.magic != MAGIC || s_header.version != VERSION) {

        print("blkfs: disk not formatted (will format on first flush)\n");
        return false;
    }

    if (!vblk::read_sectors(TABLE_SEC, TABLE_SECS, s_table)) {
        print("blkfs: entry table read failed\n");
        return false;
    }

    uint32_t loaded = 0;
    for (size_t i = 0; i < MAX_ENTRIES; ++i) {
        const DiskEntry& e = s_table[i];
        if (!e.used) continue;

        if (e.is_dir) {
            ramfs::mkdir(e.name);
        } else if (e.data_size == 0 || e.data_sector == 0) {
            ramfs::create(e.name, nullptr, 0);
        } else {

            uint32_t nsec = sectors_for(e.data_size);
            uint32_t bufsz = nsec * 512u;
            void* buf = kheap::alloc(bufsz, 512);
            if (!buf) {
                print("blkfs: out of memory loading file\n");
                continue;
            }
            if (!vblk::read_sectors(e.data_sector, nsec, buf)) {
                print("blkfs: data read failed for ");
                print(e.name); print("\n");
                kheap::free(buf);
                continue;
            }
            ramfs::create(e.name, buf, e.data_size);
            kheap::free(buf);
        }
        ++loaded;
    }

    g_loaded = true;
    printk("blkfs: loaded %u entries from disk\n", loaded);
    return true;
}

bool flush() {
    if (!g_ready) return false;

    const ramfs::Entry* tbl = ramfs::table();
    uint32_t free_sec = DATA_START;
    uint32_t entry_count = 0;

    memset(s_table, 0, sizeof(s_table));

    for (size_t i = 0; i < MAX_ENTRIES; ++i) {
        const ramfs::Entry& re = tbl[i];
        if (!re.used) continue;

        DiskEntry& de = s_table[entry_count++];

        size_t nl = strlen(re.name);
        if (nl >= sizeof(de.name)) nl = sizeof(de.name) - 1;
        memcpy(de.name, re.name, nl);
        de.name[nl] = '\0';

        de.is_dir  = re.is_dir ? 1u : 0u;
        de.used    = 1u;

        if (re.is_dir || re.size == 0 || re.data == nullptr) {
            de.data_sector = 0;
            de.data_size   = 0;
            continue;
        }

        uint32_t nsec  = sectors_for((uint32_t)re.size);
        uint32_t bufsz = nsec * 512u;
        void* buf = kheap::alloc(bufsz, 512);
        if (!buf) {
            print("blkfs: flush: out of memory\n");
            de.data_sector = 0;
            de.data_size   = 0;
            continue;
        }
        memcpy(buf, re.data, re.size);

        memset((uint8_t*)buf + re.size, 0, bufsz - re.size);

        if (!vblk::write_sectors(free_sec, nsec, buf)) {
            print("blkfs: flush: write failed for ");
            print(re.name); print("\n");
            kheap::free(buf);
            de.data_sector = 0;
            de.data_size   = 0;
            continue;
        }

        de.data_sector = free_sec;
        de.data_size   = (uint32_t)re.size;
        free_sec      += nsec;
        kheap::free(buf);
    }

    if (!vblk::write_sectors(TABLE_SEC, TABLE_SECS, s_table)) {
        print("blkfs: flush: entry table write failed\n");
        return false;
    }

    memset(&s_header, 0, sizeof(s_header));
    s_header.magic        = MAGIC;
    s_header.version      = VERSION;
    s_header.entry_count  = entry_count;
    s_header.free_sector  = free_sec;

    if (!vblk::write_sectors(HEADER_SEC, 1, &s_header)) {
        print("blkfs: flush: header write failed\n");
        return false;
    }

    printk("blkfs: flushed %u entries (%u data sectors)\n",
           entry_count, free_sec - DATA_START);
    return true;
}

}
