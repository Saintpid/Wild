"""Regression: all normal-start vectors must be attached before top RAM locks."""
from pathlib import Path
s = (Path(__file__).resolve().parents[1] / "firmware/src/app/main_fm1.c").read_text()
s = s.split("static void fm1_main(void)", 1)[1].split("void fm1_cstart(void)", 1)[0]
assert s.count("fm1_guard_lock_top();") == 1
assert s.index("fm1_timer5_start(") < s.index("audio_init();") < s.index("fm1_guard_lock_top();")
a = s.index("audio_init();")
assert s.rfind("irq_save();", 0, a) > s.index("ui_init();")
assert s.index("fm1_guard_lock_top();") < s.index("irq_restore(flags);", a)
print("boot vector registration precedes protection, with IRQs disabled: OK")

assert s.index("fm1_irq_attach(FM1_IRQ_SOFT0, isr_render, 2)") < s.index("fm1_guard_lock_top();")
assert s.index("wild_started = 1;") < s.index("irq_restore(flags);", a)
body = s.split("for (;;) {", 1)[1]
assert "audio_service()" not in body, "foreground must not share renderer ownership"
full = (Path(__file__).resolve().parents[1] / "firmware/src/app/main_fm1.c").read_text()
consumer = full.split("void fm1_alnk0_irq(void)", 1)[1].split("extern void isr_alnk0", 1)[0]
assert "fm1_render_request();" in consumer
assert "ne_render(" not in consumer and "audio_service(" not in consumer
print("render IRQ registered before guard; foreground and DMA do not synthesize: OK")
