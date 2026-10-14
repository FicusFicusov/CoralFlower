/* Host preview of the flora engine: plays a scripted chord progression and writes indexed frames.
 *
 *   cc -O2 -Ifirmware/src/flora -o build/flora_preview host/flora_preview.c firmware/src/flora/flora.c firmware/src/flora/flora_canvas.c
 *   build/flora_preview OUT_DIR [fps] [seconds] [species: 0 plant, 1 coral]
 *
 * Each frame is W*H palette indices (frame_NNNN.idx); tools/flora_sheet.py turns them into PNGs and a GIF.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "flora.h"

#define W 120
#define H 74

typedef struct {
    uint32_t at_ms;
    fl_chord_t c;
} step_t;

static fl_chord_t mk(int root_midi, int quality, const int *iv, int n, int bass)
{
    fl_chord_t c;
    int i;
    memset(&c, 0, sizeof(c));
    c.n = (uint8_t)n;
    for (i = 0; i < n; i++)
        c.note[i] = (uint8_t)(root_midi + iv[i]);
    c.root = (uint8_t)(root_midi % 12);
    c.quality = (uint8_t)quality;
    c.bass = (int8_t)bass;
    return c;
}

int main(int argc, char **argv)
{
    static flora_t f;
    static uint8_t px[W * H];
    flc_t cv = {px, W, H};
    const char *out = argc > 1 ? argv[1] : "build/frames";
    int fps = argc > 2 ? atoi(argv[2]) : 20;
    int secs = argc > 3 ? atoi(argv[3]) : 24;
    int species = argc > 4 ? atoi(argv[4]) : 0;
    static const int maj[] = {0, 4, 7}, min[] = {0, 3, 7}, maj7[] = {0, 4, 7, 11}, sus4[] = {0, 5, 7},
                     min9[] = {0, 3, 7, 10, 14}, maj9[] = {0, 4, 7, 11, 14}, dim[] = {0, 3, 6},
                     maj69[] = {0, 4, 7, 9, 14}, m11[] = {0, 3, 7, 10, 14, 17};
    step_t seq[12];
    int nseq = 0, si = 0, frame, nframes, dt;
    char path[512];

    seq[nseq].at_ms = 3000; seq[nseq++].c = mk(60, FL_Q_MAJOR, maj, 3, 48);
    seq[nseq].at_ms = 5500; seq[nseq++].c = mk(57, FL_Q_MINOR, min, 3, 45);
    seq[nseq].at_ms = 8000; seq[nseq++].c = mk(53, FL_Q_MAJOR, maj7, 4, 41);
    seq[nseq].at_ms = 10500; seq[nseq++].c = mk(55, FL_Q_SUS4, sus4, 3, 43);
    seq[nseq].at_ms = 13000; seq[nseq++].c = mk(52, FL_Q_MINOR, min9, 5, 40);
    seq[nseq].at_ms = 15500; seq[nseq++].c = mk(60, FL_Q_MAJOR, maj9, 5, 48);
    seq[nseq].at_ms = 18000; seq[nseq++].c = mk(59, FL_Q_DIM, dim, 3, 47);
    seq[nseq].at_ms = 20000; seq[nseq++].c = mk(62, FL_Q_MINOR, m11, 6, 38);
    seq[nseq].at_ms = 22000; seq[nseq++].c = mk(53, FL_Q_MAJOR, maj69, 5, 41);

    flora_init(&f, 12345, W / 2, H + 1);
    flora_set_species(&f, species);
    dt = 1000 / fps;
    nframes = fps * secs;
    for (frame = 0; frame < nframes; frame++) {
        uint32_t t = (uint32_t)(frame * dt);
        FILE *fp;
        while (si < nseq && t >= seq[si].at_ms) {
            int i;
            flora_set_chord(&f, &seq[si].c);
            for (i = 0; i < seq[si].c.n; i++)
                flora_note_on(&f, seq[si].c.note[i], 100);
            si++;
        }
        /* a fake envelope: loud after each chord, decaying */
        if (si > 0) {
            uint32_t since = t - seq[si - 1].at_ms;
            flora_set_level(&f, (uint16_t)(since < 1500 ? 52000 - since * 30 : 7000));
        }
        flora_tick(&f, (uint32_t)dt);
        flora_render(&f, &cv);
        snprintf(path, sizeof(path), "%s/frame_%04d.idx", out, frame);
        fp = fopen(path, "wb");
        if (!fp) {
            perror(path);
            return 1;
        }
        fwrite(px, 1, sizeof(px), fp);
        fclose(fp);
    }
    printf("%d frames, %d segments at the end\n", nframes, f.nseg);
    return 0;
}
