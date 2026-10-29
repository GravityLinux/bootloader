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
static u32 read32(u64 addr);
static void udelay(u32 usec);
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
static u32 cluster_count, child_count, ppt_throttle, llc_throttle, amx_throttle;
static unsigned int gate_accesses[2][4];
static const u64 gate_offsets[] = {0x40250, 0x40270, 0x48400, 0x48408};
static int missing_node;
static const char *missing_property;
static bool timeout_poll, drop_feature_write;
static bool command_stuck;
static unsigned int command_busy_reads[2];
enum access_type { READ64, READ32, WRITE64, DELAY };
static struct {
    enum access_type type;
    u64 addr, value;
} accesses[4096];
static unsigned int naccesses;
static struct {
    u64 addr, value;
} regs[64];
static unsigned int nregs, writes;

static void record(enum access_type type, u64 addr, u64 value)
{
    assert(naccesses < 4096);
    accesses[naccesses].type = type;
    accesses[naccesses].addr = addr;
    accesses[naccesses++].value = value;
}

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

static void check_access(u64 addr)
{
    if (chip_id != T8132)
        return;

    unsigned int cluster = addr >= 0x211e00000;
    u64 base = cluster ? 0x211e00000 : 0x210e00000;
    assert(addr != base + 0x440f8 && "unexplained T8132 CPM register accessed");
    u32 enabled[] = {amx_throttle, llc_throttle, ppt_throttle, ppt_throttle};
    for (unsigned int i = 0; i < 4; i++) {
        if (addr != base + gate_offsets[i])
            continue;
        /* This includes diagnostic reads, read/modify/write and polling. */
        assert(enabled[i] && "disabled T8132 throttle gate accessed");
        gate_accesses[cluster][i]++;
    }
}

static u64 read64(u64 addr)
{
    check_access(addr);
    assert(addr != 0x210e20050 && addr != 0x211e20050);
    u64 value = *reg(addr);
    if (chip_id == T8132 && (addr == 0x210e20020 || addr == 0x211e20020)) {
        unsigned int cluster = addr == 0x211e20020;
        if (command_stuck || command_busy_reads[cluster])
            value |= BIT(31);
        if (command_busy_reads[cluster])
            command_busy_reads[cluster]--;
    }
    record(READ64, addr, value);
    return value;
}
static u32 read32(u64 addr)
{
    assert(addr == 0x210e20050 || addr == 0x211e20050);
    record(READ32, addr, (u32)*reg(addr));
    return *reg(addr);
}
static void udelay(u32 usec)
{
    record(DELAY, 0, usec);
}
static void write64(u64 addr, u64 value)
{
    check_access(addr);
    /* Independent whitelist: no snooze, LUT, PMP SRAM or voltage writes. */
    u64 base = addr >= 0x211e00000 ? 0x211e00000 : 0x210e00000;
    u64 offset = addr - base;
    assert(offset == 0x20020 || offset == 0x40250 || offset == 0x40270 || offset == 0x440f8 ||
           offset == 0x48400 || offset == 0x48408);
    record(WRITE64, addr, value);
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
    if (!strcmp(name, "ppt-thrtl"))
        return ppt_throttle;
    if (!strcmp(name, "llc-thrtl"))
        return llc_throttle;
    if (!strcmp(name, "amx-thrtl"))
        return amx_throttle;
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
    nregs = writes = ppt_throttle = llc_throttle = amx_throttle = 0;
    naccesses = 0;
    command_stuck = false;
    memset(command_busy_reads, 0, sizeof(command_busy_reads));
    memset(gate_accesses, 0, sizeof(gate_accesses));
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
        /* Actual/target deliberately differ from requests; status is not polled. */
        *reg(base + 0x20050) = BIT(31) | (3 << 5) | 7;
        for (unsigned int i = 0; i < 4; i++)
            *reg(base + gate_offsets[i]) = BIT(63) | 0x12345678;
    }
}

static void reject(void)
{
    assert(cpufreq_init() < 0);
    assert(writes == 0);
}

static void test_t8132_transition(void)
{
    for (unsigned int cluster = 0; cluster < 2; cluster++) {
        reset();
        const struct cluster_t *c = &cpufreq_get_clusters()[cluster];
        u64 command = cluster ? 0x211e20020 : 0x210e20020;
        u64 status = cluster ? 0x211e20050 : 0x210e20050;
        u64 expected = (*reg(command) & ~0x1f) | BIT(25) | 6;
        command_busy_reads[cluster] = 2;
        assert(set_pstate(c, 6) == 0);
        /* Two busy samples, then readiness, one request, one 32-bit observation. */
        assert(naccesses == 7);
        for (unsigned int i = 0; i <= 4; i += 2) {
            assert(accesses[i].type == READ64 && accesses[i].addr == command);
            assert(!!(accesses[i].value & BIT(31)) == (i < 4));
        }
        assert(accesses[1].type == DELAY && accesses[1].value == 2);
        assert(accesses[3].type == DELAY && accesses[3].value == 2);
        assert(accesses[5].type == WRITE64 && accesses[5].addr == command);
        assert(accesses[5].value == expected);
        assert(accesses[6].type == READ32 && accesses[6].addr == status);
        assert(accesses[6].value == (BIT(31) | (3 << 5) | 7));
        assert(writes == 1);

        /* Linux also submits an already-requested state, after checking busy. */
        naccesses = 0;
        assert(set_pstate(c, 6) == 0);
        assert(naccesses == 3);
        assert(accesses[0].type == READ64 && accesses[0].addr == command);
        assert(accesses[1].type == WRITE64 && accesses[1].addr == command);
        assert(accesses[2].type == READ32 && accesses[2].addr == status);

        reset();
        command_stuck = true;
        assert(set_pstate(c, 6) < 0);
        assert(writes == 0 && naccesses == 401);
        for (unsigned int i = 0; i < naccesses; i++) {
            if (i & 1)
                assert(accesses[i].type == DELAY && accesses[i].value == 2);
            else
                assert(accesses[i].type == READ64 && accesses[i].addr == command);
        }
    }
}

int main(void)
{
    test_t8132_transition();
    reset();
    assert(cpufreq_init() == 0);
    for (u64 base = 0x210e00000; base <= 0x211e00000; base += 0x1000000) {
        u64 command = read64(base + 0x20020);
        assert(command == (BIT(48) | BIT(25) | (base == 0x210e00000 ? 5 : 6)));
        assert(*reg(base + 0x440f8) == 0);
        for (unsigned int i = 0; i < 4; i++) {
            /* Inspect model storage without simulating a forbidden MMIO read. */
            assert(*reg(base + gate_offsets[i]) == (BIT(63) | 0x12345678));
            assert(gate_accesses[base == 0x211e00000][i] == 0);
        }
    }
    /* Check each gate independently, including all mixed enable combinations. */
    for (unsigned int enabled_mask = 1; enabled_mask < 8; enabled_mask++) {
        reset();
        ppt_throttle = !!(enabled_mask & 1);
        llc_throttle = !!(enabled_mask & 2);
        amx_throttle = !!(enabled_mask & 4);
        for (unsigned int cluster = 0; cluster < 2; cluster++) {
            u64 base = cluster ? 0x211e00000 : 0x210e00000;
            for (unsigned int i = 0; i < 4; i++)
                *reg(base + gate_offsets[i]) = 0x12345678;
        }
        assert(cpufreq_init() == 0);
        u32 enabled[] = {amx_throttle, llc_throttle, ppt_throttle, ppt_throttle};
        for (unsigned int cluster = 0; cluster < 2; cluster++) {
            u64 base = cluster ? 0x211e00000 : 0x210e00000;
            for (unsigned int i = 0; i < 4; i++) {
                assert(*reg(base + gate_offsets[i]) == (0x12345678 | (enabled[i] ? BIT(63) : 0)));
                assert((gate_accesses[cluster][i] != 0) == !!enabled[i]);
            }
        }
    }
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
    ppt_throttle = 1;
    *reg(0x210e48400) &= ~BIT(63);
    *reg(0x211e48400) &= ~BIT(63);
    drop_feature_write = true;
    assert(cpufreq_init() < 0);
    reset();
    chip_id = T8122;
    assert(cpufreq_get_clusters() == t6030_clusters);
    assert(cpufreq_get_features() == t8122_features);
    puts("PASS: T8132 ADT mapping, pre-write command polling, 32-bit status, busy timeout, "
         "failure-before-write, register preservation, forbidden CPM access rejection, "
         "mixed feature enables, failures, legacy dispatch");
    return 0;
}
