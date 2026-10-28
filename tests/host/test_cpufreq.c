/* SPDX-License-Identifier: MIT */
/* Host-only MMIO/ADT model: cc -Wall -Wextra tests/host/test_cpufreq.c -o /tmp/test-cpufreq */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../../src/adt.h"
#include "../../src/types.h"

/* Compile the production path, substituting only platform I/O. */
#define UTILS_H
u32 chip_id;
static u64 read64(u64 addr);
static void write64(u64 addr, u64 value);
static void write32(u64 addr, u32 value);
static u64 mask64(u64 addr, u64 clear, u64 set);
static u64 set64(u64 addr, u64 set);
static int poll32(u64 addr, u32 mask, u32 target, u32 timeout);
static int poll64(u64 addr, u64 mask, u64 target, u32 timeout);
#include "../../src/cpufreq.c"

void *adt;
struct fw_version_info os_firmware;
static struct {
    u32 id;
    char type[2];
    u64 cpm[2];
} cpus[10];
static u32 cluster_count, child_count, throttle;
static int missing_node;
static const char *missing_property;
static bool timeout_poll, drop_feature_write;
static struct {
    u64 addr, value;
} regs[64];
static unsigned int nregs, writes;

static u64 *reg(u64 addr)
{
    for (unsigned int i = 0; i < nregs; i++)
        if (regs[i].addr == addr)
            return &regs[i].value;
    assert(nregs < 64);
    regs[nregs].addr = addr;
    regs[nregs].value = 0;
    return &regs[nregs++].value;
}

static u64 read64(u64 addr)
{
    return *reg(addr);
}
static void write64(u64 addr, u64 value)
{
    /* Independent whitelist: no snooze, LUT, PMP SRAM or voltage writes. */
    u64 base = addr >= 0x211e00000 ? 0x211e00000 : 0x210e00000;
    u64 offset = addr - base;
    assert(offset == 0x20020 || offset == 0x40250 || offset == 0x40270 || offset == 0x440f8 ||
           offset == 0x48400 || offset == 0x48408);
    writes++;
    if (!(drop_feature_write && offset == 0x48400))
        *reg(addr) = value;
}
static void write32(u64 addr, u32 value)
{
    write64(addr, value);
}
static u64 mask64(u64 addr, u64 clear, u64 set)
{
    u64 before = read64(addr);
    write64(addr, (before & ~clear) | set);
    return before;
}
static u64 set64(u64 addr, u64 set)
{
    return mask64(addr, 0, set);
}
static int poll64(u64 addr, u64 mask, u64 target, u32 timeout)
{
    (void)timeout;
    return timeout_poll || (read64(addr) & mask) != target ? -1 : 0;
}
static int poll32(u64 addr, u32 mask, u32 target, u32 timeout)
{
    return poll64(addr, mask, target, timeout);
}

int adt_path_offset(const void *tree, const char *path)
{
    (void)tree;
    return !strcmp(path, "/cpus") ? 100 : -1;
}
int adt_path_offset_trace(const void *tree, const char *path, int *offsets)
{
    (void)tree;
    (void)path;
    offsets[0] = 101;
    return 101;
}
int adt_get_child_count(const void *tree, int node)
{
    (void)tree;
    assert(node == 100);
    return child_count;
}
int adt_first_child_offset(const void *tree, int node)
{
    (void)tree;
    assert(node == 100);
    return 0;
}
int adt_next_sibling_offset(const void *tree, int node)
{
    (void)tree;
    return node + 1;
}
int adt_getprop_copy(const void *tree, int node, const char *key, void *out, size_t len)
{
    (void)tree;
    if (node == missing_node && missing_property && !strcmp(key, missing_property))
        return -1;
    const void *value = NULL;
    size_t size = 0;
    if (node == 100 && !strcmp(key, "cpu-cluster-count")) {
        value = &cluster_count;
        size = sizeof(cluster_count);
    } else if (node >= 0 && node < 10) {
        if (!strcmp(key, "cluster-id")) {
            value = &cpus[node].id;
            size = sizeof(cpus[node].id);
        } else if (!strcmp(key, "cluster-type")) {
            value = cpus[node].type;
            size = sizeof(cpus[node].type);
        } else if (!strcmp(key, "cpm-impl-reg")) {
            value = cpus[node].cpm;
            size = sizeof(cpus[node].cpm);
        }
    }
    if (!value || len != size)
        return -1;
    memcpy(out, value, size);
    return size; /* ADT's success is a positive byte count, including cluster ID 0. */
}
int adt_get_reg(const void *tree, int *path, const char *prop, int idx, u64 *addr, u64 *size)
{
    (void)tree;
    (void)path;
    (void)prop;
    (void)idx;
    (void)size;
    *addr = 0x380000000;
    return 0;
}
u32 pmgr_get_feature(const char *name)
{
    if (!strcmp(name, "cpu-apsc"))
        return 1;
    if (strstr(name, "thrtl"))
        return throttle;
    return 0;
}
int pmgr_power_on(int die, const char *name)
{
    (void)die;
    (void)name;
    assert(false);
    return -1;
}

static void reset(void)
{
    chip_id = T8132;
    nregs = writes = throttle = 0;
    cluster_count = 2;
    child_count = 10;
    missing_node = -1;
    missing_property = NULL;
    timeout_poll = drop_feature_write = false;
    /* J713 and J773g captured ADTs both describe 6 E + 4 P, same CPM apertures. */
    for (unsigned int i = 0; i < 10; i++) {
        cpus[i].id = i >= 6;
        strcpy(cpus[i].type, i >= 6 ? "P" : "E");
        cpus[i].cpm[0] = i >= 6 ? 0x211e40000 : 0x210e40000;
        cpus[i].cpm[1] = 0xc010;
    }
    for (u64 base = 0x210e00000; base <= 0x211e00000; base += 0x1000000) {
        *reg(base + 0x20020) = BIT(48) | BIT(42) | BIT(23) | 18;
        u64 offsets[] = {0x40250, 0x40270, 0x48400, 0x48408};
        for (unsigned int i = 0; i < 4; i++)
            *reg(base + offsets[i]) = BIT(63) | 0x12345678;
    }
}

static void reject(void)
{
    assert(cpufreq_init() < 0);
    assert(writes == 0);
}

int main(void)
{
    reset();
    assert(cpufreq_init() == 0);
    for (u64 base = 0x210e00000; base <= 0x211e00000; base += 0x1000000) {
        u64 command = read64(base + 0x20020);
        assert(command == (BIT(48) | BIT(25) | (base == 0x210e00000 ? 5 : 6)));
        assert(read64(base + 0x440f8) == 1);
        u64 offsets[] = {0x40250, 0x40270, 0x48400, 0x48408};
        for (unsigned int i = 0; i < 4; i++)
            assert(read64(base + offsets[i]) == 0x12345678);
    }
    reset();
    throttle = 1;
    assert(cpufreq_init() == 0);
    assert(read64(0x210e48400) & BIT(63));
    assert(read64(0x211e48408) & BIT(63));
    const char *keys[] = {"cluster-id", "cluster-type", "cpm-impl-reg"};
    for (unsigned int i = 0; i < 3; i++) {
        reset();
        missing_node = 9;
        missing_property = keys[i];
        reject();
    }
    reset();
    missing_node = 100;
    missing_property = "cpu-cluster-count";
    reject();
    reset();
    cluster_count = 3;
    reject();
    reset();
    child_count = 6;
    reject();
    reset();
    cpus[9].id = 2;
    reject();
    reset();
    cpus[9].type[0] = 'E';
    reject();
    reset();
    cpus[9].cpm[0] += 0x10000;
    reject();
    reset();
    cpus[9].cpm[0]++;
    reject();
    reset();
    cpus[9].cpm[1] = 0x8408;
    reject();
    reset();
    cpus[9].cpm[1] = UINT64_MAX;
    reject();
    reset();
    timeout_poll = true;
    assert(cpufreq_init() < 0);
    reset();
    drop_feature_write = true;
    assert(cpufreq_init() < 0);
    reset();
    chip_id = T8122;
    assert(cpufreq_get_clusters() == t6030_clusters);
    assert(cpufreq_get_features() == t8122_features);
    puts("PASS: T8132 ADT mapping, failure-before-write, register preservation, feature polarity, "
         "failures, legacy dispatch");
    return 0;
}
