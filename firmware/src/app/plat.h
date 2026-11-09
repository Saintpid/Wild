/* SPDX-License-Identifier: GPL-3.0-only */
/* n001se platform (FiMba-1's) interface (FoMni's, from X0X's): everything the app needs from the hardware.
 *
 * The device implementation (plat_fm1.c) wraps Felucca's HAL and panel map; the
 * host implementation (host/n001se_host.c) is a simulator fed by a script, which is
 * what lets the whole app â€” UI, sequencer, engines â€” run and be tested on a Mac.
 * Drawing goes through Felucca's lcd_fill / lcd_blit (lcd.c on the device, a
 * framebuffer on the host) and gfx.c on top. */
#pragma once
#include <stdint.h>

/* big zero-initialised buffers go in the .pool RAM region on the device */
#ifdef OM_HOST
#define OM_POOL
#else
#define OM_POOL __attribute__((section(".pool")))
#endif

/* panel controls, by printed label (Felucca's panel.c map turns these into matrix ids; Felucca
 * calls the second button SCL, the panel prints SEL) */
enum { B_FX, B_SEL, B_ENV, B_LFO, B_EDIT, B_GLO, B_HOME, B_SAVE, B_ARP, B_SEQ, B_PLAY, B_REC,
       B_OCTDN, B_OCTUP, NB };
enum { EN_SELECT, EN_ALGO, EN_PRESET, EN_K1, EN_K2, EN_K3, EN_K4, NE };

#define NKEYS 27                       /* F3 .. G5 */

uint32_t plat_ms(void);
uint32_t plat_buttons(void);           /* held: bit B_* */
uint32_t plat_keys(void);              /* held: bit k = key k (0 = F3) */
int32_t plat_enc(int role);            /* detents since the last call, + = clockwise; EN_* */
uint32_t plat_master(void);            /* MASTER pot, 0..4096 gain Q12 */
void plat_leds(uint32_t buttons, uint32_t keys, uint32_t dim_keys);   /* lit: bit B_*, bit key; dim: bit key
                                                                     (the buttons not lit are always dim) */

/* USB MIDI, 4-byte USB-MIDI event packets (cable 0) */
int plat_midi_in(uint32_t *pkt);       /* 1 = got one (called from the audio ISR) */
void plat_midi_out(uint32_t pkt);      /* queue one (audio ISR or main loop) */

/* persistent storage (Felucca storage.c objects: A/B sectors, CRC, torn-write safe) */
int plat_store_load(uint32_t obj, void *dst, uint32_t max);   /* bytes loaded, < 0 = none / bad */
int plat_store_save(uint32_t obj, const void *src, uint32_t len);   /* 0 = ok */
#define PLAT_STORE_MAX 3840u           /* bytes per object */
enum { OBJ_PROJ, OBJ_PANEL, OBJ_NOBJ };
/* presets: PLAT_PRESETS numbered slots of up to PLAT_STORE_MAX bytes (flash 0x97000.., a sector each) */
#define PLAT_PRESETS 64
int plat_preset_load(int n, void *dst, uint32_t max);   /* bytes loaded, < 0 = empty / bad */
int plat_preset_save(int n, const void *src, uint32_t len);   /* 0 = ok */
int plat_preset_used(int n);                            /* 1 = holds a preset */
/* a song's parts (Wild): preset slots that hold no preset hold the rest of a song's banks and its
 * automation; code = the song's slot * 8 + the part */
int plat_part_of(int n);                                /* the code, < 0: not a part */
int plat_part_load(int n, int code, void *dst, uint32_t max);
int plat_part_save(int n, int code, const void *src, uint32_t len);
int plat_part_free(int n);                              /* any slot erased (a part or a preset): 0 = ok */
int plat_preset_copy(int from, int to);                 /* preset from into slot to: 0 = ok */
int plat_part_copy(int from, int code, int to, int code_to);   /* a part, as another song's: 0 = ok */
/* the banks (Wild): PLAT_BANK_SECTORS slots like the presets', two banks each (flash 0xD7000.., 0xDE000..);
 * the last one holds the automation */
#define PLAT_BANK_SECTORS 7
int plat_bank_load(int n, void *dst, uint32_t max);     /* bytes loaded, < 0 = empty / bad */
int plat_bank_save(int n, const void *src, uint32_t len);   /* 0 = ok */
/* flash: OBJ_PROJ 0xFC000, OBJ_PANEL 0xDC000 (2 sectors each, inside Felucca's data area) */

/* audio load in % of the render budget, and xruns since boot (device: from the ISR) */
uint32_t plat_cpu_pct(void);
uint32_t plat_xruns(void);

/* the battery: its voltage in mV (PB1's divider, read every 2 s and smoothed; 0: none), and 1 while a USB host
 * powers the FM-1 (and so charges it: there is no charger line, a charger alone is not seen) */
uint32_t plat_batt_mv(void);
int plat_usb_power(void);

/* Hardware diagnostics. Memory values are linked static regions, not heap use.
 * CPU values are rendering budget percentages, not whole-chip utilization. */
typedef struct {
    uint32_t render_now, render_avg, render_peak;
    uint32_t irq_us, irq_peak_us, ui_us, ui_peak_us;
    uint32_t ready, slots, underruns, late, recoveries, render_blocks;   /* ready of slots: PCM blocks queued */
    uint32_t ram_used, ram_total, pool_used, pool_total;
    uint32_t hardware;
    uint32_t split[4];                 /* drums, 303, sequencer + FX, master: % of the budget since the last read */
} plat_audio_diag;
void plat_audio_diag_read(plat_audio_diag *d);
