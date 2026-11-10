/* SPDX-License-Identifier: GPL-3.0-only */
#include <stdio.h>
#include "fm1_flash_id.h"

/* Exercise the actual hardware storage entry points without touching registers. */
static unsigned flash_calls;
static uint32_t abuf[1], st_holes;

static uint32_t irq_save(void) { return 0; }
static void irq_restore(uint32_t state) { (void)state; }
static int audio_loud(void) { return 0; }
static int fl_read_ram(uint32_t off, void *dst, uint32_t len)
{
    (void)off;
    (void)dst;
    (void)len;
    flash_calls++;
    return 0;
}
static int fl_erase4k_ram(uint32_t off, uint32_t *took_us)
{
    (void)off;
    *took_us = 0;
    flash_calls++;
    return 0;
}
static int fl_write(uint32_t off, const void *src, uint32_t len)
{
    (void)off;
    (void)src;
    (void)len;
    flash_calls++;
    return 0;
}

#define FL_FAR(fn) (fn)
#define FL_STORE_OK(off, len) ((off) == 0xFC000u && (len) <= 4096u)
#include "../firmware/src/storage_hw.c"

int main(void)
{
    static const struct {
        uint32_t id;
        int supported;
        const char *part;
    } cases[] = {
        {0x856014u, 1, "Puya P25Q80H"},
        {0xB36014u, 1, "UCUN UC25HQ80"},
        {0xEB6014u, 0, "SMK-37 flash, unverified on FM-1"},
        {0xB36015u, 0, "unexpected capacity"},
        {0x000000u, 0, "failed read"},
        {0xFFFFFFu, 0, "no response"},
    };
    int failed = 0;
    for (size_t index = 0; index < sizeof cases / sizeof cases[0]; index++) {
        uint8_t data = 0;
        flash_ok = fm1_flash_supported(cases[index].id);
        if (flash_ok != cases[index].supported) {
            fprintf(stderr, "%s (0x%06X): expected flash support %d, got %d\n",
                    cases[index].part, (unsigned)cases[index].id,
                    cases[index].supported, flash_ok);
            failed = 1;
        }
        flash_calls = 0;
        int read_ok = st_read(0xFC000u, &data, sizeof data) == 0;
        int erase_ok = st_erase(0xFC000u) == 0;
        int write_ok = st_prog(0xFC000u, &data, sizeof data) == 0;
        if (read_ok != cases[index].supported || erase_ok != cases[index].supported ||
            write_ok != cases[index].supported || flash_calls != 3u * cases[index].supported) {
            fprintf(stderr, "%s (0x%06X): storage read/erase/write %d/%d/%d, driver calls %u\n",
                    cases[index].part, (unsigned)cases[index].id,
                    read_ok, erase_ok, write_ok, flash_calls);
            failed = 1;
        }
    }
    if (!failed)
        puts("FM-1 flash ID and storage access: all six cases passed");
    return failed;
}
