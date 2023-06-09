// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/io.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/random.h>
#include <asm/errno.h>
#include <linux/timer.h>
#include <media/cam_icp.h>
#include <linux/iopoll.h>

#include "cam_presil_hw_access.h"
#include "cam_io_util.h"
#include "hfi_reg.h"
#include "hfi_sys_defs.h"
#include "hfi_session_defs.h"
#include "hfi_intf.h"
#include "cam_icp_hw_mgr_intf.h"
#include "cam_debug_util.h"
#include "cam_compat.h"
#include "cam_soc_util.h"

#define HFI_VERSION_INFO_MAJOR_VAL  1
#define HFI_VERSION_INFO_MINOR_VAL  1
#define HFI_VERSION_INFO_STEP_VAL   0
#define HFI_VERSION_INFO_STEP_VAL   0
#define HFI_VERSION_INFO_MAJOR_BMSK  0xFF000000
#define HFI_VERSION_INFO_MAJOR_SHFT  24
#define HFI_VERSION_INFO_MINOR_BMSK  0xFFFF00
#define HFI_VERSION_INFO_MINOR_SHFT  8
#define HFI_VERSION_INFO_STEP_BMSK   0xFF
#define HFI_VERSION_INFO_STEP_SHFT  0

/* TO DO Lower timeout value */
#define HFI_POLL_DELAY_US 10
#define HFI_POLL_TIMEOUT_US 1500000

static struct hfi_info *g_hfi;
unsigned int g_icp_mmu_hdl;

static DEFINE_MUTEX(hfi_cmd_q_mutex);
static DEFINE_MUTEX(hfi_msg_q_mutex);

static int cam_hfi_presil_setup(struct hfi_mem_info *hfi_mem);
static int cam_hfi_presil_set_init_request(void);

#ifndef CONFIG_CAM_PRESIL
static void hfi_irq_raise(struct hfi_info *hfi)
{
	if (hfi->ops.irq_raise)
		hfi->ops.irq_raise(hfi->priv);
}
#endif

static void hfi_irq_enable(struct hfi_info *hfi)
{
	if (hfi->ops.irq_enable)
		hfi->ops.irq_enable(hfi->priv);
}

static void __iomem *hfi_iface_addr(struct hfi_info *hfi)
{
	void __iomem *ret = NULL;

	if (hfi->ops.iface_addr)
		ret = hfi->ops.iface_addr(hfi->priv);

	return IS_ERR_OR_NULL(ret) ? NULL : ret;
}

static void hfi_queue_dump(uint32_t *dwords, int count)
{
	int i;
	int rows;
	int remaining;

	rows = count / 4;
	remaining = count % 4;

	for (i = 0; i < rows; i++, dwords += 4)
		CAM_DBG(CAM_HFI,
			"word[%04d]: 0x%08x 0x%08x 0x%08x 0x%08x",
			i * 4, dwords[0], dwords[1], dwords[2], dwords[3]);

	if (remaining == 1)
		CAM_DBG(CAM_HFI, "word[%04d]: 0x%08x", rows * 4, dwords[0]);
	else if (remaining == 2)
		CAM_DBG(CAM_HFI, "word[%04d]: 0x%08x 0x%08x",
			rows * 4, dwords[0], dwords[1]);
	else if (remaining == 3)
		CAM_DBG(CAM_HFI, "word[%04d]: 0x%08x 0x%08x 0x%08x",
			rows * 4, dwords[0], dwords[1], dwords[2]);
}

void cam_hfi_mini_dump(struct hfi_mini_dump_info *dst)
{
	struct hfi_mem_info *hfi_mem = &g_hfi->map;
	struct hfi_qtbl *qtbl;
	struct hfi_q_hdr *q_hdr;
	uint32_t *dwords;
	int num_dwords;

	if (!hfi_mem) {
		CAM_ERR(CAM_HFI, "hfi mem info NULL... unable to dump queues");
		return;
	}

	qtbl = (struct hfi_qtbl *)hfi_mem->qtbl.kva;
	q_hdr = &qtbl->q_hdr[Q_CMD];
	dwords = (uint32_t *)hfi_mem->cmd_q.kva;
	num_dwords = ICP_CMD_Q_SIZE_IN_BYTES >> BYTE_WORD_SHIFT;
	memcpy(dst->cmd_q, dwords, ICP_CMD_Q_SIZE_IN_BYTES);

	q_hdr = &qtbl->q_hdr[Q_MSG];
	dwords = (uint32_t *)hfi_mem->msg_q.kva;
	memcpy(dst->msg_q, dwords, ICP_CMD_Q_SIZE_IN_BYTES);
	dst->msg_q_state = g_hfi->msg_q_state;
	dst->cmd_q_state = g_hfi->cmd_q_state;
}

void cam_hfi_queue_dump(bool dump_queue_data)
{
	struct hfi_mem_info *hfi_mem = &g_hfi->map;
	struct hfi_qtbl *qtbl;
	struct hfi_q_hdr *q_hdr;
	uint32_t *dwords;
	int num_dwords;

	if (!hfi_mem) {
		CAM_ERR(CAM_HFI, "hfi mem info NULL... unable to dump queues");
		return;
	}

	qtbl = (struct hfi_qtbl *)hfi_mem->qtbl.kva;
	CAM_INFO(CAM_HFI,
		"qtbl header: version=0x%08x tbl_size=%u numq=%u qhdr_size=%u",
		qtbl->q_tbl_hdr.qtbl_version,
		qtbl->q_tbl_hdr.qtbl_size,
		qtbl->q_tbl_hdr.qtbl_num_q,
		qtbl->q_tbl_hdr.qtbl_qhdr_size);

	q_hdr = &qtbl->q_hdr[Q_CMD];
	CAM_INFO(CAM_HFI,
		"cmd_q: addr=0x%08x size=%u read_idx=%u write_idx=%u",
		hfi_mem->cmd_q.iova,
		q_hdr->qhdr_q_size,
		q_hdr->qhdr_read_idx,
		q_hdr->qhdr_write_idx);

	dwords = (uint32_t *)hfi_mem->cmd_q.kva;
	num_dwords = ICP_CMD_Q_SIZE_IN_BYTES >> BYTE_WORD_SHIFT;

	if (dump_queue_data)
		hfi_queue_dump(dwords, num_dwords);

	q_hdr = &qtbl->q_hdr[Q_MSG];
	CAM_INFO(CAM_HFI,
		"msg_q: addr=0x%08x size=%u read_idx=%u write_idx=%u",
		hfi_mem->msg_q.iova,
		q_hdr->qhdr_q_size,
		q_hdr->qhdr_read_idx,
		q_hdr->qhdr_write_idx);

	dwords = (uint32_t *)hfi_mem->msg_q.kva;
	num_dwords = ICP_MSG_Q_SIZE_IN_BYTES >> BYTE_WORD_SHIFT;

	if (dump_queue_data)
		hfi_queue_dump(dwords, num_dwords);
}

#ifndef CONFIG_CAM_PRESIL
int hfi_write_cmd(void *cmd_ptr)
{
	uint32_t size_in_words, empty_space, new_write_idx, read_idx, temp;
	uint32_t *write_q, *write_ptr;
	struct hfi_qtbl *q_tbl;
	struct hfi_q_hdr *q;
	int rc = 0;

	if (!cmd_ptr) {
		CAM_ERR(CAM_HFI, "command is null");
		return -EINVAL;
	}

	mutex_lock(&hfi_cmd_q_mutex);
	if (!g_hfi) {
		CAM_ERR(CAM_HFI, "HFI interface not setup");
		rc = -ENODEV;
		goto err;
	}

	if (g_hfi->hfi_state != HFI_READY ||
		!g_hfi->cmd_q_state) {
		CAM_ERR(CAM_HFI, "HFI state: %u, cmd q state: %u",
			g_hfi->hfi_state, g_hfi->cmd_q_state);
		rc = -ENODEV;
		goto err;
	}

	q_tbl = (struct hfi_qtbl *)g_hfi->map.qtbl.kva;
	q = &q_tbl->q_hdr[Q_CMD];

	write_q = (uint32_t *)g_hfi->map.cmd_q.kva;

	size_in_words = (*(uint32_t *)cmd_ptr) >> BYTE_WORD_SHIFT;
	if (!size_in_words) {
		CAM_DBG(CAM_HFI, "failed");
		rc = -EINVAL;
		goto err;
	}

	read_idx = q->qhdr_read_idx;
	empty_space = (q->qhdr_write_idx >= read_idx) ?
		(q->qhdr_q_size - (q->qhdr_write_idx - read_idx)) :
		(read_idx - q->qhdr_write_idx);
	if (empty_space <= size_in_words) {
		CAM_ERR(CAM_HFI, "failed: empty space %u, size_in_words %u",
			empty_space, size_in_words);
		rc = -EIO;
		goto err;
	}

	new_write_idx = q->qhdr_write_idx + size_in_words;
	write_ptr = (uint32_t *)(write_q + q->qhdr_write_idx);

	if (new_write_idx < q->qhdr_q_size) {
		memcpy(write_ptr, (uint8_t *)cmd_ptr,
			size_in_words << BYTE_WORD_SHIFT);
	} else {
		new_write_idx -= q->qhdr_q_size;
		temp = (size_in_words - new_write_idx) << BYTE_WORD_SHIFT;
		memcpy(write_ptr, (uint8_t *)cmd_ptr, temp);
		memcpy(write_q, (uint8_t *)cmd_ptr + temp,
			new_write_idx << BYTE_WORD_SHIFT);
	}

	/*
	 * To make sure command data in a command queue before
	 * updating write index
	 */
	wmb();

	q->qhdr_write_idx = new_write_idx;

	/*
	 * Before raising interrupt make sure command data is ready for
	 * firmware to process
	 */
	wmb();
	hfi_irq_raise(g_hfi);

	/* Ensure HOST2ICP trigger is received by FW */
	wmb();
err:
	mutex_unlock(&hfi_cmd_q_mutex);
	return rc;
}

int hfi_read_message(uint32_t *pmsg, uint8_t q_id,
	uint32_t *words_read)
{
	struct hfi_qtbl *q_tbl_ptr;
	struct hfi_q_hdr *q;
	uint32_t new_read_idx, size_in_words, word_diff, temp;
	uint32_t *read_q, *read_ptr, *write_ptr;
	uint32_t size_upper_bound = 0;
	int rc = 0;

	if (!pmsg) {
		CAM_ERR(CAM_HFI, "Invalid msg");
		return -EINVAL;
	}

	if (!((q_id == Q_MSG) || (q_id == Q_DBG))) {
		CAM_ERR(CAM_HFI, "Invalid q :%u", q_id);
		return -EINVAL;
	}

	mutex_lock(&hfi_msg_q_mutex);
	if (!g_hfi) {
		CAM_ERR(CAM_HFI, "hfi not set up yet");
		rc = -ENODEV;
		goto err;
	}

	if ((g_hfi->hfi_state != HFI_READY) ||
		!g_hfi->msg_q_state) {
		CAM_ERR(CAM_HFI, "hfi state: %u, msg q state: %u",
			g_hfi->hfi_state, g_hfi->msg_q_state);
		rc = -ENODEV;
		goto err;
	}

	q_tbl_ptr = (struct hfi_qtbl *)g_hfi->map.qtbl.kva;
	q = &q_tbl_ptr->q_hdr[q_id];

	if (q->qhdr_read_idx == q->qhdr_write_idx) {
		CAM_DBG(CAM_HFI, "Q not ready, state:%u, r idx:%u, w idx:%u",
			g_hfi->hfi_state, q->qhdr_read_idx, q->qhdr_write_idx);
		rc = -EIO;
		goto err;
	}

	size_upper_bound = q->qhdr_q_size;
	if (q_id == Q_MSG)
		read_q = (uint32_t *)g_hfi->map.msg_q.kva;
	else
		read_q = (uint32_t *)g_hfi->map.dbg_q.kva;

	read_ptr = (uint32_t *)(read_q + q->qhdr_read_idx);
	write_ptr = (uint32_t *)(read_q + q->qhdr_write_idx);

	if (write_ptr > read_ptr)
		size_in_words = write_ptr - read_ptr;
	else {
		word_diff = read_ptr - write_ptr;
		size_in_words =  q->qhdr_q_size -  word_diff;
	}

	if ((size_in_words == 0) ||
		(size_in_words > size_upper_bound)) {
		CAM_ERR(CAM_HFI, "invalid HFI message packet size - 0x%08x",
			size_in_words << BYTE_WORD_SHIFT);
		q->qhdr_read_idx = q->qhdr_write_idx;
		rc = -EIO;
		goto err;
	}

	new_read_idx = q->qhdr_read_idx + size_in_words;

	if (new_read_idx < q->qhdr_q_size) {
		memcpy(pmsg, read_ptr, size_in_words << BYTE_WORD_SHIFT);
	} else {
		new_read_idx -= q->qhdr_q_size;
		temp = (size_in_words - new_read_idx) << BYTE_WORD_SHIFT;
		memcpy(pmsg, read_ptr, temp);
		memcpy((uint8_t *)pmsg + temp, read_q,
			new_read_idx << BYTE_WORD_SHIFT);
	}

	q->qhdr_read_idx = new_read_idx;
	*words_read = size_in_words;
	/* Memory Barrier to make sure message
	 * queue parameters are updated after read
	 */
	wmb();
err:
	mutex_unlock(&hfi_msg_q_mutex);
	return rc;
}
#endif /* #ifndef CONFIG_CAM_PRESIL */

int hfi_cmd_ubwc_config(uint32_t *ubwc_cfg)
{
	uint8_t *prop;
	struct hfi_cmd_prop *dbg_prop;
	uint32_t size = 0;

	size = sizeof(struct hfi_cmd_prop) +
		sizeof(struct hfi_cmd_ubwc_cfg);

	CAM_DBG(CAM_HFI,
		"size of ubwc %u, ubwc_cfg [rd-0x%x,wr-0x%x]",
		size, ubwc_cfg[0],  ubwc_cfg[1]);

	prop = kzalloc(size, GFP_KERNEL);
	if (!prop)
		return -ENOMEM;

	dbg_prop = (struct hfi_cmd_prop *)prop;
	dbg_prop->size = size;
	dbg_prop->pkt_type = HFI_CMD_SYS_SET_PROPERTY;
	dbg_prop->num_prop = 1;
	dbg_prop->prop_data[0] = HFI_PROP_SYS_UBWC_CFG;
	dbg_prop->prop_data[1] = ubwc_cfg[0];
	dbg_prop->prop_data[2] = ubwc_cfg[1];

	hfi_write_cmd(prop);
	kfree(prop);

	return 0;
}

int hfi_cmd_ubwc_config_ext(uint32_t *ubwc_ipe_cfg,
	uint32_t *ubwc_bps_cfg)
{
	uint8_t *prop;
	struct hfi_cmd_prop *dbg_prop;
	uint32_t size = 0;

	size = sizeof(struct hfi_cmd_prop) +
		sizeof(struct hfi_cmd_ubwc_cfg_ext);

	CAM_DBG(CAM_HFI,
		"size of ubwc %u, ubwc_ipe_cfg[rd-0x%x,wr-0x%x] ubwc_bps_cfg[rd-0x%x,wr-0x%x]",
		size, ubwc_ipe_cfg[0], ubwc_ipe_cfg[1],
		ubwc_bps_cfg[0], ubwc_bps_cfg[1]);

	prop = kzalloc(size, GFP_KERNEL);
	if (!prop)
		return -ENOMEM;

	dbg_prop = (struct hfi_cmd_prop *)prop;
	dbg_prop->size = size;
	dbg_prop->pkt_type = HFI_CMD_SYS_SET_PROPERTY;
	dbg_prop->num_prop = 1;
	dbg_prop->prop_data[0] = HFI_PROPERTY_SYS_UBWC_CONFIG_EX;
	dbg_prop->prop_data[1] = ubwc_bps_cfg[0];
	dbg_prop->prop_data[2] = ubwc_bps_cfg[1];
	dbg_prop->prop_data[3] = ubwc_ipe_cfg[0];
	dbg_prop->prop_data[4] = ubwc_ipe_cfg[1];

	hfi_write_cmd(prop);
	kfree(prop);

	return 0;
}


int hfi_enable_ipe_bps_pc(bool enable, uint32_t core_info)
{
	uint8_t *prop;
	struct hfi_cmd_prop *dbg_prop;
	uint32_t size = 0;

	size = sizeof(struct hfi_cmd_prop) +
		sizeof(struct hfi_ipe_bps_pc);

	prop = kzalloc(size, GFP_KERNEL);
	if (!prop)
		return -ENOMEM;

	dbg_prop = (struct hfi_cmd_prop *)prop;
	dbg_prop->size = size;
	dbg_prop->pkt_type = HFI_CMD_SYS_SET_PROPERTY;
	dbg_prop->num_prop = 1;
	dbg_prop->prop_data[0] = HFI_PROP_SYS_IPEBPS_PC;
	dbg_prop->prop_data[1] = enable;
	dbg_prop->prop_data[2] = core_info;

	hfi_write_cmd(prop);
	kfree(prop);

	return 0;
}

int hfi_set_debug_level(u64 icp_dbg_type, uint32_t lvl)
{
	uint8_t *prop;
	struct hfi_cmd_prop *dbg_prop;
	uint32_t size = 0, val;

	val = HFI_DEBUG_MSG_LOW |
		HFI_DEBUG_MSG_MEDIUM |
		HFI_DEBUG_MSG_HIGH |
		HFI_DEBUG_MSG_ERROR |
		HFI_DEBUG_MSG_FATAL |
		HFI_DEBUG_MSG_PERF |
		HFI_DEBUG_CFG_WFI |
		HFI_DEBUG_CFG_ARM9WD;

	if (lvl > val)
		return -EINVAL;

	if (g_hfi)
		g_hfi->dbg_lvl = lvl;

	size = sizeof(struct hfi_cmd_prop) +
		sizeof(struct hfi_debug);

	prop = kzalloc(size, GFP_KERNEL);
	if (!prop)
		return -ENOMEM;

	dbg_prop = (struct hfi_cmd_prop *)prop;
	dbg_prop->size = size;
	dbg_prop->pkt_type = HFI_CMD_SYS_SET_PROPERTY;
	dbg_prop->num_prop = 1;
	dbg_prop->prop_data[0] = HFI_PROP_SYS_DEBUG_CFG;
	dbg_prop->prop_data[1] = lvl;
	dbg_prop->prop_data[2] = icp_dbg_type;
	hfi_write_cmd(prop);

	kfree(prop);

	return 0;
}

int hfi_set_fw_dump_levels(uint32_t hang_dump_lvl,
	uint32_t ram_dump_lvl)
{
	uint8_t *prop = NULL;
	struct hfi_cmd_prop *fw_dump_level_switch_prop = NULL;
	uint32_t size = 0;

	CAM_DBG(CAM_HFI, "fw dump ENTER");

	size = sizeof(struct hfi_cmd_prop) + sizeof(uint32_t);
	prop = kzalloc(size, GFP_KERNEL);
	if (!prop)
		return -ENOMEM;

	fw_dump_level_switch_prop = (struct hfi_cmd_prop *)prop;
	fw_dump_level_switch_prop->size = size;
	fw_dump_level_switch_prop->pkt_type = HFI_CMD_SYS_SET_PROPERTY;
	fw_dump_level_switch_prop->num_prop = 1;
	fw_dump_level_switch_prop->prop_data[0] = HFI_PROP_SYS_FW_DUMP_CFG;
	fw_dump_level_switch_prop->prop_data[1] = hang_dump_lvl;

	/* Write hang dump level */
	hfi_write_cmd(prop);

	/* Update and write ramdump level */
	fw_dump_level_switch_prop->prop_data[0] = HFI_PROPERTY_SYS_RAMDUMP_MODE;
	fw_dump_level_switch_prop->prop_data[1] = ram_dump_lvl;

	hfi_write_cmd(prop);
	CAM_DBG(CAM_HFI,
		"prop->size = %d prop->pkt_type = %d prop->num_prop = %d hang_dump_lvl = %u ram_dump_lvl = %u",
		fw_dump_level_switch_prop->size,
		fw_dump_level_switch_prop->pkt_type,
		fw_dump_level_switch_prop->num_prop,
		hang_dump_lvl, ram_dump_lvl);

	kfree(prop);
	return 0;
}

int hfi_send_freq_info(int32_t freq)
{
	uint8_t *prop = NULL;
	struct hfi_cmd_prop *dbg_prop = NULL;
	uint32_t size = 0;

	if (!g_hfi) {
		CAM_ERR(CAM_HFI, "HFI interface not setup");
		return -ENODEV;
	}

	if (!(g_hfi->dbg_lvl & HFI_DEBUG_MSG_PERF))
		return -EINVAL;

	size = sizeof(struct hfi_cmd_prop) + sizeof(freq);
	prop = kzalloc(size, GFP_KERNEL);
	if (!prop)
		return -ENOMEM;

	dbg_prop = (struct hfi_cmd_prop *)prop;
	dbg_prop->size = size;
	dbg_prop->pkt_type = HFI_CMD_SYS_SET_PROPERTY;
	dbg_prop->num_prop = 1;
	dbg_prop->prop_data[0] = HFI_PROPERTY_SYS_ICP_HW_FREQUENCY;
	dbg_prop->prop_data[1] = freq;

	CAM_DBG(CAM_HFI, "prop->size = %d\n"
			 "prop->pkt_type = %d\n"
			 "prop->num_prop = %d\n"
			 "prop->prop_data[0] = %d\n"
			 "prop->prop_data[1] = %d\n"
			 "dbg_lvl = 0x%x\n",
			 dbg_prop->size,
			 dbg_prop->pkt_type,
			 dbg_prop->num_prop,
			 dbg_prop->prop_data[0],
			 dbg_prop->prop_data[1],
			 g_hfi->dbg_lvl);

	hfi_write_cmd(prop);
	kfree(prop);
	return 0;
}

void hfi_send_system_cmd(uint32_t type, uint64_t data, uint32_t size)
{
	switch (type) {
	case HFI_CMD_SYS_INIT: {
		struct hfi_cmd_sys_init init;

		init.size = sizeof(struct hfi_cmd_sys_init);
		init.pkt_type = type;
		hfi_write_cmd(&init);
	}
		break;
	case HFI_CMD_SYS_PC_PREP: {
		struct hfi_cmd_pc_prep prep;

		prep.size = sizeof(struct hfi_cmd_pc_prep);
		prep.pkt_type = type;
		hfi_write_cmd(&prep);
	}
		break;
	case HFI_CMD_SYS_SET_PROPERTY: {
		struct hfi_cmd_prop prop;

		if ((uint32_t)data == (uint32_t)HFI_PROP_SYS_DEBUG_CFG) {
			prop.size = sizeof(struct hfi_cmd_prop);
			prop.pkt_type = type;
			prop.num_prop = 1;
			prop.prop_data[0] = HFI_PROP_SYS_DEBUG_CFG;
			hfi_write_cmd(&prop);
		}
	}
		break;
	case HFI_CMD_SYS_GET_PROPERTY:
		break;
	case HFI_CMD_SYS_PING: {
		struct hfi_cmd_ping_pkt ping;

		ping.size = sizeof(struct hfi_cmd_ping_pkt);
		ping.pkt_type = type;
		ping.user_data = (uint64_t)data;
		hfi_write_cmd(&ping);
	}
		break;
	case HFI_CMD_SYS_RESET: {
		struct hfi_cmd_sys_reset_pkt reset;

		reset.size = sizeof(struct hfi_cmd_sys_reset_pkt);
		reset.pkt_type = type;
		reset.user_data = (uint64_t)data;
		hfi_write_cmd(&reset);
	}
		break;
	case HFI_CMD_IPEBPS_CREATE_HANDLE: {
		struct hfi_cmd_create_handle handle;

		handle.size = sizeof(struct hfi_cmd_create_handle);
		handle.pkt_type = type;
		handle.handle_type = (uint32_t)data;
		handle.user_data1 = 0;
		hfi_write_cmd(&handle);
	}
		break;
	case HFI_CMD_IPEBPS_ASYNC_COMMAND_INDIRECT:
		break;
	default:
		CAM_ERR(CAM_HFI, "command not supported :%d", type);
		break;
	}
}


int hfi_get_hw_caps(void *query_buf)
{
	int i = 0;
	struct cam_icp_query_cap_cmd *query_cmd = NULL;

	if (!query_buf) {
		CAM_ERR(CAM_HFI, "query buf is NULL");
		return -EINVAL;
	}

	query_cmd = (struct cam_icp_query_cap_cmd *)query_buf;
	query_cmd->fw_version.major = 0x12;
	query_cmd->fw_version.minor = 0x12;
	query_cmd->fw_version.revision = 0x12;

	query_cmd->api_version.major = 0x13;
	query_cmd->api_version.minor = 0x13;
	query_cmd->api_version.revision = 0x13;

	query_cmd->num_ipe = 2;
	query_cmd->num_bps = 1;

	for (i = 0; i < CAM_ICP_DEV_TYPE_MAX; i++) {
		query_cmd->dev_ver[i].dev_type = i;
		query_cmd->dev_ver[i].hw_ver.major = 0x34 + i;
		query_cmd->dev_ver[i].hw_ver.minor = 0x34 + i;
		query_cmd->dev_ver[i].hw_ver.incr = 0x34 + i;
	}
	return 0;
}

int cam_hfi_resume(struct hfi_mem_info *hfi_mem)
{
	int rc = 0;
	uint32_t fw_version, status = 0;
	void __iomem *icp_base = hfi_iface_addr(g_hfi);

	if (!icp_base) {
		CAM_ERR(CAM_HFI, "invalid HFI interface address");
		return -EINVAL;
	}

	if (cam_common_read_poll_timeout(icp_base +
		    HFI_REG_ICP_HOST_INIT_RESPONSE,
		    HFI_POLL_DELAY_US, HFI_POLL_TIMEOUT_US,
		    (uint32_t)UINT_MAX, ICP_INIT_RESP_SUCCESS, &status)) {
	    CAM_ERR(CAM_HFI, "response poll timed out: status=0x%08x",
		    status);
	    return -ETIMEDOUT;
	}

	hfi_irq_enable(g_hfi);

	fw_version = cam_io_r(icp_base + HFI_REG_FW_VERSION);
	CAM_DBG(CAM_HFI, "fw version : [%x]", fw_version);

	cam_io_w_mb((uint32_t)hfi_mem->qtbl.iova, icp_base + HFI_REG_QTBL_PTR);
	cam_io_w_mb((uint32_t)hfi_mem->sfr_buf.iova,
		icp_base + HFI_REG_SFR_PTR);
	cam_io_w_mb((uint32_t)hfi_mem->shmem.iova,
		icp_base + HFI_REG_SHARED_MEM_PTR);
	cam_io_w_mb((uint32_t)hfi_mem->shmem.len,
		icp_base + HFI_REG_SHARED_MEM_SIZE);
	cam_io_w_mb((uint32_t)hfi_mem->sec_heap.iova,
		icp_base + HFI_REG_SECONDARY_HEAP_PTR);
	cam_io_w_mb((uint32_t)hfi_mem->sec_heap.len,
		icp_base + HFI_REG_SECONDARY_HEAP_SIZE);
	cam_io_w_mb((uint32_t)hfi_mem->qdss.iova,
		icp_base + HFI_REG_QDSS_IOVA);
	cam_io_w_mb((uint32_t)hfi_mem->qdss.len,
		icp_base + HFI_REG_QDSS_IOVA_SIZE);
	cam_io_w_mb((uint32_t)hfi_mem->io_mem.iova,
		icp_base + HFI_REG_IO_REGION_IOVA);
	cam_io_w_mb((uint32_t)hfi_mem->io_mem.len,
		icp_base + HFI_REG_IO_REGION_SIZE);

	cam_io_w_mb((uint32_t)hfi_mem->io_mem2.iova,
		icp_base + HFI_REG_IO2_REGION_IOVA);
	cam_io_w_mb((uint32_t)hfi_mem->io_mem2.len,
		icp_base + HFI_REG_IO2_REGION_SIZE);

	cam_io_w_mb((uint32_t)hfi_mem->fw_uncached.iova,
		icp_base + HFI_REG_FWUNCACHED_REGION_IOVA);
	cam_io_w_mb((uint32_t)hfi_mem->fw_uncached.len,
		icp_base + HFI_REG_FWUNCACHED_REGION_SIZE);

	CAM_DBG(CAM_HFI, "IO1 : [0x%x 0x%x] IO2 [0x%x 0x%x]",
		hfi_mem->io_mem.iova, hfi_mem->io_mem.len,
		hfi_mem->io_mem2.iova, hfi_mem->io_mem2.len);

	CAM_DBG(CAM_HFI, "FwUncached : [0x%x 0x%x] Shared [0x%x 0x%x]",
		hfi_mem->fw_uncached.iova, hfi_mem->fw_uncached.len,
		hfi_mem->shmem.iova, hfi_mem->shmem.len);

	CAM_DBG(CAM_HFI, "SecHeap : [0x%x 0x%x] QDSS [0x%x 0x%x]",
		hfi_mem->sec_heap.iova, hfi_mem->sec_heap.len,
		hfi_mem->qdss.iova, hfi_mem->qdss.len);

	CAM_DBG(CAM_HFI, "QTbl : [0x%x 0x%x] Sfr [0x%x 0x%x]",
		hfi_mem->qtbl.iova, hfi_mem->qtbl.len,
		hfi_mem->sfr_buf.iova, hfi_mem->sfr_buf.len);

	return rc;
}

int cam_hfi_init(struct hfi_mem_info *hfi_mem, const struct hfi_ops *hfi_ops,
		void *priv, uint8_t event_driven_mode)
{
	int rc = 0;
	uint32_t status = 0;
	struct hfi_qtbl *qtbl;
	struct hfi_qtbl_hdr *qtbl_hdr;
	struct hfi_q_hdr *cmd_q_hdr, *msg_q_hdr, *dbg_q_hdr;
	struct sfr_buf *sfr_buffer;
	void __iomem *icp_base;

	if (!hfi_mem || !hfi_ops || !priv) {
		CAM_ERR(CAM_HFI,
			"invalid arg: hfi_mem=%pK hfi_ops=%pK priv=%pK",
			hfi_mem, hfi_ops, priv);
		return -EINVAL;
	}

	mutex_lock(&hfi_cmd_q_mutex);
	mutex_lock(&hfi_msg_q_mutex);

	if (!g_hfi) {
		g_hfi = kzalloc(sizeof(struct hfi_info), GFP_KERNEL);
		if (!g_hfi) {
			rc = -ENOMEM;
			goto alloc_fail;
		}
	}

	if (g_hfi->hfi_state != HFI_DEINIT) {
		CAM_ERR(CAM_HFI, "hfi_init: invalid state");
		rc = -EINVAL;
		goto regions_fail;
	}

	memcpy(&g_hfi->map, hfi_mem, sizeof(g_hfi->map));
	g_hfi->hfi_state = HFI_DEINIT;

	qtbl = (struct hfi_qtbl *)hfi_mem->qtbl.kva;
	qtbl_hdr = &qtbl->q_tbl_hdr;
	qtbl_hdr->qtbl_version = 0xFFFFFFFF;
	qtbl_hdr->qtbl_size = sizeof(struct hfi_qtbl);
	qtbl_hdr->qtbl_qhdr0_offset = sizeof(struct hfi_qtbl_hdr);
	qtbl_hdr->qtbl_qhdr_size = sizeof(struct hfi_q_hdr);
	qtbl_hdr->qtbl_num_q = ICP_HFI_NUMBER_OF_QS;
	qtbl_hdr->qtbl_num_active_q = ICP_HFI_NUMBER_OF_QS;

	/* setup host-to-firmware command queue */
	cmd_q_hdr = &qtbl->q_hdr[Q_CMD];
	cmd_q_hdr->qhdr_status = QHDR_ACTIVE;
	cmd_q_hdr->qhdr_start_addr = hfi_mem->cmd_q.iova;
	cmd_q_hdr->qhdr_q_size =  ICP_CMD_Q_SIZE_IN_BYTES >> BYTE_WORD_SHIFT;
	cmd_q_hdr->qhdr_pkt_size = ICP_HFI_VAR_SIZE_PKT;
	cmd_q_hdr->qhdr_pkt_drop_cnt = RESET;
	cmd_q_hdr->qhdr_read_idx = RESET;
	cmd_q_hdr->qhdr_write_idx = RESET;

	/* setup firmware-to-Host message queue */
	msg_q_hdr = &qtbl->q_hdr[Q_MSG];
	msg_q_hdr->qhdr_status = QHDR_ACTIVE;
	msg_q_hdr->qhdr_start_addr = hfi_mem->msg_q.iova;
	msg_q_hdr->qhdr_q_size = ICP_MSG_Q_SIZE_IN_BYTES >> BYTE_WORD_SHIFT;
	msg_q_hdr->qhdr_pkt_size = ICP_HFI_VAR_SIZE_PKT;
	msg_q_hdr->qhdr_pkt_drop_cnt = RESET;
	msg_q_hdr->qhdr_read_idx = RESET;
	msg_q_hdr->qhdr_write_idx = RESET;

	/* setup firmware-to-Host message queue */
	dbg_q_hdr = &qtbl->q_hdr[Q_DBG];
	dbg_q_hdr->qhdr_status = QHDR_ACTIVE;
	dbg_q_hdr->qhdr_start_addr = hfi_mem->dbg_q.iova;
	dbg_q_hdr->qhdr_q_size = ICP_DBG_Q_SIZE_IN_BYTES >> BYTE_WORD_SHIFT;
	dbg_q_hdr->qhdr_pkt_size = ICP_HFI_VAR_SIZE_PKT;
	dbg_q_hdr->qhdr_pkt_drop_cnt = RESET;
	dbg_q_hdr->qhdr_read_idx = RESET;
	dbg_q_hdr->qhdr_write_idx = RESET;

	sfr_buffer = (struct sfr_buf *)hfi_mem->sfr_buf.kva;
	sfr_buffer->size = ICP_MSG_SFR_SIZE_IN_BYTES;

	switch (event_driven_mode) {
	case INTR_MODE:
		cmd_q_hdr->qhdr_type = Q_CMD;
		cmd_q_hdr->qhdr_rx_wm = SET;
		cmd_q_hdr->qhdr_tx_wm = SET;
		cmd_q_hdr->qhdr_rx_req = SET;
		cmd_q_hdr->qhdr_tx_req = RESET;
		cmd_q_hdr->qhdr_rx_irq_status = RESET;
		cmd_q_hdr->qhdr_tx_irq_status = RESET;

		msg_q_hdr->qhdr_type = Q_MSG;
		msg_q_hdr->qhdr_rx_wm = SET;
		msg_q_hdr->qhdr_tx_wm = SET;
		msg_q_hdr->qhdr_rx_req = SET;
		msg_q_hdr->qhdr_tx_req = RESET;
		msg_q_hdr->qhdr_rx_irq_status = RESET;
		msg_q_hdr->qhdr_tx_irq_status = RESET;

		dbg_q_hdr->qhdr_type = Q_DBG;
		dbg_q_hdr->qhdr_rx_wm = SET;
		dbg_q_hdr->qhdr_tx_wm = SET_WM;
		dbg_q_hdr->qhdr_rx_req = RESET;
		dbg_q_hdr->qhdr_tx_req = RESET;
		dbg_q_hdr->qhdr_rx_irq_status = RESET;
		dbg_q_hdr->qhdr_tx_irq_status = RESET;

		break;

	case POLL_MODE:
		cmd_q_hdr->qhdr_type = Q_CMD | TX_EVENT_POLL_MODE_2 |
			RX_EVENT_POLL_MODE_2;
		msg_q_hdr->qhdr_type = Q_MSG | TX_EVENT_POLL_MODE_2 |
			RX_EVENT_POLL_MODE_2;
		dbg_q_hdr->qhdr_type = Q_DBG | TX_EVENT_POLL_MODE_2 |
			RX_EVENT_POLL_MODE_2;
		break;

	case WM_MODE:
		cmd_q_hdr->qhdr_type = Q_CMD | TX_EVENT_DRIVEN_MODE_2 |
			RX_EVENT_DRIVEN_MODE_2;
		cmd_q_hdr->qhdr_rx_wm = SET;
		cmd_q_hdr->qhdr_tx_wm = SET;
		cmd_q_hdr->qhdr_rx_req = RESET;
		cmd_q_hdr->qhdr_tx_req = SET;
		cmd_q_hdr->qhdr_rx_irq_status = RESET;
		cmd_q_hdr->qhdr_tx_irq_status = RESET;

		msg_q_hdr->qhdr_type = Q_MSG | TX_EVENT_DRIVEN_MODE_2 |
			RX_EVENT_DRIVEN_MODE_2;
		msg_q_hdr->qhdr_rx_wm = SET;
		msg_q_hdr->qhdr_tx_wm = SET;
		msg_q_hdr->qhdr_rx_req = SET;
		msg_q_hdr->qhdr_tx_req = RESET;
		msg_q_hdr->qhdr_rx_irq_status = RESET;
		msg_q_hdr->qhdr_tx_irq_status = RESET;

		dbg_q_hdr->qhdr_type = Q_DBG | TX_EVENT_DRIVEN_MODE_2 |
			RX_EVENT_DRIVEN_MODE_2;
		dbg_q_hdr->qhdr_rx_wm = SET;
		dbg_q_hdr->qhdr_tx_wm = SET_WM;
		dbg_q_hdr->qhdr_rx_req = RESET;
		dbg_q_hdr->qhdr_tx_req = RESET;
		dbg_q_hdr->qhdr_rx_irq_status = RESET;
		dbg_q_hdr->qhdr_tx_irq_status = RESET;
		break;

	default:
		CAM_ERR(CAM_HFI, "Invalid event driven mode :%u",
			event_driven_mode);
		break;
	}

	g_hfi->ops = *hfi_ops;
	g_hfi->priv = priv;

	icp_base = hfi_iface_addr(g_hfi);
	if (!icp_base) {
		CAM_ERR(CAM_HFI, "invalid HFI interface address");
		rc = -EINVAL;
		goto regions_fail;
	}

	cam_io_w_mb((uint32_t)hfi_mem->qtbl.iova,
		icp_base + HFI_REG_QTBL_PTR);
	cam_io_w_mb((uint32_t)hfi_mem->sfr_buf.iova,
		icp_base + HFI_REG_SFR_PTR);
	cam_io_w_mb((uint32_t)hfi_mem->shmem.iova,
		icp_base + HFI_REG_SHARED_MEM_PTR);
	cam_io_w_mb((uint32_t)hfi_mem->shmem.len,
		icp_base + HFI_REG_SHARED_MEM_SIZE);
	cam_io_w_mb((uint32_t)hfi_mem->sec_heap.iova,
		icp_base + HFI_REG_SECONDARY_HEAP_PTR);
	cam_io_w_mb((uint32_t)hfi_mem->sec_heap.len,
		icp_base + HFI_REG_SECONDARY_HEAP_SIZE);
	cam_io_w_mb((uint32_t)hfi_mem->qdss.iova,
		icp_base + HFI_REG_QDSS_IOVA);
	cam_io_w_mb((uint32_t)hfi_mem->qdss.len,
		icp_base + HFI_REG_QDSS_IOVA_SIZE);
	cam_io_w_mb((uint32_t)hfi_mem->io_mem.iova,
		icp_base + HFI_REG_IO_REGION_IOVA);
	cam_io_w_mb((uint32_t)hfi_mem->io_mem.len,
		icp_base + HFI_REG_IO_REGION_SIZE);
	cam_io_w_mb((uint32_t)hfi_mem->io_mem2.iova,
		icp_base + HFI_REG_IO2_REGION_IOVA);
	cam_io_w_mb((uint32_t)hfi_mem->io_mem2.len,
		icp_base + HFI_REG_IO2_REGION_SIZE);
	cam_io_w_mb((uint32_t)hfi_mem->fw_uncached.iova,
		icp_base + HFI_REG_FWUNCACHED_REGION_IOVA);
	cam_io_w_mb((uint32_t)hfi_mem->fw_uncached.len,
		icp_base + HFI_REG_FWUNCACHED_REGION_SIZE);

	CAM_DBG(CAM_HFI, "IO1 : [0x%x 0x%x] IO2 [0x%x 0x%x]",
		hfi_mem->io_mem.iova, hfi_mem->io_mem.len,
		hfi_mem->io_mem2.iova, hfi_mem->io_mem2.len);

	CAM_DBG(CAM_HFI, "FwUncached : [0x%x 0x%x] Shared [0x%x 0x%x]",
		hfi_mem->fw_uncached.iova, hfi_mem->fw_uncached.len,
		hfi_mem->shmem.iova, hfi_mem->shmem.len);

	CAM_DBG(CAM_HFI, "SecHeap : [0x%x 0x%x] QDSS [0x%x 0x%x]",
		hfi_mem->sec_heap.iova, hfi_mem->sec_heap.len,
		hfi_mem->qdss.iova, hfi_mem->qdss.len);

	CAM_DBG(CAM_HFI, "QTbl : [0x%x 0x%x] Sfr [0x%x 0x%x]",
		hfi_mem->qtbl.iova, hfi_mem->qtbl.len,
		hfi_mem->sfr_buf.iova, hfi_mem->sfr_buf.len);

	if (cam_presil_mode_enabled())
		cam_hfi_presil_setup(hfi_mem);

	cam_io_w_mb((uint32_t)ICP_INIT_REQUEST_SET,
		icp_base + HFI_REG_HOST_ICP_INIT_REQUEST);

	if (cam_presil_mode_enabled())
		cam_hfi_presil_set_init_request();

	if (cam_common_read_poll_timeout(icp_base +
		    HFI_REG_ICP_HOST_INIT_RESPONSE,
		    HFI_POLL_DELAY_US, HFI_POLL_TIMEOUT_US,
		    (uint32_t)UINT_MAX, ICP_INIT_RESP_SUCCESS, &status)) {
		CAM_ERR(CAM_HFI, "response poll timed out: status=0x%08x",
			status);
		rc = -ETIMEDOUT;
		goto regions_fail;
	}

	CAM_DBG(CAM_HFI, "ICP fw version: 0x%x",
		cam_io_r(icp_base + HFI_REG_FW_VERSION));

	g_hfi->hfi_state = HFI_READY;
	g_hfi->cmd_q_state = true;
	g_hfi->msg_q_state = true;

	hfi_irq_enable(g_hfi);

	mutex_unlock(&hfi_cmd_q_mutex);
	mutex_unlock(&hfi_msg_q_mutex);

	return rc;
regions_fail:
	kfree(g_hfi);
	g_hfi = NULL;
alloc_fail:
	mutex_unlock(&hfi_cmd_q_mutex);
	mutex_unlock(&hfi_msg_q_mutex);
	return rc;
}

void cam_hfi_deinit(void)
{
	if (cam_presil_mode_enabled()) {
		CAM_DBG(CAM_HFI, "SYS_RESET Needed in presil for back to back hfi_init success");
		hfi_send_system_cmd(HFI_CMD_SYS_RESET, 0, 0);
	}

	mutex_lock(&hfi_cmd_q_mutex);
	mutex_lock(&hfi_msg_q_mutex);

	if (!g_hfi) {
		CAM_ERR(CAM_HFI, "hfi path not established yet");
		goto err;
	}

	g_hfi->cmd_q_state = false;
	g_hfi->msg_q_state = false;

	cam_free_clear((void *)g_hfi);
	g_hfi = NULL;

err:
	mutex_unlock(&hfi_cmd_q_mutex);
	mutex_unlock(&hfi_msg_q_mutex);
}


#ifdef CONFIG_CAM_PRESIL
static int cam_hfi_presil_setup(struct hfi_mem_info *hfi_mem)
{
	/**
	 * The pchost maintains its own set of queue structures and
	 * needs additional info to accomplish this. Use the set of
	 * dummy registers to pass along this info.
	 */
	/**
	 * IOVA region length for each queue is currently hardcoded in
	 * pchost (except for SFR). No need to send for now.
	 */
	cam_presil_send_event(CAM_PRESIL_EVENT_HFI_REG_CMD_Q_IOVA, hfi_mem->cmd_q.iova);
	cam_presil_send_event(CAM_PRESIL_EVENT_HFI_REG_MSG_Q_IOVA, hfi_mem->msg_q.iova);
	cam_presil_send_event(CAM_PRESIL_EVENT_HFI_REG_DBG_Q_IOVA, hfi_mem->dbg_q.iova);
	cam_presil_send_event(CAM_PRESIL_EVENT_HFI_REG_SFR_LEN, hfi_mem->sfr_buf.len);

	return 0;
}

static int cam_hfi_presil_set_init_request(void)
{
	CAM_DBG(CAM_PRESIL, "notifying pchost to start HFI init...");
	cam_presil_send_event(CAM_PRESIL_EVENT_HFI_REG_ICP_V1_HW_VERSION_TO_START_HFI_INIT, 0xFF);
	CAM_DBG(CAM_PRESIL, "got done with PCHOST HFI init...");

	return 0;
}

int hfi_write_cmd(void *cmd_ptr)
{
	int presil_rc = CAM_PRESIL_BLOCKED;
	int rc = 0;

	if (!cmd_ptr) {
		CAM_ERR(CAM_HFI, "command is null");
		return -EINVAL;
	}

	mutex_lock(&hfi_cmd_q_mutex);

	presil_rc = cam_presil_hfi_write_cmd(cmd_ptr, (*(uint32_t *)cmd_ptr),
		CAM_PRESIL_CLIENT_ID_CAMERA);

	if ((presil_rc != CAM_PRESIL_SUCCESS) && (presil_rc != CAM_PRESIL_BLOCKED)) {
		CAM_ERR(CAM_HFI, "failed presil rc %d", presil_rc);
		rc = -EINVAL;
	} else {
		CAM_DBG(CAM_HFI, "presil rc %d", presil_rc);
	}

	mutex_unlock(&hfi_cmd_q_mutex);
	return rc;
}

int hfi_read_message(uint32_t *pmsg, uint8_t q_id,
	uint32_t *words_read)
{
	int presil_rc = CAM_PRESIL_BLOCKED;
	int rc = 0;

	if (!pmsg) {
		CAM_ERR(CAM_HFI, "Invalid msg");
		return -EINVAL;
	}

	if (q_id > Q_DBG) {
		CAM_ERR(CAM_HFI, "Invalid q :%u", q_id);
		return -EINVAL;
	}
	mutex_lock(&hfi_msg_q_mutex);

	memset(pmsg, 0x0, sizeof(uint32_t) * 256 /* ICP_MSG_BUF_SIZE */);
	*words_read = 0;

	presil_rc = cam_presil_hfi_read_message(pmsg, q_id, words_read,
		CAM_PRESIL_CLIENT_ID_CAMERA);

	if ((presil_rc != CAM_PRESIL_SUCCESS) && (presil_rc != CAM_PRESIL_BLOCKED)) {
		CAM_ERR(CAM_HFI, "failed presil rc %d", presil_rc);
		rc = -EINVAL;
	} else {
		CAM_DBG(CAM_HFI, "presil rc %d", presil_rc);
	}

	mutex_unlock(&hfi_msg_q_mutex);
	return rc;
}
#else
/* when presil mode not enabled */
static int cam_hfi_presil_setup(struct hfi_mem_info *hfi_mem)
{
	return 0;
}

static int cam_hfi_presil_set_init_request(void)
{
	return 0;
}
#endif /* #ifdef CONFIG_CAM_PRESIL */
