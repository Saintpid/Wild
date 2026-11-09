/* SPDX-License-Identifier: GPL-3.0-only */
/* n001se platform (FiMba-1's) on the FM-1 (FoMni's): plat.h over Felucca's HAL, panel map, USB rings and storage.
 * Part of the unity build (main.c), after the HAL, usb.c, storage.c and panel.c. */

uint32_t plat_ms(void) { return fm1_ms; }

uint32_t plat_buttons(void)
{
    uint32_t raw = fm1_in.buttons, out = 0, i;
    for (i = 0; i < NB; i++)
        if (raw & (1u << panel.btn[i]))
            out |= 1u << i;
    return out;
}

uint32_t plat_keys(void) { return fm1_in.notes & ((1u << NKEYS) - 1u); }

int32_t plat_enc(int role)
{
    if (role < 0 || role >= NE)
        return 0;
    return fm1_enc_take(panel.enc[role]) * panel.dir[role];
}

static volatile uint32_t master_q12 = 2048;
uint32_t plat_master(void) { return master_q12; }

/* LEDs: the picture is built off-line and copied one byte per column (Felucca: clearing
 * and relighting would let the 10 kHz scan catch the dark gap and flicker) */
static uint8_t led_pos[FM1_NKEY];
static void led_pos_init(void)
{
    uint32_t id, p, r;
    for (id = 0; id < FM1_NKEY; id++) {
        led_pos[id] = 0xFF;
        for (p = 0; p < FM1_NCOL; p++)
            for (r = 1; r < 5u; r++)
                if (FM1_KEYMAP[r][p] == (int8_t)id)
                    led_pos[id] = (uint8_t)((p << 3) | r);
    }
}
void plat_leds(uint32_t buttons, uint32_t keys, uint32_t dim_keys)
{
    uint8_t nl[FM1_NCOL] = {0}, nd[FM1_NCOL] = {0};
    uint32_t i, c;
    for (i = 0; i < NB + NKEYS; i++) {
        uint32_t id = i < NB ? panel.btn[i] : 14u + (i - NB);
        int on = i < NB ? (buttons >> i) & 1u : (keys >> (i - NB)) & 1u;
        int dim = i < NB || ((dim_keys >> (i - NB)) & 1u);   /* every button glows: its name shows */
        uint8_t q = led_pos[id];
        if (q == 0xFF)
            continue;
        if (on)
            nl[q >> 3] |= (uint8_t)(1u << (q & 7u));
        else if (dim)
            nd[q >> 3] |= (uint8_t)(1u << (q & 7u));
    }
    for (c = 0; c < FM1_NCOL; c++) {
        fm1_led[c] = nl[c];
        fm1_led_dim[c] = nd[c];
    }
}

/* MIDI: Felucca's rings carry 4-byte USB-MIDI event packets, byte 0 = cable / CIN */
int plat_midi_in(uint32_t *pkt)
{
    if (mi_r == mi_w)
        return 0;
    *pkt = midi_in_q[mi_r % MQ];
    mi_r++;
    return 1;
}
void plat_midi_out(uint32_t pkt) { midi_out_event(pkt); }

int plat_store_load(uint32_t obj, void *dst, uint32_t max) { return flash_ok ? st_load(obj, dst, max) : -1; }
int plat_store_save(uint32_t obj, const void *src, uint32_t len) { return flash_ok ? st_save(obj, src, len) : -9; }
int plat_preset_load(int n, void *dst, uint32_t max) { return flash_ok ? st_preset_load((uint32_t)n, dst, max) : -1; }
int plat_preset_save(int n, const void *src, uint32_t len) { return flash_ok ? st_preset_save((uint32_t)n, src, len) : -9; }
int plat_preset_used(int n) { return flash_ok && st_preset_used((uint32_t)n); }
int plat_part_of(int n) { return flash_ok ? st_part_of((uint32_t)n) : -1; }
int plat_part_load(int n, int code, void *dst, uint32_t max) { return flash_ok ? st_part_load((uint32_t)n, (uint32_t)code, dst, max) : -1; }
int plat_part_save(int n, int code, const void *src, uint32_t len) { return flash_ok ? st_part_save((uint32_t)n, (uint32_t)code, src, len) : -9; }
int plat_part_free(int n) { return flash_ok ? st_part_free((uint32_t)n) : -9; }
int plat_preset_copy(int from, int to) { return flash_ok ? st_preset_copy((uint32_t)from, (uint32_t)to) : -9; }
int plat_part_copy(int from, int code, int to, int code_to)
{
    return flash_ok ? st_part_copy((uint32_t)from, (uint32_t)code, (uint32_t)to, (uint32_t)code_to) : -9;
}
int plat_bank_load(int n, void *dst, uint32_t max) { return flash_ok ? st_bank_load((uint32_t)n, dst, max) : -1; }
int plat_bank_save(int n, const void *src, uint32_t len) { return flash_ok ? st_bank_save((uint32_t)n, src, len) : -9; }

static volatile uint32_t audio_cpu_pct, audio_xruns;
uint32_t plat_cpu_pct(void) { return audio_cpu_pct; }
uint32_t plat_xruns(void) { return audio_xruns; }

/* the battery: one conversion every 2 s from the main loop (a few us), smoothed over ~16 s. The scale is
 * Felucca's ADC (3.3 V full scale) over PB1's halving divider: 6.6 V at 1023 */
static int32_t batt_s;                 /* the reading x 16, smoothed (0: none yet) */
static void batt_poll(void)
{
    static uint32_t t;
    int32_t a;
    if (t && fm1_ms - t < 2000u)
        return;
    t = fm1_ms | 1u;
    if ((a = fm1_adc_read(FM1_ADC_BATT)) > 0)
        batt_s = batt_s ? batt_s + (a * 16 - batt_s) / 8 : a * 16;
}
uint32_t plat_batt_mv(void) { return (uint32_t)batt_s * 6600u / (1023u * 16u); }
int plat_usb_power(void) { return usb.config && !usb.suspended; }
