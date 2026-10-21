/* SPDX-License-Identifier: GPL-2.0-only */

#include "hv.h"
#include "adt.h"
#include "smp.h"
#include "soc.h"
#include "string.h"
#include "utils.h"

#define POWER_GUARD_PAGE_SIZE           0x4000UL
#define POWER_GUARD_CPU_CONTROL_OFFSET  0x8000UL
#define POWER_GUARD_CPM_CONTROL0_OFFSET 0x4000UL
#define POWER_GUARD_CPM_CONTROL1_OFFSET 0x8000UL
#define POWER_GUARD_MAX_PAGES           (MAX_CPUS * 3)
#define POWER_GUARD_SHADOW_ENTRIES      256

struct power_guard_shadow {
    u64 ipa;
    u64 value;
    u8 width;
    bool valid;
};

static u64 power_guard_pages[POWER_GUARD_MAX_PAGES];
static u32 power_guard_page_count;
static struct power_guard_shadow power_guard_shadow[POWER_GUARD_SHADOW_ENTRIES];

static int power_guard_page_index(u64 ipa)
{
    u64 page = ipa & ~(POWER_GUARD_PAGE_SIZE - 1);

    for (u32 i = 0; i < power_guard_page_count; i++)
        if (page == power_guard_pages[i])
            return i;

    return -1;
}

static int power_guard_add_page(u64 addr)
{
    addr &= ~(POWER_GUARD_PAGE_SIZE - 1);
    if (power_guard_page_index(addr) >= 0)
        return 0;
    if (power_guard_page_count >= ARRAY_SIZE(power_guard_pages))
        return -1;

    power_guard_pages[power_guard_page_count++] = addr;
    return 0;
}

static bool power_guard_shadow_read(u64 ipa, u64 *value, int width)
{
    for (u32 i = 0; i < ARRAY_SIZE(power_guard_shadow); i++) {
        struct power_guard_shadow *entry = &power_guard_shadow[i];
        if (entry->valid && entry->ipa == ipa && entry->width == width) {
            *value = entry->value;
            return true;
        }
    }
    return false;
}

static void power_guard_shadow_write(u64 ipa, u64 value, int width)
{
    int free = -1;

    for (u32 i = 0; i < ARRAY_SIZE(power_guard_shadow); i++) {
        struct power_guard_shadow *entry = &power_guard_shadow[i];
        if (entry->valid) {
            if (entry->ipa == ipa && entry->width == width) {
                entry->value = value;
                return;
            }
        } else if (free < 0) {
            free = i;
        }
    }

    if (free >= 0)
        power_guard_shadow[free] = (struct power_guard_shadow){
            .ipa = ipa,
            .value = value,
            .width = width,
            .valid = true,
        };
}

static bool power_guard_hook(struct exc_info *ctx, u64 ipa, u64 *value, bool write, int width)
{
    /* EL2 owns the live CPU/CPM state; XNU sees its writes through a shadow. */
    if (write) {
        power_guard_shadow_write(ipa, *value, width);
        return true;
    }

    if (power_guard_shadow_read(ipa, value, width))
        return true;
    return hv_pa_rw(ctx, ipa, value, false, width);
}

int hv_map_power_register_guard(void)
{
    if (chip_id != T8132 && chip_id != T8140 && chip_id != T6040 && chip_id != T6041)
        return 0;

    int cpus = adt_path_offset(adt, "/cpus");
    if (cpus < 0) {
        printf("HV: CPU power register guard: /cpus is missing\n");
        return -1;
    }

    power_guard_page_count = 0;
    memset(power_guard_shadow, 0, sizeof(power_guard_shadow));
    int failures = 0;

    int node = cpus;
    ADT_FOREACH_CHILD(adt, node)
    {
        u64 cpu_reg[2];
        u64 cpm_reg[2];
        if (ADT_GETPROP_ARRAY(adt, node, "cpu-impl-reg", cpu_reg) < 0 ||
            ADT_GETPROP_ARRAY(adt, node, "cpm-impl-reg", cpm_reg) < 0) {
            printf("HV: CPU power register guard: incomplete CPU node %s\n",
                   adt_get_name(adt, node));
            failures++;
            continue;
        }

        if (cpu_reg[1] <= POWER_GUARD_CPU_CONTROL_OFFSET ||
            power_guard_add_page(cpu_reg[0] + POWER_GUARD_CPU_CONTROL_OFFSET) < 0) {
            printf("HV: CPU power register guard: invalid CPU range for %s\n",
                   adt_get_name(adt, node));
            failures++;
        }

        const u64 cpm_offsets[] = {
            POWER_GUARD_CPM_CONTROL0_OFFSET,
            POWER_GUARD_CPM_CONTROL1_OFFSET,
        };
        for (u32 i = 0; i < ARRAY_SIZE(cpm_offsets); i++) {
            if (cpm_reg[1] <= cpm_offsets[i])
                continue;
            if (power_guard_add_page(cpm_reg[0] + cpm_offsets[i]) < 0) {
                printf("HV: CPU power register guard: invalid CPM range for %s\n",
                       adt_get_name(adt, node));
                failures++;
            }
        }
    }

    for (u32 i = 0; i < power_guard_page_count; i++)
        failures += hv_map_hook(power_guard_pages[i], power_guard_hook, POWER_GUARD_PAGE_SIZE) != 0;

    sysop("dsb ishst");
    sysop("tlbi vmalls12e1is");
    sysop("dsb ish");
    sysop("isb");
    return failures ? -failures : 0;
}
