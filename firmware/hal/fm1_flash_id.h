/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stdint.h>

/* Both known FM-1 parts use 256-byte program pages and 4 KiB erase sectors.
 * Leave unrecognised parts in RAM-only mode. */
static inline int fm1_flash_supported(uint32_t jedec_id)
{
    return jedec_id == 0x856014u || jedec_id == 0xB36014u;
}
