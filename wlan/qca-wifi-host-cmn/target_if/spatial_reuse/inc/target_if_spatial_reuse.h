/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * Permission to use, copy, modify, and/or distribute this software for
 * any purpose with or without fee is hereby granted, provided that the
 * above copyright notice and this permission notice appear in all
 * copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
 * AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/**
 * DOC: target_if_spatial_reuse.h
 */

#ifndef _TARGET_IF_SPATIAL_REUSE_H_
#define _TARGET_IF_SPATIAL_REUSE_H_

#include <wlan_lmac_if_def.h>
#include <target_if.h>

#if defined WLAN_FEATURE_11AX

#define NON_SRG_PD_SR_DISALLOWED 0x02
#define NON_SRG_OFFSET_PRESENT 0x04
#define NON_SRG_SPR_ENABLE 1
#define NON_SRG_SPR_ENABLE_POS 31
#define NON_SRG_SPR_ENABLE_SIZE 1
#define NON_SRG_PARAM_VAL_DBM_UNIT 1
#define NON_SRG_PARAM_VAL_DBM_POS 29
#define NON_SRG_PARAM_VAL_DBM_SIZE 1
#define NON_SRG_MAX_PD_OFFSET_POS 0
#define NON_SRG_MAX_PD_OFFSET_SIZE 8
#define WILDCARD_PDEV_ID 0x0

/**
 * target_if_spatial_reuse_register_tx_ops  - tx_ops registration
 * @tx_ops: wlan_lmac_if_tx_ops
 *
 * Return: void
 */
void
target_if_spatial_reuse_register_tx_ops(struct wlan_lmac_if_tx_ops *tx_ops);
#else

static inline void target_if_spatial_reuse_register_tx_ops(
					struct wlan_lmac_if_tx_ops *tx_ops)
{}

#endif

#endif /* _TARGET_IF_SPATIAL_REUSE_H_ */
