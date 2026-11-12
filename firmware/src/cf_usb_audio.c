/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 CoralFlower contributors */
/* Capture the same final stereo mix as the DAC, including FX, MASTER, the boot sound and the metronome.
 * Called by audio_block before its Q15 -> PCM24 conversion. No part stems and no host playback path.
 * The DAC shifts by seven into a signed 24-bit word, so PCM16 is out / 2 (the same normalized level).
 * A 48 kHz host gets the existing 160/147 FIR; the synth and analogue output keep their own clock. */
_Static_assert(UA_CAP_CHANNELS == 2u && CTL <= UA_RS_BLOCK, "CoralFlower stereo capture block");

static void cf_usb_audio_capture(const int32_t *out, uint32_t n)
{
    int16_t stage[CTL * 2u];
    const int16_t *samples = stage;
    uint32_t i, frames = n, rate48;
    if (!usb.up || !usb.config || usb.suspended || USB_CDC_ON || !ua.cap_alt || ua.cap_hold)
        return;
    rate48 = ua.r48;
    for (i = 0; i < n * 2u; i++) {
        int32_t sample = out[i] >> 1;
        stage[i] = (int16_t)(sample > 32767 ? 32767 : sample < -32768 ? -32768 : sample);
    }
    if (rate48) {
        frames = ua_block48(stage, n);
        samples = ua.rs_out;
    }
    /* TIMER5 may serve or reset the stream between blocks, but not during this short ring copy. */
    fm1_irq_off();
    if (usb.up && usb.config && !usb.suspended && !USB_CDC_ON)
        ua_feed(samples, frames, rate48);
    fm1_irq_on();
}
