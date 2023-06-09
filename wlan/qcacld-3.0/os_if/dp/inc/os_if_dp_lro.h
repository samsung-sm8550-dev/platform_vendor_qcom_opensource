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
 * DOC : osif_dp_lro.h
 *
 * WLAN Host Device Driver file for DP LRO support.
 *
 */

#ifndef _OSIF_DP_LRO_H_
#define _OSIF_DP_LRO_H_

#include <qdf_types.h>
#include <wlan_objmgr_vdev_obj.h>

#if defined(FEATURE_LRO)
/**
 * osif_dp_lro_rx() - Handle Rx procesing via LRO
 * @vdev: Vdev obj mgr
 * @nbuf: network buffer
 *
 * Return: QDF_STATUS_SUCCESS if processed via LRO or non zero return code
 */
QDF_STATUS osif_dp_lro_rx(qdf_netdev_t dev, qdf_nbuf_t nbuf);

void osif_dp_lro_display_stats(struct wlan_objmgr_vdev *vdev);

/**
 * osif_dp_lro_set_reset() - vendor command for Disable/Enable LRO
 * @vdev: Vdev obj mgr
 * @enable_flag: enable or disable LRO.
 *
 * Return: none
 */
QDF_STATUS
osif_dp_lro_set_reset(struct wlan_objmgr_vdev *vdev, uint8_t enable_flag);

#else
static inline
QDF_STATUS osif_dp_lro_rx(qdf_netdev_t dev, qdf_nbuf_t nbuf)
{
	return QDF_STATUS_E_NOSUPPORT;
}

static inline void osif_dp_lro_display_stats(struct wlan_objmgr_vdev *vdev)
{
}

static inline QDF_STATUS
osif_dp_lro_set_reset(struct wlan_objmgr_vdev *vdev, uint8_t enable_flag)
{
	return 0;
}
#endif /* FEATURE_LRO */

#endif /* _OSIF_DP_LRO_H_ */
