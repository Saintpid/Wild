/* SPDX-License-Identifier: GPL-3.0-only */
/* The FM-1's flash parts, by JEDEC id (0x9F): both 1 MiB, the same plain SPI NOR commands.
 * Puya P25Q80H on the first units; UCUN UC25HQ80 on those sold since autumn 2026 (as Felucca's
 * 7435517 and fm1-base's #35). Anything else: RAM only, nothing written. */
#pragma once
#define FL_JEDEC_PUYA 0x856014u
#define FL_JEDEC_UCUN 0xB36014u
#define FL_JEDEC_OK(id) ((id) == FL_JEDEC_PUYA || (id) == FL_JEDEC_UCUN)
