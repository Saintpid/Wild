/* SPDX-License-Identifier: GPL-3.0-only */
/* The flash parts the firmware saves to and updates on: the first FM-1s' Puya and the newer
 * units' UCUN; everything else stays RAM only. */
#include <stdio.h>
#include <stdint.h>
#include "fm1_flash_ok.h"

static int fails;
static void ok(int c, const char *what)
{
    printf("  %s %s\n", c ? "ok  " : "FAIL", what);
    fails += !c;
}

int main(void)
{
    ok(FL_JEDEC_OK(0x856014u), "Puya P25Q80H, the first FM-1s");
    ok(FL_JEDEC_OK(0xB36014u), "UCUN UC25HQ80, the newer FM-1s");
    ok(!FL_JEDEC_OK(0x000000u), "no part answering");
    ok(!FL_JEDEC_OK(0xFFFFFFu), "all ones");
    ok(!FL_JEDEC_OK(0xEF4016u), "a 4 MiB Winbond");
    ok(!FL_JEDEC_OK(0xC86014u), "a GigaDevice");
    ok(!FL_JEDEC_OK(0x856015u), "near the Puya");
    ok(!FL_JEDEC_OK(0xB36013u), "near the UCUN");
    return fails != 0;
}
