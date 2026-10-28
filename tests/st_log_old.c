/* SPDX-License-Identifier: GPL-3.0-only
 * CoralFlower: older firmware's storage.c (ST_OLD_C, a path) on tests/st_log_test.c's NOR: the reader after a
 * downgrade. Its own translation unit, so both storage.c's statics live side by side. */
#include <stdint.h>
#include <string.h>

#define FELUCCA_CZ 0
#define FELUCCA_QUAD 0
#define CF_LOOP_STORE 1
#define ST_LOG_OBJ (OBJ_PROJECT0 + 3u)
int nor_read(uint32_t off, void *dst, uint32_t n);
int nor_erase(uint32_t off);
int nor_prog(uint32_t off, const void *src, uint32_t n);
static int st_read(uint32_t off, void *dst, uint32_t n) { return nor_read(off, dst, n); }
static int st_erase(uint32_t off) { return nor_erase(off); }
static int st_prog(uint32_t off, const void *src, uint32_t n) { return nor_prog(off, src, n); }
#include ST_OLD_C

int old_load(uint32_t obj, void *dst, uint32_t max) { return st_load(obj, dst, max); }
int old_put(uint32_t obj, const void *src, uint32_t len, uint32_t how) { return st_put(obj, src, len, how); }
void old_boot(uint32_t obj)                      /* its power-on: what RAM knew gone, its tidies */
{
    uint32_t k;
    memset(st_blank, 0, sizeof st_blank);
    st_check_blank(obj);
    for (k = 0; k < 3u && st_tidy(obj) > 0; k++)
        ;
}
