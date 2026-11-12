/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * ChoralRoot changes (six capture channels, PCM16 capture, the channel names, the playback function removed; 0.19:
 * the second sampling frequency)
 * Copyright (C) 2026 ChoralRoot FM-1 contributors */
/* Melodee's UAC1 recording function (usb_audio_desc.h, Melodee 0.11.1), 44.1 and 48 kHz. ChoralRoot has no playback
 * function (Melodee's "Out": the computer's audio through the FM-1, removed in 0.14).
 * IF2 control + IF3 streaming: recording (string 3), EP2 IN, alternate 1 = PCM16.
 * CoralFlower selects two channels, master L/R (strings 4..5); legacy builds
 * use six, master/CHORD/BASS L/R (strings 4..9, the input terminal's iChannelNames).
 * Included inside usb.c's CFG_DESC (after the MIDI function), whose length is CFG_LEN. */
    8, 0x0B, 2, 2, 1, 1, 0, 3,                      /* IAD: recording (IF 2-3), "ChoralRoot In" */
    9, 4, 2, 0, 0, 1, 1, 0, 3,
    9, 0x24, 1, 0x00, 0x01, 30, 0, 1, 3,            /* AC header: one stream, IF 3 */
    12, 0x24, 2, 3, 0x13, 0x07, 0, UA_CAP_CHANNELS,
#if UA_CAP_CHANNELS == 2u
    3, 0, 4, 0,                                    /* synthesizer: front left/right, Master L/R strings 4..5 */
#else
    0, 0, 4, 0,                                    /* legacy six non-spatial channels, strings 4..9 */
#endif
    9, 0x24, 3, 4, 0x01, 0x01, 0, 3, 0,             /* USB streaming output */

    9, 4, 3, 0, 0, 1, 2, 0, 0,
    9, 4, 3, 1, 1, 1, 2, 0, 0,
    7, 0x24, 1, 4, 1, 1, 0,                         /* PCM */
    14, 0x24, 2, 1, UA_CAP_CHANNELS, 2, 16, 2, 0x44, 0xAC, 0x00, 0x80, 0xBB, 0x00,   /* type I: PCM16, two rates:
                                                     * 44100 and 48000 Hz (0.19; SLOOP 2.5's / Felucca 1.1.5's form) */
    9, 5, 0x82, 0x05, UA_EP_MAXP & 0xFF, UA_EP_MAXP >> 8, 1, 0, 0,   /* EP2 IN isochronous async: packets of up to
                                                     * 49 frames at 48 kHz; UA_EP_MAXP scales the reservation to
                                                     * the selected channel count, with a bit-stuffing allowance */
    7, 0x25, 1, 1, 0, 0, 0,                         /* sampling-frequency control: the host's SET_CUR picks the rate */
