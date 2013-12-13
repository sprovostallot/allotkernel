/*
 * Copyright (c) 2004, 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Sun Microsystems, Inc. All rights reserved.
 * Copyright (c) 2005, 2006, 2007 Cisco Systems.  All rights reserved.
 * Copyright (c) 2005, 2006, 2007, 2008 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2004 Voltaire, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef MLX4_H
#define MLX4_H

#include <linux/mutex.h>
#include <linux/radix-tree.h>
#include <linux/timer.h>
#include <linux/workqueue.h>

#include <linux/mlx4/device.h>
#include <linux/mlx4/driver.h>
#include <linux/mlx4/doorbell.h>
#include <linux/mlx4/cmd.h>
#include <rdma/ib_verbs.h>

#define DRV_NAME	"mlx4_core"
#define PFX		DRV_NAME ": "
#define DRV_VERSION	"1.0-mlnx_ofed1.5.3"
#define DRV_RELDATE	"November 3, 2011"

enum {
	MLX4_HCR_BASE		= 0x80680,
	MLX4_HCR_SRIOV_BASE	= 0x4080680, /* good for SRIOV FW only */
	MLX4_HCR_SIZE		= 0x0001c,
	MLX4_CLR_INT_SIZE	= 0x00008,
	MLX4_SLAVE_COMM_BASE	= 0x0,
	MLX4_COMM_PAGESIZE	= 0x1000
};

enum {
	MLX4_DEF_MGM_ENTRY_SIZE	= 0x200,
	MLX4_MIN_MGM_ENTRY_SIZE = 0x80,
	MLX4_MAX_MGM_ENTRY_SIZE	= 0x1000,
	MLX4_MAX_QP_PER_MGM	= 4 * (MLX4_MAX_MGM_ENTRY_SIZE / 16 - 2),
	MLX4_MTT_ENTRY_PER_SEG	= 8
};

enum {
	MLX4_NUM_PDS		= 1 << 15,
};

enum {
	MLX4_CMPT_TYPE_QP	= 0,
	MLX4_CMPT_TYPE_SRQ	= 1,
	MLX4_CMPT_TYPE_CQ	= 2,
	MLX4_CMPT_TYPE_EQ	= 3,
	MLX4_CMPT_NUM_TYPE
};

enum {
	MLX4_CMPT_SHIFT		= 24,
	MLX4_NUM_CMPTS		= MLX4_CMPT_NUM_TYPE << MLX4_CMPT_SHIFT
};

#define MLX4_COMM_TIME		10000
enum {
	MLX4_COMM_CMD_RESET,
	MLX4_COMM_CMD_VHCR0,
	MLX4_COMM_CMD_VHCR1,
	MLX4_COMM_CMD_VHCR2,
	MLX4_COMM_CMD_VHCR_EN,
	MLX4_COMM_CMD_VHCR_POST
};

enum mlx4_resource {
	RES_QP,
	RES_CQ,
	RES_SRQ,
	RES_MPT,
	RES_MTT,
	RES_MAC,
	RES_VLAN,
	RES_MCAST,
   /* update after adding new resource*/
    MLX4_NUM_OF_RESOURCE_TYPE = 8
};

enum mlx4_alloc_mode {
	ICM_RESERVE_AND_ALLOC,
	ICM_RESERVE,
	ICM_ALLOC,
	ICM_MAC_VLAN,
};

enum {
	MLX4_MFUNC_MAX		= 64,
	MLX4_MFUNC_EQ_NUM	= 4,
	MLX4_MFUNC_MAX_EQES     = 8,
	MLX4_MFUNC_EQE_MASK     = (MLX4_MFUNC_MAX_EQES - 1)
};

#ifdef CONFIG_MLX4_DEBUG
extern int mlx4_debug_level;
#else /* CONFIG_MLX4_DEBUG */
#define mlx4_debug_level	(0)
#endif /* CONFIG_MLX4_DEBUG */

#define mlx4_dbg(mdev, format, arg...)					\
	do {								\
		if (mlx4_debug_level)					\
			dev_printk(KERN_DEBUG, &mdev->pdev->dev, format, ## arg); \
	} while (0)

#define mlx4_err(mdev, format, arg...) \
	dev_err(&mdev->pdev->dev, format, ## arg)
#define mlx4_info(mdev, format, arg...) \
	dev_info(&mdev->pdev->dev, format, ## arg)
#define mlx4_warn(mdev, format, arg...) \
	dev_warn(&mdev->pdev->dev, format, ## arg)

extern int mlx4_blck_lb;
extern int mlx4_log_num_mgm_entry_size;

#define MLX4_MAX_NUM_PF		16
#define MLX4_MAX_NUM_VF		64
#define MLX4_MAX_NUM_SLAVES	(MLX4_MAX_NUM_PF + MLX4_MAX_NUM_VF)

#define MLX4_VF					(1 << 0)
#define MLX4_VDEVICE(vendor, device, flags)	\
	PCI_VDEVICE(vendor, device), (flags)

struct mlx4_bitmap {
	u32			last;
	u32			top;
	u32			max;
	u32                     reserved_top;
	u32			mask;
	u32			avail;
	spinlock_t		lock;
	unsigned long	       *table;
};

struct mlx4_buddy {
	unsigned long	      **bits;
	unsigned int	       *num_free;
	int			max_order;
	spinlock_t		lock;
};

struct mlx4_icm;

struct mlx4_icm_table {
	u64			virt;
	int			num_icm;
	u32			num_obj;
	int			obj_size;
	int			lowmem;
	int			coherent;
	struct mutex		mutex;
	struct mlx4_icm	      **icm;
};


struct mlx4_eqe {
	u8			reserved1;
	u8			type;
	u8			reserved2;
	u8			subtype;
	union {
		u32		raw[6];
		struct {
			__be32	cqn;
		} __attribute__((packed)) comp;
		struct {
			u16	reserved1;
			__be16	token;
			u32	reserved2;
			u8	reserved3[3];
			u8	status;
			__be64	out_param;
		} __attribute__((packed)) cmd;
		struct {
			__be32	qpn;
		} __attribute__((packed)) qp;
		struct {
			__be32	srqn;
		} __attribute__((packed)) srq;
		struct {
			__be32	cqn;
			u32	reserved1;
			u8	reserved2[3];
			u8	syndrome;
		} __attribute__((packed)) cq_err;
		struct {
			u32	reserved1[2];
			__be32	port;
		} __attribute__((packed)) port_change;
		struct {
			#define COMM_CHANNEL_BIT_ARRAY_SIZE	4
			u32 reserved;
			u32 bit_vec[COMM_CHANNEL_BIT_ARRAY_SIZE];
		} __attribute__((packed)) comm_channel_arm;
		struct {
			u8	reserved[2];
			u8 	vep_num;
			u8	port;
		} __attribute__((packed)) vep_config;
		struct {
			u8	port;
			u8	reserved[3];
			__be64	mac;
		} __attribute__((packed)) mac_update;
		struct {
			__be16	current_temperature;
			__be16	warning_threshold;
		} __attribute__((packed)) warming;
	}			event;
#define ALL_SLAVES 0xff
	u8			slave_id;
	u8			reserved3[2];
	u8			owner;
} __attribute__((packed));

struct mlx4_eq {
	struct mlx4_dev	       *dev;
	void __iomem	       *doorbell;
	int			eqn;
	u32			cons_index;
	u16			irq;
	u16			have_irq;
	int			nent;
	int			load;
	struct mlx4_buf_list   *page_list;
	struct mlx4_mtt		mtt;
};

struct mlx4_slave_eqe {
	u8 type;
	u8 port;
	u32 param;
};

struct mlx4_slave_event_eq_info {
	u32 eqn;
	bool  use_int;
	u16 token;
	u64 event_type;
};

struct mlx4_profile {
	int			num_qp;
	int			rdmarc_per_qp;
	int			num_srq;
	int			num_cq;
	int			num_mcg;
	int			num_mpt;
	int			num_mtt;
};

struct mlx4_fw {
	u64			clr_int_base;
	u64			catas_offset;
	u64			comm_base;
	struct mlx4_icm	       *fw_icm;
	struct mlx4_icm	       *aux_icm;
	u32			catas_size;
	u16			fw_pages;
	u8			clr_int_bar;
	u8			catas_bar;
	u8			comm_bar;
};

struct mlx4_comm {
	u32			slave_write;
	u32			slave_read;
};

#define VLAN_FLTR_SIZE	128

struct mlx4_vlan_fltr {
	__be32 entry[VLAN_FLTR_SIZE];
};

#define GID_SIZE        16

#define MGM_QPN_MASK       0x00FFFFFF
#define MGM_BLCK_LB_BIT    30

enum mlx4_resource_state {
	RES_INIT = 0,
	RES_RESERVED = 1,
	RES_ALLOCATED = 2,
	RES_ALLOCATED_AFTER_RESERVATION = 3,
/*When registered mac the master reserved that qp, but the allocation should be in the slave*/
	RES_ALLOCATED_WITH_MASTER_RESERVATION = 4
};

struct mlx_tracked_qp_mcg {
		u8 gid[GID_SIZE];
		enum mlx4_protocol prot;
		struct list_head list;
};

struct mlx_tracked_vln_fltr {
	int port;
	struct mlx4_vlan_fltr vlan_fltr;
};

struct mlx4_tracked_resource {
		int slave_id;
		int res_type;
		int resource_id;
		/* state indicates the allocation stage,
		   importance where there is reservation and after that allocation
		*/
		unsigned long state;
		union {
			struct list_head mcg_list ; /*for the QP res*/
			u8 port ; /*for the MAC res and VLAN*/
			int order;/*for the MTT object*/
		} specific_data;
		struct list_head list;
};

struct mlx4_resource_tracker {
	spinlock_t lock;
	/* tree for each resources */
	struct radix_tree_root res_tree[MLX4_NUM_OF_RESOURCE_TYPE];
	/* num_of_slave's lists, one per slave */
	struct list_head *res_list;
};

struct mlx4_mcast_entry {
	struct list_head list;
	u64 addr;
};

struct mlx4_promisc_qp {
	struct list_head list;
	u32 qpn;
};

struct mlx4_steer_index {
	struct list_head list;
	unsigned int index;
	struct list_head duplicates;
};

struct mlx4_vep_cfg {
	u64 mac;
	u8  link;
	u8  bw_value;
};

struct mlx4_slave_state {
	u8 comm_toggle;
	u8 last_cmd;
	u8 init_port_mask;
	u8 pf_num;
	u8 vep_num;
	u8 port_num;
	bool active;
	u8 function;
	dma_addr_t vhcr_dma;
	u16 mtu[MLX4_MAX_PORTS + 1];
	__be32 ib_cap_mask[MLX4_MAX_PORTS + 1];
	struct mlx4_slave_eqe eq[MLX4_MFUNC_MAX_EQES];
	struct list_head mcast_filters[MLX4_MAX_PORTS + 1];
	struct mlx4_vlan_fltr *vlan_filter[MLX4_MAX_PORTS + 1];
	struct mlx4_slave_event_eq_info event_eq;
	struct mlx4_vep_cfg vep_cfg;
	u16 eq_pi;
	u16 eq_ci;
	int sqp_start;
	spinlock_t lock;
};

#define SLAVE_EVENT_EQ_SIZE	128
struct mlx4_slave_event_eq {
	u32 eqn;
	u32 cons;
	u32 prod;
	struct mlx4_eqe event_eqe[SLAVE_EVENT_EQ_SIZE];
};


struct mlx4_mfunc_master_ctx {
	struct mlx4_slave_state *slave_state;
	int			init_port_ref[MLX4_MAX_PORTS + 1];
	u16			max_mtu[MLX4_MAX_PORTS + 1];
	int			disable_mcast_ref[MLX4_MAX_PORTS + 1];
	u8			num_veps[MLX4_MAX_PORTS + 1];
	struct mlx4_resource_tracker res_tracker;
	struct workqueue_struct *comm_wq;
	struct work_struct	comm_work;
	struct work_struct	slave_event_work;
	struct work_struct	vep_config_work;
	u16			vep_config_bitmap;
	spinlock_t		vep_config_lock;
	u32			comm_arm_bit_vector[4];
	struct mlx4_eqe		cmd_eqe;
	struct mlx4_slave_event_eq slave_eq;
};

struct mlx4_vhcr {
	u64	in_param;
	u64	out_param;
	u32	in_modifier;
	u32	timeout;
	u16	op;
	u16	token;
	u8	op_modifier;
	int	errno;
};

struct mlx4_mfunc {
	struct mlx4_comm __iomem       *comm;
	struct mlx4_vhcr	       *vhcr;
	dma_addr_t			vhcr_dma;

	struct mlx4_mfunc_master_ctx	master;
	u32				demux_sqp[MLX4_MFUNC_MAX];
};

struct mlx4_mgm {
	__be32			next_gid_index;
	__be32			members_count;
	u32			reserved[2];
	u8			gid[16];
	__be32			qp[MLX4_MAX_QP_PER_MGM];
};

struct mlx4_cmd {
	struct pci_pool	       *pool;
	void __iomem	       *hcr;
	struct mutex		hcr_mutex;
	struct semaphore	poll_sem;
	struct semaphore	event_sem;
	struct semaphore	slave_sem;
	int			max_cmds;
	spinlock_t		context_lock;
	int			free_head;
	struct mlx4_cmd_context *context;
	u16			token_mask;
	u8			use_events;
	u8			toggle;
	u8			comm_toggle;
};

struct mlx4_uar_table {
	struct mlx4_bitmap	bitmap;
};

struct mlx4_mr_table {
	struct mlx4_bitmap	mpt_bitmap;
	struct mlx4_buddy	mtt_buddy;
	u64			mtt_base;
	u64			mpt_base;
	struct mlx4_icm_table	mtt_table;
	struct mlx4_icm_table	dmpt_table;
};

struct mlx4_cq_table {
	struct mlx4_bitmap	bitmap;
	spinlock_t		lock;
	struct radix_tree_root	tree;
	struct mlx4_icm_table	table;
	struct mlx4_icm_table	cmpt_table;
};

struct mlx4_eq_table {
	struct mlx4_bitmap	bitmap;
	char		       *irq_names;
	void __iomem	       *clr_int;
	void __iomem	      **uar_map;
	u32			clr_mask;
	struct mlx4_eq	       *eq;
	struct mlx4_icm_table	table;
	struct mlx4_icm_table	cmpt_table;
	int			have_irq;
	u8			inta_pin;
};

struct mlx4_srq_table {
	struct mlx4_bitmap	bitmap;
	spinlock_t		lock;
	struct mlx4_icm_table	table;
	struct mlx4_icm_table	cmpt_table;
};

struct mlx4_qp_table {
	struct mlx4_bitmap	bitmap;
	u32			rdmarc_base;
	int			rdmarc_shift;
	spinlock_t		lock;
	struct mlx4_icm_table	qp_table;
	struct mlx4_icm_table	auxc_table;
	struct mlx4_icm_table	altc_table;
	struct mlx4_icm_table	rdmarc_table;
	struct mlx4_icm_table	cmpt_table;
};

struct mlx4_mcg_table {
	struct mutex		mutex;
	struct mlx4_bitmap	bitmap;
	struct mlx4_icm_table	table;
};

struct mlx4_catas_err {
	u32 __iomem	       *map;
	struct timer_list	timer;
	struct list_head	list;
};

#define MLX4_MAX_MAC_NUM	128
#define MLX4_MAC_TABLE_SIZE	(MLX4_MAX_MAC_NUM << 3)

struct mlx4_mac_table {
	__be64			entries[MLX4_MAX_MAC_NUM];
	int			refs[MLX4_MAX_MAC_NUM];
	struct mutex		mutex;
	int			total;
	int			max;
};

#define MLX4_MAX_VLAN_NUM	128
#define MLX4_VLAN_TABLE_SIZE	(MLX4_MAX_VLAN_NUM << 2)

struct mlx4_vlan_table {
	__be32			entries[MLX4_MAX_VLAN_NUM];
	int			refs[MLX4_MAX_VLAN_NUM];
	struct mutex		mutex;
	int			total;
	int			max;
};


#define SET_PORT_GEN_ALL_VALID		0x7
#define SET_PORT_PROMISC_SHIFT		31
#define SET_PORT_MC_PROMISC_SHIFT	30

enum {
	MCAST_DIRECT_ONLY	= 0,
	MCAST_DIRECT		= 1,
	MCAST_DEFAULT		= 2
};

struct mlx4_set_port_general_context {
	u8 reserved[3];
	u8 flags;
	u16 reserved2;
	__be16 mtu;
	u8 pptx;
	u8 pfctx;
	u16 reserved3;
	u8 pprx;
	u8 pfcrx;
	u16 reserved4;
	u8 qinq;
	u8 reserved5[3];
};

struct mlx4_set_port_rqp_calc_context {
	__be32 base_qpn;
	u8 rererved;
	u8 n_mac;
	u8 n_vlan;
	u8 n_prio;
	u8 reserved2[3];
	u8 mac_miss;
	u8 intra_no_vlan;
	u8 no_vlan;
	u8 intra_vlan_miss;
	u8 vlan_miss;
	u8 reserved3[3];
	u8 no_vlan_prio;
	__be32 promisc;
	__be32 mcast;
};

struct mlx4_mac_entry {
	u64 mac;
};

struct mlx4_port_info {
	struct mlx4_dev	       *dev;
	int			port;
	char			dev_name[16];
	struct device_attribute port_attr;
	enum mlx4_port_type	tmp_type;
	struct mlx4_mac_table	mac_table;
	struct radix_tree_root	mac_tree;
	struct mlx4_vlan_table	vlan_table;
	int			base_qpn;
};

struct mlx4_sense {
	struct mlx4_dev		*dev;
	u8			do_sense_port[MLX4_MAX_PORTS + 1];
	u8			sense_allowed[MLX4_MAX_PORTS + 1];
	struct delayed_work	sense_poll;
	struct workqueue_struct	*sense_wq;
	u32			resched;
};

extern struct mutex drv_mutex;

struct mlx4_steer {
	struct list_head promisc_qps[MLX4_NUM_STEERS];
	struct list_head steer_entries[MLX4_NUM_STEERS];
	struct list_head high_prios;
};

struct mlx4_msix_ctl {
	u64		pool_bm;
	struct mutex	pool_lock;
};

struct mlx4_priv {
	struct mlx4_dev		dev;

	struct list_head	dev_list;
	struct list_head	ctx_list;
	spinlock_t		ctx_lock;

	struct list_head        pgdir_list;
	struct mutex            pgdir_mutex;

	struct mlx4_fw		fw;
	struct mlx4_cmd		cmd;
	struct mlx4_mfunc	mfunc;

	struct mlx4_bitmap	pd_bitmap;
	struct mlx4_bitmap	xrcd_bitmap;
	struct mlx4_uar_table	uar_table;
	struct mlx4_mr_table	mr_table;
	struct mlx4_cq_table	cq_table;
	struct mlx4_eq_table	eq_table;
	struct mlx4_srq_table	srq_table;
	struct mlx4_qp_table	qp_table;
	struct mlx4_mcg_table	mcg_table;
	struct mlx4_bitmap	counters_bitmap;
	struct list_head	bf_list;
	struct mutex		bf_mutex;

	struct mlx4_catas_err	catas_err;

	void __iomem	       *clr_base;

	struct mlx4_uar		driver_uar;
	void __iomem	       *kar;
	struct mlx4_port_info	port[MLX4_MAX_PORTS + 1];
	struct device_attribute trigger_attr;
	int                     trig;
	int                     changed_ports;
	struct mlx4_sense       sense;
	struct mutex		port_mutex;
	int			iboe_counter_index[MLX4_MAX_PORTS];
	struct io_mapping      *bf_mapping;
 	struct mlx4_steer	*steer;
 	bool			link_up[MLX4_MAX_PORTS + 1];
	bool			vep_mode[MLX4_MAX_PORTS + 1];
	struct mutex		port_ops_mutex;
	int                     init_port_ref[MLX4_MAX_PORTS + 1];
	atomic_t		opreq_count;
	struct workqueue_struct *opreq_queue;
	struct work_struct 	opreq_task;
	struct mlx4_msix_ctl    msix_ctl;
};

static inline struct mlx4_priv *mlx4_priv(struct mlx4_dev *dev)
{
	return container_of(dev, struct mlx4_priv, dev);
}

#define MLX4_SENSE_RANGE	(HZ * 3)

extern struct workqueue_struct *mlx4_wq;

u32 mlx4_bitmap_alloc(struct mlx4_bitmap *bitmap);
void mlx4_bitmap_free(struct mlx4_bitmap *bitmap, u32 obj);
u32 mlx4_bitmap_alloc_range(struct mlx4_bitmap *bitmap, int cnt, int align,
			    u32 skip_mask);
void mlx4_bitmap_free_range(struct mlx4_bitmap *bitmap, u32 obj, int cnt);
u32 mlx4_bitmap_avail(struct mlx4_bitmap *bitmap);
int mlx4_bitmap_init(struct mlx4_bitmap *bitmap, u32 num, u32 mask,
		     u32 reserved_bot, u32 resetrved_top);
int mlx4_bitmap_init_no_mask(struct mlx4_bitmap *bitmap, u32 num,
			     u32 reserved_bot, u32 reserved_top);
void mlx4_bitmap_cleanup(struct mlx4_bitmap *bitmap);

int mlx4_reset(struct mlx4_dev *dev);
int mlx4_get_ownership(struct mlx4_dev *dev);
void mlx4_free_ownership(struct mlx4_dev *dev);

int mlx4_alloc_eq_table(struct mlx4_dev *dev);
void mlx4_free_eq_table(struct mlx4_dev *dev);
int mlx4_GET_EVENT_wrapper(struct mlx4_dev *dev, int slave, struct mlx4_vhcr *vhcr,
						 struct mlx4_cmd_mailbox *inbox,
						 struct mlx4_cmd_mailbox *outbox);

int mlx4_init_pd_table(struct mlx4_dev *dev);
int mlx4_init_xrcd_table(struct mlx4_dev *dev);
int mlx4_init_uar_table(struct mlx4_dev *dev);
int mlx4_init_mr_table(struct mlx4_dev *dev);
int mlx4_init_eq_table(struct mlx4_dev *dev);
int mlx4_init_cq_table(struct mlx4_dev *dev);
int mlx4_init_qp_table(struct mlx4_dev *dev);
int mlx4_init_srq_table(struct mlx4_dev *dev);
int mlx4_init_mcg_table(struct mlx4_dev *dev);

void mlx4_cleanup_pd_table(struct mlx4_dev *dev);
void mlx4_cleanup_uar_table(struct mlx4_dev *dev);
void mlx4_cleanup_mr_table(struct mlx4_dev *dev);
void mlx4_cleanup_eq_table(struct mlx4_dev *dev);
void mlx4_cleanup_cq_table(struct mlx4_dev *dev);
void mlx4_cleanup_qp_table(struct mlx4_dev *dev);
void mlx4_cleanup_srq_table(struct mlx4_dev *dev);
void mlx4_cleanup_mcg_table(struct mlx4_dev *dev);
void mlx4_cleanup_xrcd_table(struct mlx4_dev *dev);

int mlx4_qp_alloc_icm(struct mlx4_dev *dev, int qpn);
void mlx4_qp_free_icm(struct mlx4_dev *dev, int qpn);
int mlx4_cq_alloc_icm(struct mlx4_dev *dev, int *cqn);
void mlx4_cq_free_icm(struct mlx4_dev *dev, int cqn);
int mlx4_srq_alloc_icm(struct mlx4_dev *dev, int *srqn);
void mlx4_srq_free_icm(struct mlx4_dev *dev, int srqn);
int mlx4_mr_reserve(struct mlx4_dev *dev);
void mlx4_mr_release(struct mlx4_dev *dev, u32 index);
int mlx4_mr_alloc_icm(struct mlx4_dev *dev, u32 index);
void mlx4_mr_free_icm(struct mlx4_dev *dev, u32 index);
u32 mlx4_alloc_mtt_range(struct mlx4_dev *dev, int order);
void mlx4_free_mtt_range(struct mlx4_dev *dev, u32 first_seg, int order);
int mlx4_WRITE_MTT_wrapper(struct mlx4_dev *dev, int slave, struct mlx4_vhcr *vhcr,
						 struct mlx4_cmd_mailbox *inbox,
						 struct mlx4_cmd_mailbox *outbox);

void mlx4_start_catas_poll(struct mlx4_dev *dev);
void mlx4_stop_catas_poll(struct mlx4_dev *dev);
void mlx4_catas_init(void);
int mlx4_restart_one(struct pci_dev *pdev);
int mlx4_register_device(struct mlx4_dev *dev);
void mlx4_unregister_device(struct mlx4_dev *dev);
void mlx4_dispatch_event(struct mlx4_dev *dev, enum mlx4_dev_event type, int port);
u16 mlx4_set_interface_mtu_get_max(struct mlx4_interface *intf,
		struct mlx4_dev *dev, int port, u16 new_mtu);
struct mlx4_dev_cap;
struct mlx4_init_hca_param;

u64 mlx4_make_profile(struct mlx4_dev *dev,
		      struct mlx4_profile *request,
		      struct mlx4_dev_cap *dev_cap,
		      struct mlx4_init_hca_param *init_hca);
void mlx4_master_comm_channel(struct work_struct *work);
void mlx4_gen_slave_eqe(struct work_struct *work);
void mlx4_update_vep_config(struct work_struct *work);

int mlx4_MAP_EQ_wrapper(struct mlx4_dev *dev, int slave,
			struct mlx4_vhcr *vhcr, struct mlx4_cmd_mailbox *inbox,
			struct mlx4_cmd_mailbox *outbox);
int mlx4_COMM_INT_wrapper(struct mlx4_dev *dev, int slave, struct mlx4_vhcr *vhcr,
			  struct mlx4_cmd_mailbox *inbox,
			  struct mlx4_cmd_mailbox *outbox);
int mlx4_RTR2RTS_QP_wrapper(struct mlx4_dev *dev, int slave,
			    struct mlx4_vhcr *vhcr,
			    struct mlx4_cmd_mailbox *inbox,
			    struct mlx4_cmd_mailbox *outbox);
int mlx4_GEN_EQE(struct mlx4_dev *dev, int slave, struct mlx4_eqe *eqe);

int mlx4_cmd_init(struct mlx4_dev *dev);
void mlx4_cmd_cleanup(struct mlx4_dev *dev);
int mlx4_multi_func_init(struct mlx4_dev *dev);
void mlx4_multi_func_cleanup(struct mlx4_dev *dev);
void mlx4_cmd_event(struct mlx4_dev *dev, u16 token, u8 status, u64 out_param);
int mlx4_cmd_use_events(struct mlx4_dev *dev);
void mlx4_cmd_use_polling(struct mlx4_dev *dev);

int mlx4_comm_cmd(struct mlx4_dev *dev, u8 cmd, u16 param, unsigned long timeout);


void mlx4_cq_completion(struct mlx4_dev *dev, u32 cqn);
void mlx4_cq_event(struct mlx4_dev *dev, u32 cqn, int event_type);

void mlx4_qp_event(struct mlx4_dev *dev, u32 qpn, int event_type);

void mlx4_srq_event(struct mlx4_dev *dev, u32 srqn, int event_type);

void mlx4_handle_catas_err(struct mlx4_dev *dev);

int mlx4_SENSE_PORT(struct mlx4_dev *dev, int port,
			   enum mlx4_port_type *type);
void mlx4_do_sense_ports(struct mlx4_dev *dev,
			 enum mlx4_port_type *stype,
			 enum mlx4_port_type *defaults);
void mlx4_start_sense(struct mlx4_dev *dev);
void mlx4_stop_sense(struct mlx4_dev *dev);
int mlx4_sense_init(struct mlx4_dev *dev);
void mlx4_sense_cleanup(struct mlx4_dev *dev);
int mlx4_check_port_params(struct mlx4_dev *dev,
			   enum mlx4_port_type *port_type);
int mlx4_change_port_types(struct mlx4_dev *dev,
			   enum mlx4_port_type *port_types);
void mlx4_set_port_mask(struct mlx4_dev *dev, struct mlx4_caps *caps, int function);

void mlx4_init_mac_table(struct mlx4_dev *dev, struct mlx4_mac_table *table);
void mlx4_init_vlan_table(struct mlx4_dev *dev, struct mlx4_vlan_table *table);

/* resource tracker functions*/
int mlx4_init_resource_tracker(struct mlx4_dev *dev);

void mlx4_free_resource_tracker(struct mlx4_dev *dev);

int mlx4_get_slave_from_resource_id(struct mlx4_dev *dev, enum mlx4_resource resource_type,
				    int resource_id, int *slave);

/* the parameter "state" indicates the current status (like in qp/mtt)
	need to reserve the renge before the allocation*/
int mlx4_add_resource_for_slave(struct mlx4_dev *dev, enum mlx4_resource resource_type,
				int slave_id, int resource_id, unsigned long state);
/*The MPT index need to have a mask*/
int mlx4_add_mpt_resource_for_slave(struct mlx4_dev *dev,
				    enum mlx4_resource resource_type, int slave_id,
				    int resource_id, unsigned long state);

/* use this fuction when there is call for resrvation of qp/mtt */
int mlx4_add_range_resource_for_slave(struct mlx4_dev *dev, enum mlx4_resource resource_type,
				      int slave_id, int from, int cnt);

void mlx4_delete_resource_for_slave(struct mlx4_dev *dev, enum mlx4_resource resource_type,
					int slave_id, int resource_id);

void mlx4_delete_range_resource_for_slave(struct mlx4_dev *dev, enum mlx4_resource resource_type,
					int slave_id, int from, int cnt);

void mlx4_delete_all_resources_for_slave(struct mlx4_dev *dev, int slave_id);

int mlx4_add_mcg_to_tracked_qp(struct mlx4_dev *dev, int qpn, u8* gid, enum mlx4_protocol prot) ;
int mlx4_remove_mcg_from_tracked_qp(struct mlx4_dev *dev, int qpn, u8* gid);

int mlx4_add_port_to_tracked_mac(struct mlx4_dev *dev, int qpn, u8 port) ;

int mlx4_add_vlan_fltr_to_tracked_slave(struct mlx4_dev *dev, int slave_id, int port);

void mlx4_delete_specific_res_type_for_slave(struct mlx4_dev *dev, int slave_id,
					     enum mlx4_resource resource_type);

int mlx4_add_mtt_resource_for_slave(struct mlx4_dev *dev,
				    int slave_id, int resource_id,
				    unsigned long state, int order);
/*Resource tracker - verification functions.*/

int mlx4_verify_resource_wrapper(struct mlx4_dev *dev, int slave, struct mlx4_vhcr *vhcr,
						  struct mlx4_cmd_mailbox *inbox);

int mlx4_verify_mpt_index(struct mlx4_dev *dev, int slave, struct mlx4_vhcr *vhcr,
						  struct mlx4_cmd_mailbox *inbox);

int mlx4_verify_cq_index(struct mlx4_dev *dev, int slave, struct mlx4_vhcr *vhcr,
						  struct mlx4_cmd_mailbox *inbox);

int mlx4_verify_srq_index(struct mlx4_dev *dev, int slave, struct mlx4_vhcr *vhcr,
						  struct mlx4_cmd_mailbox *inbox);

int mlx4_verify_qp_index(struct mlx4_dev *dev, int slave, struct mlx4_vhcr *vhcr,
						  struct mlx4_cmd_mailbox *inbox);

int mlx4_verify_srq_aram(struct mlx4_dev *dev, int slave, struct mlx4_vhcr *vhcr,
						  struct mlx4_cmd_mailbox *inbox) ;

/*Ruturns the mask according to specific bitmap allocator*/
u32 calculate_bitmap_mask(struct mlx4_dev *dev, enum mlx4_resource resource_type);

int mlx4_SET_PORT(struct mlx4_dev *dev, u8 port);
int mlx4_SET_PORT_wrapper(struct mlx4_dev *dev, int slave, struct mlx4_vhcr *vhcr,
							struct mlx4_cmd_mailbox *inbox,
							struct mlx4_cmd_mailbox *outbox);
int mlx4_INIT_PORT_wrapper(struct mlx4_dev *dev, int slave, struct mlx4_vhcr *vhcr,
							 struct mlx4_cmd_mailbox *inbox,
							 struct mlx4_cmd_mailbox *outbox);
int mlx4_CLOSE_PORT_wrapper(struct mlx4_dev *dev, int slave, struct mlx4_vhcr *vhcr,
							  struct mlx4_cmd_mailbox *inbox,
							  struct mlx4_cmd_mailbox *outbox);
int mlx4_QUERY_PORT_wrapper(struct mlx4_dev *dev, int slave, struct mlx4_vhcr *vhcr,
							  struct mlx4_cmd_mailbox *inbox,
							  struct mlx4_cmd_mailbox *outbox);
int mlx4_get_port_ib_caps(struct mlx4_dev *dev, u8 port, __be32 *caps);
int mlx4_get_ext_port_caps(struct mlx4_dev *dev, u8 port, u64 *get_ext_caps);

int mlx4_CONF_SPECIAL_QP_wrapper(struct mlx4_dev *dev, int slave, struct mlx4_vhcr *vhcr,
						       struct mlx4_cmd_mailbox *inbox,
						       struct mlx4_cmd_mailbox *outbox);
int mlx4_GET_SLAVE_SQP(struct mlx4_dev *dev, u32 *sqp, int num);
int mlx4_GET_SLAVE_SQP_wrapper(struct mlx4_dev *dev, int slave, struct mlx4_vhcr *vhcr,
							  struct mlx4_cmd_mailbox *inbox,
							  struct mlx4_cmd_mailbox *outbox);

int mlx4_MCAST_wrapper(struct mlx4_dev *dev, int slave, struct mlx4_vhcr *vhcr,
						     struct mlx4_cmd_mailbox *inbox,
						     struct mlx4_cmd_mailbox *outbox);
int mlx4_PROMISC_wrapper(struct mlx4_dev *dev, int slave,
			 struct mlx4_vhcr *vhcr,
			 struct mlx4_cmd_mailbox *inbox,
			 struct mlx4_cmd_mailbox *outbox);
int mlx4_SET_MCAST_FLTR_wrapper(struct mlx4_dev *dev, int slave, struct mlx4_vhcr *vhcr,
				struct mlx4_cmd_mailbox *inbox,
				struct mlx4_cmd_mailbox *outbox);
int mlx4_SET_VLAN_FLTR_wrapper(struct mlx4_dev *dev, int slave, struct mlx4_vhcr *vhcr,
				struct mlx4_cmd_mailbox *inbox,
				struct mlx4_cmd_mailbox *outbox);
int mlx4_common_set_vlan_fltr(struct mlx4_dev *dev, int function,
				     int port, void *buf);
int mlx4_common_dump_eth_stats(struct mlx4_dev *dev, int slave, u32 in_mod,
				struct mlx4_cmd_mailbox *outbox);
int mlx4_DUMP_ETH_STATS_wrapper(struct mlx4_dev *dev, int slave,
				   struct mlx4_vhcr *vhcr,
				   struct mlx4_cmd_mailbox *inbox,
				   struct mlx4_cmd_mailbox *outbox);
int MLX4_CMD_QUERY_IF_STAT_wrapper(struct mlx4_dev *dev, int slave,
				   struct mlx4_vhcr *vhcr,
				   struct mlx4_cmd_mailbox *inbox,
				   struct mlx4_cmd_mailbox *outbox);
int MLX4_CMD_SET_NODE_wrapper(struct mlx4_dev *dev, int slave,
				   struct mlx4_vhcr *vhcr,
				   struct mlx4_cmd_mailbox *inbox,
				   struct mlx4_cmd_mailbox *outbox);

int mlx4_get_mgm_entry_size(struct mlx4_dev *dev);
int mlx4_get_qp_per_mgm(struct mlx4_dev *dev);


#endif /* MLX4_H */
