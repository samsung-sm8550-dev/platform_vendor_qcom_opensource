/*
 * Copyright (c) 2021-2022 Qualcomm Innovation Center, Inc. All rights reserved.
 * Copyright (c) 2019-2021, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

/*
 * Copyright (c) 2016 Intel Corporation
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#ifndef _DP_AUX_BRIDGE_H_
#define _DP_AUX_BRIDGE_H_

#include <linux/types.h>
#include <drm/drm_dp_helper.h>

/**
 * enum dp_aux_bridge_flag - DP aux bridge capability flag
 * DP_AUX_BRIDGE_HPD: HPD will be generated by DP aux bridge
 * DP_AUX_BRIDGE_MST: MST simulator is used by DP aux bridge
 */
enum dp_aux_bridge_flag {
	DP_AUX_BRIDGE_HPD = (1 << 0),
	DP_AUX_BRIDGE_MST = (1 << 1),
};

/**
 * struct dp_aux_bridge - DP aux bridge control structure
 * @of_node: device node pointer to the bridge
 * @dev_priv: pointer to the bridge driver's internal context
 * @flag: flag for capability
 * @mst_ctx: pointer to mst context when DP_AUX_BRIDGE_MST is set
 * @head: to keep track of all added bridges
 */
struct dp_aux_bridge {
#if IS_ENABLED(CONFIG_OF)
	struct device_node *of_node;
#endif /* CONFIG_OF */
	void *dev_priv;
	u32 flag;
	void *mst_ctx;
	struct list_head head;

	/**
	 * @register_hpd:
	 *
	 * This callback is invoked whenever bridge is registered
	 * for HPD handling
	 *
	 * The attach callback is optional.
	 *
	 * Host will pass HPD callback handle to bridge, with
	 * arguments @hpd_cb(void* dev, bool hpd, bool hpd_irq):
	 *
	 *     @dev: private handle passed in register_hpd
	 *     @hpd: true if HPD is high, false if HPD is low
	 *     @hpd_irq: true if this is a HPD irq. @hpd will be
	 *     ignored when hpd_irq is true.
	 *
	 * RETURNS:
	 *
	 * Zero on success, error code on failure.
	 */
	int (*register_hpd)(struct dp_aux_bridge *bridge,
			int (*hpd_cb)(void *, bool, bool), void *dev);

	/**
	 * @transfer:
	 *
	 * This callback is invoked whenever dp_aux transfer
	 * is called from host. Inside @transfer bridge can still
	 * call @drm_aux->transfer to trigger the actual
	 * DPCD/I2C transfer at host side.
	 *
	 * The attach callback is optional.
	 *
	 * RETURNS:
	 *
	 * Size of the bytes transferred, error code on failure.
	 */
	ssize_t (*transfer)(struct dp_aux_bridge *bridge,
			struct drm_dp_aux *drm_aux,
			struct drm_dp_aux_msg *msg);
};

/**
 * dp_aux_add_bridge - Register DP aux bridge
 * @bridge: bridge pointer
 * return: 0 if successful
 */
int dp_aux_add_bridge(struct dp_aux_bridge *bridge);

#if IS_ENABLED(CONFIG_OF)
/**
 * of_dp_aux_find_bridge - Find registered DP aux bridge
 * @np: device node pointer to the bridge
 * return: DP aux bridge pointer, NULL if not found
 */
struct dp_aux_bridge *of_dp_aux_find_bridge(struct device_node *np);
#endif /* CONFIG_OF */

#endif /* _DP_AUX_BRIDGE_H_ */

