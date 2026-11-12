/* SPDX-License-Identifier: GPL-3.0-only */
/* Host-only SIE and USB DMA model for production usb.c. Include before the
 * firmware translation unit. The real HAL header is read once; only subsequent
 * call sites are aliased, so no target HAL or firmware test hooks are needed.
 *
 * Define CF_USB_MOCK_CHECK(condition, message) to integrate assertions; its
 * default aborts on a model contract violation. Registers 0..15 are global;
 * regs_ep[endpoint][register-16] holds the indexed registers. To consume an IN
 * packet, first check its contents/ownership, then clear regs_ep[ep][1] bit0.
 * No clock is provided: the embedding test supplies its normal emulated HAL.
 * This model tests protocol logic and DMA ownership, not physical USB timing. */
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../firmware/hal/fm1_usb.h"
#ifndef CF_USB_MOCK_CHECK
#define CF_USB_MOCK_CHECK(condition, message) do { \
    if (!(condition)) { fprintf(stderr, "USB mock: %s\n", message); abort(); } \
} while (0)
#endif

static uint8_t cf_usb_mock_reg_global[32], cf_usb_mock_reg_ep[4][8], cf_usb_mock_reg_index, cf_usb_mock_reg_result, cf_usb_mock_hal_on;
static uint8_t *cf_usb_mock_dma_tx[4], *cf_usb_mock_dma_rx[4];
static uint32_t cf_usb_mock_dma_size[4], cf_usb_mock_dma_sends[4];
static uint8_t cf_usb_mock_dma_capture[1024], cf_usb_mock_control_reply[1024];
static uint32_t cf_usb_mock_control_size;
static int cf_usb_mock_control_stalled;
static int cf_usb_mock_stuck;                          /* injected bounded SIE handshake timeout */

static uint8_t *cf_usb_mock_reg_at(uint32_t r) { return r >= 16u ? &cf_usb_mock_reg_ep[cf_usb_mock_reg_index][r - 16u] : &cf_usb_mock_reg_global[r]; }
static void cf_usb_mock_reset(void) { cf_usb_mock_hal_on = 0; memset(cf_usb_mock_reg_global, 0, sizeof cf_usb_mock_reg_global); memset(cf_usb_mock_reg_ep, 0, sizeof cf_usb_mock_reg_ep); }
static void cf_usb_mock_attach(void *p) { cf_usb_mock_hal_on = 1; cf_usb_mock_dma_rx[0] = p; }
static void cf_usb_mock_off(void) { cf_usb_mock_hal_on = 0; }
static uint32_t cf_usb_mock_sie_on(void) { return cf_usb_mock_hal_on; }
static void cf_usb_mock_wr(uint32_t r, uint32_t v)
{
    if (cf_usb_mock_stuck) return;
    if (r == 14u) { cf_usb_mock_reg_index = (uint8_t)v; return; }
    if (r == 17u && !cf_usb_mock_reg_index) {
        if (v == 0x60u) { cf_usb_mock_control_stalled = 1; *cf_usb_mock_reg_at(r) = 4; }
        else if (v == 0x80u) *cf_usb_mock_reg_at(r) &= (uint8_t)~0x10u;
        else if (v == 0x40u || v == 0x48u) *cf_usb_mock_reg_at(r) = 0;
        else *cf_usb_mock_reg_at(r) = (uint8_t)(v & 2u);
        return;
    }
    if (r == 17u && (v == 0x48u || v == 0x40u)) v = 0;
    if (r == 20u && (v & 0x10u)) v &= ~1u;
    *cf_usb_mock_reg_at(r) = (uint8_t)v;
}
static void cf_usb_mock_rd(uint32_t r)
{
    if (cf_usb_mock_stuck) return;
    cf_usb_mock_reg_result = *cf_usb_mock_reg_at(r);
    if (r >= 2u && r <= 6u) *cf_usb_mock_reg_at(r) = 0; /* interrupt status is read-to-clear */
}
static uint32_t cf_usb_mock_done(void) { return !cf_usb_mock_stuck; }
static uint32_t cf_usb_mock_data(void) { return cf_usb_mock_reg_result; }
static void cf_usb_mock_ep0_buf(void *p) { cf_usb_mock_dma_rx[0] = p; }
static void cf_usb_mock_txbuf(uint32_t ep, void *p) { cf_usb_mock_dma_tx[ep] = p; }
static void cf_usb_mock_rxbuf(uint32_t ep, void *p) { cf_usb_mock_dma_rx[ep] = p; }
static void cf_usb_mock_ep0_send(void *p, uint32_t n)
{
    CF_USB_MOCK_CHECK(cf_usb_mock_control_size + n <= sizeof cf_usb_mock_control_reply, "EP0 response exceeds model storage");
    if (cf_usb_mock_control_size + n <= sizeof cf_usb_mock_control_reply) memcpy(cf_usb_mock_control_reply + cf_usb_mock_control_size, p, n);
    cf_usb_mock_control_size += n;
}
static void cf_usb_mock_send(uint32_t ep, void *p, uint32_t n)
{
    CF_USB_MOCK_CHECK(ep > 0u && ep < 4u, "DMA endpoint out of bounds");
    if (!ep || ep >= 4u) return;
    CF_USB_MOCK_CHECK(!(cf_usb_mock_reg_ep[ep][1] & 1u), "armed DMA packet replaced before host took it");
    cf_usb_mock_dma_tx[ep] = p; cf_usb_mock_dma_size[ep] = n; cf_usb_mock_dma_sends[ep]++;
    if (ep == 2u) {
        CF_USB_MOCK_CHECK(n <= sizeof cf_usb_mock_dma_capture, "capture packet exceeds model storage");
        if (n <= sizeof cf_usb_mock_dma_capture) memcpy(cf_usb_mock_dma_capture, p, n);
    }
}
static void cf_usb_mock_sync(void) {}
static void cf_usb_mock_enable(uint32_t mask) { (void)mask; }
static uint32_t cf_usb_mock_sof(void) { return 1; }

#define fm1_usb_reset cf_usb_mock_reset
#define fm1_usb_attach cf_usb_mock_attach
#define fm1_usb_off cf_usb_mock_off
#define fm1_usb_sie_on cf_usb_mock_sie_on
#define fm1_usb_sie_wr_start cf_usb_mock_wr
#define fm1_usb_sie_rd_start cf_usb_mock_rd
#define fm1_usb_sie_done cf_usb_mock_done
#define fm1_usb_sie_data cf_usb_mock_data
#define fm1_usb_ep0_buf cf_usb_mock_ep0_buf
#define fm1_usb_ep_txbuf cf_usb_mock_txbuf
#define fm1_usb_ep_rxbuf cf_usb_mock_rxbuf
#define fm1_usb_ep0_send cf_usb_mock_ep0_send
#define fm1_usb_ep_send cf_usb_mock_send
#define fm1_usb_rx_sync cf_usb_mock_sync
#define fm1_usb_ep_enable cf_usb_mock_enable
#define fm1_usb_sof_take cf_usb_mock_sof
