/*
 * Copyright (c) 2004, 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005, 2006, 2007, 2008 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2005, 2006, 2007 Cisco Systems, Inc.  All rights reserved.
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

#include <linux/sched.h>
#include <linux/pci.h>
#include <linux/errno.h>

#include <linux/mlx4/cmd.h>

#include <asm/io.h>

#include "mlx4.h"
#include "fw.h"

#define CMD_POLL_TOKEN 0xffff
#define INBOX_MASK	0xffffffffffffff00ULL

enum {
	/* command completed successfully: */
	CMD_STAT_OK		= 0x00,
	/* Internal error (such as a bus error) occurred while processing command: */
	CMD_STAT_INTERNAL_ERR	= 0x01,
	/* Operation/command not supported or opcode modifier not supported: */
	CMD_STAT_BAD_OP		= 0x02,
	/* Parameter not supported or parameter out of range: */
	CMD_STAT_BAD_PARAM	= 0x03,
	/* System not enabled or bad system state: */
	CMD_STAT_BAD_SYS_STATE	= 0x04,
	/* Attempt to access reserved or unallocaterd resource: */
	CMD_STAT_BAD_RESOURCE	= 0x05,
	/* Requested resource is currently executing a command, or is otherwise busy: */
	CMD_STAT_RESOURCE_BUSY	= 0x06,
	/* Required capability exceeds device limits: */
	CMD_STAT_EXCEED_LIM	= 0x08,
	/* Resource is not in the appropriate state or ownership: */
	CMD_STAT_BAD_RES_STATE	= 0x09,
	/* Index out of range: */
	CMD_STAT_BAD_INDEX	= 0x0a,
	/* FW image corrupted: */
	CMD_STAT_BAD_NVMEM	= 0x0b,
	/* Error in ICM mapping (e.g. not enough auxiliary ICM pages to execute command): */
	CMD_STAT_ICM_ERROR	= 0x0c,
	/* Attempt to modify a QP/EE which is not in the presumed state: */
	CMD_STAT_BAD_QP_STATE   = 0x10,
	/* Bad segment parameters (Address/Size): */
	CMD_STAT_BAD_SEG_PARAM	= 0x20,
	/* Memory Region has Memory Windows bound to: */
	CMD_STAT_REG_BOUND	= 0x21,
	/* HCA local attached memory not present: */
	CMD_STAT_LAM_NOT_PRE	= 0x22,
	/* Bad management packet (silently discarded): */
	CMD_STAT_BAD_PKT	= 0x30,
	/* More outstanding CQEs in CQ than new CQ size: */
	CMD_STAT_BAD_SIZE	= 0x40,
	/* Multi Function device support required: */
	CMD_STAT_MULTI_FUNC_REQ	= 0x50,
};

enum {
	HCR_IN_PARAM_OFFSET	= 0x00,
	HCR_IN_MODIFIER_OFFSET	= 0x08,
	HCR_OUT_PARAM_OFFSET	= 0x0c,
	HCR_TOKEN_OFFSET	= 0x14,
	HCR_STATUS_OFFSET	= 0x18,

	HCR_OPMOD_SHIFT		= 12,
	HCR_T_BIT		= 21,
	HCR_E_BIT		= 22,
	HCR_GO_BIT		= 23
};

enum {
	GO_BIT_TIMEOUT_MSECS	= 10000
};

struct mlx4_cmd_context {
	struct completion	done;
	int			result;
	int			next;
	u64			out_param;
	u16			token;
	u8			fw_status;
};

static int mlx4_status_to_errno(u8 status)
{
	static const int trans_table[] = {
		[CMD_STAT_INTERNAL_ERR]	  = -EIO,
		[CMD_STAT_BAD_OP]	  = -EPERM,
		[CMD_STAT_BAD_PARAM]	  = -EINVAL,
		[CMD_STAT_BAD_SYS_STATE]  = -ENXIO,
		[CMD_STAT_BAD_RESOURCE]	  = -EBADF,
		[CMD_STAT_RESOURCE_BUSY]  = -EBUSY,
		[CMD_STAT_EXCEED_LIM]	  = -ENOMEM,
		[CMD_STAT_BAD_RES_STATE]  = -EBADF,
		[CMD_STAT_BAD_INDEX]	  = -EBADF,
		[CMD_STAT_BAD_NVMEM]	  = -EFAULT,
		[CMD_STAT_ICM_ERROR]	  = -ENFILE,
		[CMD_STAT_BAD_QP_STATE]   = -EINVAL,
		[CMD_STAT_BAD_SEG_PARAM]  = -EFAULT,
		[CMD_STAT_REG_BOUND]	  = -EBUSY,
		[CMD_STAT_LAM_NOT_PRE]	  = -EAGAIN,
		[CMD_STAT_BAD_PKT]	  = -EINVAL,
		[CMD_STAT_BAD_SIZE]	  = -ENOMEM,
		[CMD_STAT_MULTI_FUNC_REQ] = -EACCES,
	};

	if (status >= ARRAY_SIZE(trans_table) ||
	    (status != CMD_STAT_OK && trans_table[status] == 0))
		return -EIO;

	return trans_table[status];
}

static int comm_pending(struct mlx4_dev *dev)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	u32 status = readl(&priv->mfunc.comm->slave_read);

	return (swab32(status) >> 30) != priv->cmd.comm_toggle;
}

static void mlx4_comm_cmd_post(struct mlx4_dev *dev, u8 cmd, u16 param)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	u32 val;

	if (cmd == MLX4_COMM_CMD_RESET)
		priv->cmd.comm_toggle = 0;
	else if (++priv->cmd.comm_toggle > 2)
		priv->cmd.comm_toggle = 1;
	val = param | (cmd << 16) | (priv->cmd.comm_toggle << 30);
	__raw_writel((__force u32) cpu_to_be32(val), &priv->mfunc.comm->slave_write);
	wmb();
}

int mlx4_comm_cmd_poll(struct mlx4_dev *dev, u8 cmd, u16 param, unsigned long timeout)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	unsigned long end;
	int err = 0;

	/* First, verify that the master reports correct status */
	if (comm_pending(dev)) {
		mlx4_warn(dev, "Communication channel is not idle\n");
		return -EAGAIN;
	}

	/* Write command */
	down(&priv->cmd.poll_sem);
	mlx4_comm_cmd_post(dev, cmd, param);

	end = msecs_to_jiffies(timeout) + jiffies;
	while (comm_pending(dev) && time_before(jiffies, end))
		cond_resched();

	if (comm_pending(dev)) {
		mlx4_warn(dev, "Communication channel timed out\n");
		err = -ETIMEDOUT;
	}

	up(&priv->cmd.poll_sem);
	return 0;
}

static int mlx4_comm_cmd_wait(struct mlx4_dev *dev, u8 op,
			      u16 param, unsigned long timeout)
{
	struct mlx4_cmd *cmd = &mlx4_priv(dev)->cmd;
	struct mlx4_cmd_context *context;
	int err = 0;

	down(&cmd->event_sem);

	spin_lock(&cmd->context_lock);
	BUG_ON(cmd->free_head < 0);
	context = &cmd->context[cmd->free_head];
	context->token += cmd->token_mask + 1;
	cmd->free_head = context->next;
	spin_unlock(&cmd->context_lock);

	init_completion(&context->done);

	mlx4_comm_cmd_post(dev, op, param);

	if (!wait_for_completion_timeout(&context->done, msecs_to_jiffies(timeout))) {
		mlx4_warn(dev, "communication channel command 0x%x timed out\n", op);
		err = -EBUSY;
		goto out;
	}

	err = context->result;
	if (err && context->fw_status != CMD_STAT_MULTI_FUNC_REQ) {
		mlx4_err(dev, "command 0x%x failed: fw status = 0x%x\n",
			 op, context->fw_status);
		goto out;
	}

out:
	spin_lock(&cmd->context_lock);
	context->next = cmd->free_head;
	cmd->free_head = context - cmd->context;
	spin_unlock(&cmd->context_lock);

	up(&cmd->event_sem);
	return err;
}

int mlx4_comm_cmd(struct mlx4_dev *dev, u8 cmd, u16 param, unsigned long timeout)
{
	if (mlx4_priv(dev)->cmd.use_events)
		return mlx4_comm_cmd_wait(dev, cmd, param, timeout);
	return mlx4_comm_cmd_poll(dev, cmd, param, timeout);
}

static int cmd_pending(struct mlx4_dev *dev)
{
	u32 status = readl(mlx4_priv(dev)->cmd.hcr + HCR_STATUS_OFFSET);

	return (status & swab32(1 << HCR_GO_BIT)) ||
		(mlx4_priv(dev)->cmd.toggle ==
		 !!(status & swab32(1 << HCR_T_BIT)));
}

static int mlx4_cmd_post(struct mlx4_dev *dev, u64 in_param, u64 out_param,
			 u32 in_modifier, u8 op_modifier, u16 op, u16 token,
			 int event)
{
	struct mlx4_cmd *cmd = &mlx4_priv(dev)->cmd;
	u32 __iomem *hcr = cmd->hcr;
	int ret = -EAGAIN;
	unsigned long end;

	mutex_lock(&cmd->hcr_mutex);

	end = jiffies;
	if (event)
		end += msecs_to_jiffies(GO_BIT_TIMEOUT_MSECS);

	while (cmd_pending(dev)) {
		if (time_after_eq(jiffies, end)) {
			mlx4_warn(dev, "cannot post the command 0x%x (go bit not cleared)\n", op);
			goto out;
		}
		cond_resched();
	}

	/*
	 * We use writel (instead of something like memcpy_toio)
	 * because writes of less than 32 bits to the HCR don't work
	 * (and some architectures such as ia64 implement memcpy_toio
	 * in terms of writeb).
	 */
	__raw_writel((__force u32) cpu_to_be32(in_param >> 32),		  hcr + 0);
	__raw_writel((__force u32) cpu_to_be32(in_param & 0xfffffffful),  hcr + 1);
	__raw_writel((__force u32) cpu_to_be32(in_modifier),		  hcr + 2);
	__raw_writel((__force u32) cpu_to_be32(out_param >> 32),	  hcr + 3);
	__raw_writel((__force u32) cpu_to_be32(out_param & 0xfffffffful), hcr + 4);
	__raw_writel((__force u32) cpu_to_be32(token << 16),		  hcr + 5);

	/* __raw_writel may not order writes. */
	wmb();

	__raw_writel((__force u32) cpu_to_be32((1 << HCR_GO_BIT)		|
					       (cmd->toggle << HCR_T_BIT)	|
					       (event ? (1 << HCR_E_BIT) : 0)	|
					       (op_modifier << HCR_OPMOD_SHIFT) |
					       op),			  hcr + 6);

	/*
	 * Make sure that our HCR writes don't get mixed in with
	 * writes from another CPU starting a FW command.
	 */
	mmiowb();

	cmd->toggle = cmd->toggle ^ 1;

	ret = 0;

out:
	mutex_unlock(&cmd->hcr_mutex);
	return ret;
}

static int mlx4_slave_cmd(struct mlx4_dev *dev, u64 in_param, u64 *out_param,
			  int out_is_imm, u32 in_modifier, u8 op_modifier,
			  u16 op, unsigned long timeout)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_vhcr *vhcr = priv->mfunc.vhcr;
	int ret;

	down(&priv->cmd.slave_sem);
	vhcr->in_param = in_param;
	vhcr->out_param = out_param ? *out_param : 0;
	vhcr->in_modifier = in_modifier;
	vhcr->timeout = timeout;
	vhcr->op = op;
	vhcr->token = CMD_POLL_TOKEN;
	vhcr->op_modifier = op_modifier;
	vhcr->errno = 0;
	ret = mlx4_comm_cmd(dev, MLX4_COMM_CMD_VHCR_POST, 0, MLX4_COMM_TIME + timeout);
	if (!ret) {
		if (out_is_imm)
			*out_param = vhcr->out_param;
		ret = vhcr->errno;
	}
	up(&priv->cmd.slave_sem);
	return ret;
}

static int mlx4_cmd_poll(struct mlx4_dev *dev, u64 in_param, u64 *out_param,
			 int out_is_imm, u32 in_modifier, u8 op_modifier,
			 u16 op, unsigned long timeout)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	void __iomem *hcr = priv->cmd.hcr;
	int err = 0;
	unsigned long end;
	u32 stat;

	down(&priv->cmd.poll_sem);

	err = mlx4_cmd_post(dev, in_param, out_param ? *out_param : 0,
			    in_modifier, op_modifier, op, CMD_POLL_TOKEN, 0);
	if (err)
		goto out;

	end = msecs_to_jiffies(timeout) + jiffies;
	while (cmd_pending(dev) && time_before(jiffies, end))
		cond_resched();

	if (cmd_pending(dev)) {
		mlx4_warn(dev, "command 0x%x timed out (go bit not cleared)\n", op);
		err = -ETIMEDOUT;
		goto out;
	}

	if (out_is_imm)
		*out_param =
			(u64) be32_to_cpu((__force __be32)
					  __raw_readl(hcr + HCR_OUT_PARAM_OFFSET)) << 32 |
			(u64) be32_to_cpu((__force __be32)
					  __raw_readl(hcr + HCR_OUT_PARAM_OFFSET + 4));
	stat = be32_to_cpu((__force __be32) __raw_readl(hcr + HCR_STATUS_OFFSET)) >> 24;
	err = mlx4_status_to_errno(stat);
	if (err) {
		if ((op != MLX4_CMD_SET_NODE || stat != CMD_STAT_BAD_OP) &&
		    stat != CMD_STAT_MULTI_FUNC_REQ)
			mlx4_err(dev, "command 0x%x failed: fw status = 0x%x\n",
				 op, stat);
	}

out:
	up(&priv->cmd.poll_sem);
	return err;
}

void mlx4_cmd_event(struct mlx4_dev *dev, u16 token, u8 status, u64 out_param)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_cmd_context *context =
		&priv->cmd.context[token & priv->cmd.token_mask];

	/* previously timed out command completing at long last */
	if (token != context->token)
		return;

	context->fw_status = status;
	context->result    = mlx4_status_to_errno(status);
	context->out_param = out_param;

	complete(&context->done);
}

static int mlx4_cmd_wait(struct mlx4_dev *dev, u64 in_param, u64 *out_param,
			 int out_is_imm, u32 in_modifier, u8 op_modifier,
			 u16 op, unsigned long timeout)
{
	struct mlx4_cmd *cmd = &mlx4_priv(dev)->cmd;
	struct mlx4_cmd_context *context;
	int err = 0;

	down(&cmd->event_sem);

	spin_lock(&cmd->context_lock);
	BUG_ON(cmd->free_head < 0);
	context = &cmd->context[cmd->free_head];
	context->token += cmd->token_mask + 1;
	cmd->free_head = context->next;
	spin_unlock(&cmd->context_lock);

	init_completion(&context->done);

	mlx4_cmd_post(dev, in_param, out_param ? *out_param : 0,
		      in_modifier, op_modifier, op, context->token, 1);

	if (!wait_for_completion_timeout(&context->done, msecs_to_jiffies(timeout))) {
		mlx4_warn(dev, "command 0x%x timed out (go bit not cleared)\n", op);
		err = -EBUSY;
		goto out;
	}

	err = context->result;
	if (err) {
		if ((op != MLX4_CMD_SET_NODE || context->fw_status != CMD_STAT_BAD_OP) &&
		    context->fw_status != CMD_STAT_MULTI_FUNC_REQ)
			mlx4_err(dev, "command 0x%x failed: fw status = 0x%x\n",
				 op, context->fw_status);
		goto out;
	}

	if (out_is_imm && out_param)
		*out_param = context->out_param;

out:
	spin_lock(&cmd->context_lock);
	context->next = cmd->free_head;
	cmd->free_head = context - cmd->context;
	spin_unlock(&cmd->context_lock);

	up(&cmd->event_sem);
	return err;
}

int __mlx4_cmd(struct mlx4_dev *dev, u64 in_param, u64 *out_param,
	       int out_is_imm, u32 in_modifier, u8 op_modifier,
	       u16 op, unsigned long timeout)
{
	if (mlx4_is_slave(dev))
		return mlx4_slave_cmd(dev, in_param, out_param, out_is_imm,
				      in_modifier, op_modifier, op, timeout);

	if (mlx4_priv(dev)->cmd.use_events)
		return mlx4_cmd_wait(dev, in_param, out_param, out_is_imm,
				     in_modifier, op_modifier, op, timeout);
	else
		return mlx4_cmd_poll(dev, in_param, out_param, out_is_imm,
				     in_modifier, op_modifier, op, timeout);
}
EXPORT_SYMBOL_GPL(__mlx4_cmd);


static int mlx4_ARM_COMM_CHANNEL(struct mlx4_dev *dev)
{
	return mlx4_cmd(dev, 0, 0, 0, MLX4_CMD_ARM_COMM_CHANNEL, MLX4_CMD_TIME_CLASS_B);
}

int mlx4_GEN_EQE(struct mlx4_dev *dev, int slave, struct mlx4_eqe *eqe)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_slave_event_eq_info *event_eq =
		&priv->mfunc.master.slave_state[slave].event_eq;
	struct mlx4_cmd_mailbox *mailbox;
	u32 in_modifier = 0;
	int err;

	if (!event_eq->use_int)
		return 0;

	/* Create the event only if the slave is registered */
	if ((event_eq->event_type & eqe->type) == 0)
		return 0;

	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);

	if (eqe->type == MLX4_EVENT_TYPE_CMD) {
		++event_eq->token;
		eqe->event.cmd.token = cpu_to_be16(event_eq->token);
	}

	memcpy(mailbox->buf, (u8 *) eqe, 28);

	in_modifier = (slave & 0xff) | ((event_eq->eqn & 0xff) << 16);

	err = mlx4_cmd(dev, mailbox->dma, in_modifier, 0,
		       MLX4_CMD_GEN_EQE, MLX4_CMD_TIME_CLASS_B);

       mlx4_free_cmd_mailbox(dev, mailbox);
       return err;
}

static int mlx4_ACCESS_MEM(struct mlx4_dev *dev, u64 master_addr,
			   int slave, u64 slave_addr,
			   int size, int is_read)
{
	u64 in_param;
	u64 out_param;

	if ((slave_addr & 0xfff) | (master_addr & 0xfff) |
	    (slave & ~0x7f) | (size & 0xff)) {
		mlx4_err(dev, "Bad access mem params - slave_addr:0x%llx "
			      "master_addr:0x%llx slave_id:%d size:%d\n",
			      slave_addr, master_addr, slave, size);
		return -EINVAL;
	}

	if (is_read) {
		in_param = (u64) slave | slave_addr;
		out_param = (u64) dev->caps.function | master_addr;
	} else {
		in_param = (u64) dev->caps.function | master_addr;
		out_param = (u64) slave | slave_addr;
	}

	return mlx4_cmd_imm(dev, in_param, &out_param, size, 0,
					   MLX4_CMD_ACCESS_MEM,
					   MLX4_CMD_TIME_CLASS_A);
}

static int mlx4_RESOURCE_wrapper(struct mlx4_dev *dev, int slave, struct mlx4_vhcr *vhcr,
						       struct mlx4_cmd_mailbox *inbox,
						       struct mlx4_cmd_mailbox *outbox)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	u32 param1 = *((u32 *) &vhcr->in_param);
	u32 param2 = *(((u32 *) &vhcr->in_param) + 1);
	int ret;
	int rt_res;
	u8 vep_num = priv->mfunc.master.slave_state[slave].vep_num;
	u32 res_bit_mask = 0;
	int res_id ;

	vhcr->errno = 0;
	switch (vhcr->in_modifier) {
	case RES_QP:
		switch (vhcr->op_modifier) {
		case ICM_RESERVE:
			if (vhcr->op == MLX4_CMD_ALLOC_RES) {
				vhcr->errno = mlx4_qp_reserve_range(dev, param1, param2, &ret, 0);
				if (!vhcr->errno) {
					vhcr->out_param = ret;
					rt_res = mlx4_add_range_resource_for_slave(dev,
										    RES_QP,
										    slave,
										    ret,
										    param1);
					if (rt_res) {
						mlx4_info(dev, "mlx4_add_resource_for_slave"
							       " failed, ret: %d for slave: %d",
							   rt_res, slave);
						mlx4_qp_release_range(dev, ret, param1);
						return rt_res;
					}
				}
			} else {
				mlx4_qp_release_range(dev, param1, param2);
				mlx4_delete_range_resource_for_slave(dev,
								      RES_QP,
								      slave, param1, param2);
			}
			break;
		case ICM_ALLOC:
			if (vhcr->op == MLX4_CMD_ALLOC_RES) {
					vhcr->errno = mlx4_qp_alloc_icm(dev, param1);
				if (!vhcr->errno) {
					rt_res = mlx4_add_resource_for_slave(
						dev, RES_QP, slave, param1,
						 RES_ALLOCATED_AFTER_RESERVATION);
					if (rt_res) {
						mlx4_info(dev, "mlx4_add_resource_for_slave"
							       " failed, ret: %d for slave: %d",
							   rt_res, slave);
						mlx4_qp_free_icm(dev, param1);
						return rt_res;
					}
				}

			} else {
				mlx4_qp_free_icm(dev, param1);
				mlx4_delete_resource_for_slave(dev, RES_QP, slave, param1);
			}
			break;
		default:
			vhcr->errno = -EINVAL;
		}
		break;
	case RES_CQ:
		if (vhcr->op == MLX4_CMD_ALLOC_RES) {
			vhcr->errno = mlx4_cq_alloc_icm(dev, &ret);
			if (!vhcr->errno) {
					vhcr->out_param = ret;
					rt_res = mlx4_add_resource_for_slave(dev, RES_CQ, slave, ret, RES_INIT);
					if (rt_res) {
						mlx4_info(dev, "mlx4_add_resource_for_slave failed,"
							       " ret: %d for slave: %d", rt_res, slave);
						mlx4_cq_free_icm(dev, ret);
						return rt_res;
					}
			}
		} else {
				mlx4_cq_free_icm(dev, param1);
				mlx4_delete_resource_for_slave(dev,
							       RES_CQ,
							       slave,
							       param1);
		}
		break;
	case RES_SRQ:
		if (vhcr->op == MLX4_CMD_ALLOC_RES) {
			vhcr->errno = mlx4_srq_alloc_icm(dev, &ret);
			if (!vhcr->errno) {
					vhcr->out_param = ret;
					rt_res = mlx4_add_resource_for_slave(dev,
									      RES_SRQ,
									      slave,
									      ret,
									      RES_INIT);
					if (rt_res) {
						mlx4_info(dev, "mlx4_add_resource_for_slave"
							       " failed, ret: %d for slave:"
							       " %d", rt_res, slave);
						mlx4_srq_free_icm(dev, ret);
						return rt_res;
					}
			}
		} else {
				mlx4_srq_free_icm(dev, param1);
				mlx4_delete_resource_for_slave(dev, RES_SRQ, slave, param1);
		}
		break;
	case RES_MPT:
		switch (vhcr->op_modifier) {
		case ICM_RESERVE:
			if (vhcr->op == MLX4_CMD_ALLOC_RES) {
				ret = mlx4_mr_reserve(dev);
				if (ret == -1)
					vhcr->errno = -ENOMEM;
				else {
					res_bit_mask = calculate_bitmap_mask(dev, RES_MPT);
					vhcr->out_param = ret;
					res_id = ret & res_bit_mask;
					rt_res = mlx4_add_resource_for_slave(dev, RES_MPT,
									      slave,
									      res_id,
									      RES_INIT);
					if (rt_res) {
						mlx4_info(dev, "mlx4_add_resource_for_slave"
							       " failed, ret: %d for "
							       "slave: %d\n",
							   rt_res, slave);
						mlx4_mr_release(dev, ret);
						return rt_res;
					}
				}
			} else {
				mlx4_mr_release(dev, param1);
				res_bit_mask = calculate_bitmap_mask(dev, RES_MPT);
				res_id = param1 & res_bit_mask ;
				mlx4_delete_resource_for_slave(dev, RES_MPT, slave, res_id);
			}
			break;
		case ICM_ALLOC:
			if (vhcr->op == MLX4_CMD_ALLOC_RES) {
					vhcr->errno = mlx4_mr_alloc_icm(dev, param1);
					if (!vhcr->errno)  {
						res_bit_mask = calculate_bitmap_mask(dev, RES_MPT);
						res_id = param1 & res_bit_mask ;
						rt_res = mlx4_add_resource_for_slave
							(dev, RES_MPT, slave, res_id,
							  RES_ALLOCATED_AFTER_RESERVATION);
						if (rt_res) {
							mlx4_info(dev,
								   "mlx4_add_resource_for_slave"
								   "failed, ret: %d for"
								   " slave: %d\n",
								   rt_res, slave);
							mlx4_mr_free_icm(dev, param1);
							return rt_res;
						}
					}
			} else {
				mlx4_mr_free_icm(dev, param1);
				res_bit_mask = calculate_bitmap_mask(dev, RES_MPT);
				res_id = param1 & res_bit_mask ;
				mlx4_delete_resource_for_slave(dev, RES_MPT, slave, res_id);
			}
			break;
		default:
			vhcr->errno = -EINVAL;
		}
		break;
	case RES_MTT:
		if (vhcr->op == MLX4_CMD_ALLOC_RES) {
		ret = mlx4_alloc_mtt_range(dev, param1 /* order */);
		if (ret == -1) {
			vhcr->errno = -ENOMEM;
			mlx4_info(dev, "mlx4_alloc_mtt_range failed, ret: %d for slave: %d", ret, slave);
		}
		else {
			vhcr->out_param = ret;
			rt_res = mlx4_add_mtt_resource_for_slave(dev, slave, ret, RES_INIT, param1); 
			if (rt_res) {
				mlx4_info(dev, "mlx4_add_resource_for_slave failed, ret: %d for slave: %d", rt_res, slave);
				mlx4_free_mtt_range(dev, ret /* first */, param1 /* order */);
				return rt_res;
			}

		}
		} else {
			mlx4_free_mtt_range(dev, param1 /* first */, param2 /* order */);
			mlx4_delete_resource_for_slave(dev, RES_MTT, slave, param1);
		}
		break;
	case RES_MAC:
		vhcr->in_param |= (u64) (vep_num) << 48;
		switch (vhcr->op) {
		case MLX4_CMD_ALLOC_RES:
			ret = mlx4_register_mac(dev, vhcr->op_modifier,
						vhcr->in_param, (int *) &vhcr->out_param, 1);
			vhcr->errno = ret;
			if (!ret) {
				rt_res = mlx4_add_resource_for_slave(dev, RES_MAC, slave, vhcr->out_param, RES_INIT);
				if (rt_res) {
					mlx4_info(dev, "mlx4_add_resource_for_slave failed, ret: %d for slave: %d", rt_res, slave);
					mlx4_unregister_mac(dev, vhcr->op_modifier, ret);
					return rt_res;
				}
				rt_res = mlx4_add_port_to_tracked_mac(dev, (int) vhcr->out_param, vhcr->op_modifier);
				if (rt_res) {
					mlx4_info(dev, "mlx4_add_port_to_tracked_mac failed, ret: %d for slave: %d", rt_res, slave);
					mlx4_unregister_mac(dev, vhcr->op_modifier, ret);
					return rt_res;
				}
				/*When asking for mac the master already reserved that qp, the slave needs to allocate it */
				rt_res = mlx4_add_resource_for_slave(dev, RES_QP, slave, (int) vhcr->out_param, RES_ALLOCATED_WITH_MASTER_RESERVATION);
				if (rt_res) {
					mlx4_info(dev, "mlx4_add_resource_for_slave failed, ret: %d for slave: %d", rt_res, slave);
					mlx4_unregister_mac(dev, vhcr->op_modifier, ret);
					return rt_res;
				}
			}
			break;
		case MLX4_CMD_FREE_RES:
			mlx4_unregister_mac(dev, vhcr->op_modifier, vhcr->in_param);
			mlx4_delete_resource_for_slave(dev, RES_MAC, slave, vhcr->in_param);
			/*When removing mac the master also de-allocates qp*/
			mlx4_delete_resource_for_slave(dev, RES_QP, slave, vhcr->in_param);
			break;
		case MLX4_CMD_REPLACE_RES:
			ret = mlx4_replace_mac(dev, vhcr->op_modifier,
					       vhcr->out_param, vhcr->in_param, 1);
			vhcr->errno = ret;
			break;
		default:
			vhcr->errno = -EINVAL;
		}

		break;
	default:
		vhcr->errno = -EINVAL;
	}
	return 0;
}

static int mlx4_DMA_wrapper(struct mlx4_dev *dev, int slave,
			    struct mlx4_vhcr *vhcr,
			    struct mlx4_cmd_mailbox *inbox,
			    struct mlx4_cmd_mailbox *outbox)
{
	u64 in_param = inbox ? inbox->dma : vhcr->in_param;

	in_param |= (u64) slave;
	return mlx4_cmd(dev, in_param, vhcr->in_modifier,
			vhcr->op_modifier, vhcr->op, MLX4_CMD_TIME_CLASS_C);
}

static int mlx4_DMA_outbox_wrapper(struct mlx4_dev *dev, int slave,
				   struct mlx4_vhcr *vhcr,
				   struct mlx4_cmd_mailbox *inbox,
				   struct mlx4_cmd_mailbox *outbox)
{
	u64 in_param = inbox ? inbox->dma : vhcr->in_param;
	u64 out_param = outbox ? outbox->dma : vhcr->out_param;

	in_param |= (u64) slave;
	return mlx4_cmd_box(dev, in_param, out_param,
			    vhcr->in_modifier, vhcr->op_modifier, vhcr->op,
			    MLX4_CMD_TIME_CLASS_C);
}

static struct mlx4_cmd_info {
	u16 opcode;
	bool has_inbox;
	bool has_outbox;
	bool out_is_imm;
	int (*verify)(struct mlx4_dev *dev, int slave, struct mlx4_vhcr *vhcr,
					    struct mlx4_cmd_mailbox *inbox);
	int (*wrapper)(struct mlx4_dev *dev, int slave, struct mlx4_vhcr *vhcr,
					     struct mlx4_cmd_mailbox *inbox,
					     struct mlx4_cmd_mailbox *outbox);
} cmd_info[] = {
	{
		.opcode = MLX4_CMD_QUERY_FW,
		.has_inbox = false,
		.has_outbox = true,
		.out_is_imm = false,
		.verify = NULL,
		.wrapper = NULL
	},
	{
		.opcode = MLX4_CMD_QUERY_SLAVE_CAP,
		.has_inbox = false,
		.has_outbox = true,
		.out_is_imm = false,
		.verify = NULL,
		.wrapper = mlx4_QUERY_SLAVE_CAP_wrapper
	},
	{
		.opcode = MLX4_CMD_QUERY_ADAPTER,
		.has_inbox = false,
		.has_outbox = true,
		.out_is_imm = false,
		.verify = NULL,
		.wrapper = NULL
	},
	{
		.opcode = MLX4_CMD_GET_SLAVE_SQP,
		.has_inbox = false,
		.has_outbox = true,
		.out_is_imm = false,
		.verify = NULL,
		.wrapper = mlx4_GET_SLAVE_SQP_wrapper
	},

	{
		.opcode = MLX4_CMD_COMM_INT,
		.has_inbox = false,
		.has_outbox = false,
		.out_is_imm = false,
		.verify = NULL,
		.wrapper = mlx4_COMM_INT_wrapper
	},
	{
		.opcode = MLX4_CMD_INIT_PORT,
		.has_inbox = false,
		.has_outbox = false,
		.out_is_imm = false,
		.verify = NULL,
		.wrapper = mlx4_INIT_PORT_wrapper
	},
	{
		.opcode = MLX4_CMD_CLOSE_PORT,
		.has_inbox = false,
		.has_outbox = false,
		.out_is_imm  = false,
		.verify = NULL,
		.wrapper = mlx4_CLOSE_PORT_wrapper
	},
	{
		.opcode = MLX4_CMD_QUERY_PORT,
		.has_inbox = false,
		.has_outbox = true,
		.out_is_imm = false,
		.verify = NULL,
		.wrapper = mlx4_QUERY_PORT_wrapper
	},
	{
		.opcode = MLX4_CMD_SET_PORT,
		.has_inbox = true,
		.has_outbox = false,
		.out_is_imm = false,
		.verify = NULL,
		.wrapper = mlx4_SET_PORT_wrapper
	},

	{
		.opcode = MLX4_CMD_MAP_EQ,
		.has_inbox = false,
		.has_outbox = false,
		.out_is_imm = false,
		.verify = NULL,
		.wrapper = mlx4_MAP_EQ_wrapper
	},
	{
		.opcode = MLX4_CMD_SW2HW_EQ,
		.has_inbox = true,
		.has_outbox = false,
		.out_is_imm = false,
		.verify = NULL, /*need verifier */
		.wrapper = mlx4_DMA_wrapper
	},
	{
		.opcode = MLX4_CMD_HW_HEALTH_CHECK,
		.has_inbox = false,
		.has_outbox = false,
		.out_is_imm = false,
		.verify = NULL,
		.wrapper = NULL
	},
	{
		.opcode = MLX4_CMD_NOP,
		.has_inbox = false,
		.has_outbox = false,
		.out_is_imm = false,
		.verify = NULL,
		.wrapper = NULL
	},
	{
		.opcode = MLX4_CMD_ALLOC_RES,
		.has_inbox = false,
		.has_outbox = false,
		.out_is_imm = true,
		.verify = mlx4_verify_resource_wrapper,
		.wrapper = mlx4_RESOURCE_wrapper
	},
	{
		.opcode = MLX4_CMD_FREE_RES,
		.has_inbox = false,
		.has_outbox = false,
		.out_is_imm = false,
		.verify = NULL,
		.wrapper = mlx4_RESOURCE_wrapper
	},
	{
		.opcode = MLX4_CMD_GET_EVENT,
		.has_inbox = false,
		.has_outbox = false,
		.out_is_imm = true,
		.verify = NULL,
		.wrapper = mlx4_GET_EVENT_wrapper
	},

	{
		.opcode = MLX4_CMD_REPLACE_RES,
		.has_inbox = false,
		.has_outbox = false,
		.out_is_imm = true,
		.verify = NULL,
		.wrapper = mlx4_RESOURCE_wrapper
	},
	{
		.opcode = MLX4_CMD_SW2HW_MPT,
		.has_inbox = true,
		.has_outbox = false,
		.out_is_imm = false,
		.verify = NULL,
		.wrapper = mlx4_DMA_wrapper
	},
	{
		.opcode = MLX4_CMD_QUERY_MPT,
		.has_inbox = false,
		.has_outbox = true,
		.out_is_imm = false,
		.verify = mlx4_verify_mpt_index,
		.wrapper = NULL
	},
	{
		.opcode = MLX4_CMD_HW2SW_MPT,
		.has_inbox = false,
		.has_outbox = false,
		.out_is_imm = false,
		.verify = mlx4_verify_mpt_index,
		.wrapper = NULL
	},
	{
		.opcode = MLX4_CMD_READ_MTT,
		.has_inbox = false,
		.has_outbox = true,
		.out_is_imm = false,
		.verify = NULL, /* need verifier */
		.wrapper = NULL
	},
	{
		.opcode = MLX4_CMD_WRITE_MTT,
		.has_inbox = true,
		.has_outbox = false,
		.out_is_imm = false,
		.verify = NULL, /* need verifier */
		.wrapper = mlx4_WRITE_MTT_wrapper
	},
	{
		.opcode = MLX4_CMD_SYNC_TPT,
		.has_inbox = true,
		.has_outbox = false,
		.out_is_imm = false,
		.verify = NULL, /* need verifier */
		.wrapper = NULL
	},

	{
		.opcode = MLX4_CMD_HW2SW_EQ,
		.has_inbox = false,
		.has_outbox = true,
		.out_is_imm = false,
		.verify = NULL, /* need verifier */
		.wrapper = mlx4_DMA_outbox_wrapper
	},
	{
		.opcode = MLX4_CMD_QUERY_EQ,
		.has_inbox = false,
		.has_outbox = true,
		.out_is_imm = false,
		.verify = NULL, /* need verifier */
		.wrapper = NULL
	},
	{
		.opcode = MLX4_CMD_SW2HW_CQ,
		.has_inbox = true,
		.has_outbox = false,
		.out_is_imm = false,
		.verify = mlx4_verify_cq_index,
		.wrapper = mlx4_DMA_wrapper
	},
	{
		.opcode = MLX4_CMD_HW2SW_CQ,
		.has_inbox = false,
		.has_outbox = false,
		.out_is_imm = false,
		.verify = mlx4_verify_cq_index,
		.wrapper = mlx4_DMA_wrapper
	},
	{
		.opcode = MLX4_CMD_QUERY_CQ,
		.has_inbox = false,
		.has_outbox = true,
		.out_is_imm = false,
		.verify = mlx4_verify_cq_index,
		.wrapper = NULL
	},
	{
		.opcode = MLX4_CMD_MODIFY_CQ,
		.has_inbox = true,
		.has_outbox = false,
		.out_is_imm = true,
		.verify = mlx4_verify_cq_index,
		.wrapper = NULL
	},
	{
		.opcode = MLX4_CMD_SW2HW_SRQ,
		.has_inbox = true,
		.has_outbox = false,
		.out_is_imm = false,
		.verify = mlx4_verify_srq_index,
		.wrapper = mlx4_DMA_wrapper
	},
	{
		.opcode = MLX4_CMD_HW2SW_SRQ,
		.has_inbox = false,
		.has_outbox = false,
		.out_is_imm = false,
		.verify = mlx4_verify_srq_index,
		.wrapper = NULL
	},
	{
		.opcode = MLX4_CMD_QUERY_SRQ,
		.has_inbox = false,
		.has_outbox = true,
		.out_is_imm = false,
		.verify = mlx4_verify_srq_index,
		.wrapper = NULL
	},
	{
		.opcode = MLX4_CMD_ARM_SRQ,
		.has_inbox = false,
		.has_outbox = false,
		.out_is_imm = false,
		.verify = mlx4_verify_srq_aram,
		.wrapper = NULL
	},
	{
		.opcode = MLX4_CMD_RST2INIT_QP,
		.has_inbox = true,
		.has_outbox = false,
		.out_is_imm = false,
		.verify = mlx4_verify_qp_index,
		.wrapper = mlx4_DMA_wrapper
	},
	{
		.opcode = MLX4_CMD_INIT2RTR_QP,
		.has_inbox = true,
		.has_outbox = false,
		.out_is_imm = false,
		.verify = mlx4_verify_qp_index,
		.wrapper = mlx4_DMA_wrapper
	},
	{
		.opcode = MLX4_CMD_RTR2RTS_QP,
		.has_inbox = true,
		.has_outbox = false,
		.out_is_imm = false,
		.verify = mlx4_verify_qp_index,
		.wrapper = mlx4_RTR2RTS_QP_wrapper
	},
	{
		.opcode = MLX4_CMD_RTS2RTS_QP,
		.has_inbox = true,
		.has_outbox = false,
		.out_is_imm = false,
		.verify = mlx4_verify_qp_index,
		.wrapper = mlx4_DMA_wrapper
	},
	{
		.opcode = MLX4_CMD_SQERR2RTS_QP,
		.has_inbox = true,
		.has_outbox = false,
		.out_is_imm = false,
		.verify = mlx4_verify_qp_index,
		.wrapper = mlx4_DMA_wrapper
	},
	{
		.opcode = MLX4_CMD_2ERR_QP,
		.has_inbox = false,
		.has_outbox = false,
		.out_is_imm = false,
		.verify = mlx4_verify_qp_index,
		.wrapper = mlx4_DMA_wrapper
	},
	{
		.opcode = MLX4_CMD_RTS2SQD_QP,
		.has_inbox = false,
		.has_outbox = false,
		.out_is_imm = false,
		.verify = mlx4_verify_qp_index,
		.wrapper = mlx4_DMA_wrapper
	},
	{
		.opcode = MLX4_CMD_SQD2SQD_QP,
		.has_inbox = true,
		.has_outbox = false,
		.out_is_imm = false,
		.verify = mlx4_verify_qp_index,
		.wrapper = mlx4_DMA_wrapper
	},
	{
		.opcode = MLX4_CMD_SQD2RTS_QP,
		.has_inbox = true,
		.has_outbox = false,
		.out_is_imm = false,
		.verify = mlx4_verify_qp_index,
		.wrapper = mlx4_DMA_wrapper
	},
	{
		.opcode = MLX4_CMD_2RST_QP,
		.has_inbox = false,
		.has_outbox = false,
		.out_is_imm = false,
		.verify = mlx4_verify_qp_index,
		.wrapper = NULL
	},
	{
		.opcode = MLX4_CMD_QUERY_QP,
		.has_inbox = false,
		.has_outbox = true,
		.out_is_imm = false,
		.verify = mlx4_verify_qp_index,
		.wrapper = NULL
	},
	{
		.opcode = MLX4_CMD_INIT2INIT_QP,
		.has_inbox = true,
		.has_outbox = false,
		.out_is_imm = false,
		.verify = mlx4_verify_qp_index,
		.wrapper = NULL
	},
	{
		.opcode = MLX4_CMD_SUSPEND_QP,
		.has_inbox = false,
		.has_outbox = false,
		.out_is_imm = false,
		.verify = mlx4_verify_qp_index,
		.wrapper = NULL
	},
	{
		.opcode = MLX4_CMD_UNSUSPEND_QP,
		.has_inbox = false,
		.has_outbox = false,
		.out_is_imm = false,
		.verify = mlx4_verify_qp_index,
		.wrapper = NULL
	},
	{
		.opcode = MLX4_CMD_CONF_SPECIAL_QP,
		.has_inbox = false,
		.has_outbox = false,
		.out_is_imm = false,
		.verify = NULL,
		.wrapper = mlx4_CONF_SPECIAL_QP_wrapper
	},
	{
		.opcode = MLX4_CMD_MAD_IFC,
		.has_inbox = true,
		.has_outbox = true,
		.out_is_imm = false,
		.verify = NULL, /* need verifier */
		.wrapper = NULL
	},

	/* Native multicast commands are not available for guests */
	{
		.opcode = MLX4_CMD_MCAST_ATTACH,
		.has_inbox = true,
		.has_outbox = false,
		.out_is_imm = false,
		.verify = NULL,
		.wrapper = mlx4_MCAST_wrapper
	},
	{
		.opcode = MLX4_CMD_PROMISC,
		.has_inbox = false,
		.has_outbox = false,
		.out_is_imm = false,
		.verify = NULL,
		.wrapper = mlx4_PROMISC_wrapper
	},
	{
		.opcode = MLX4_CMD_DIAG_RPRT,
		.has_inbox = false,
		.has_outbox = true,
		.out_is_imm = false,
		.verify = NULL, /* need verifier */
		.wrapper = NULL
	},
	{
		.opcode = MLX4_CMD_QUERY_IF_STAT,
		.has_inbox = false,
		.has_outbox = true,
		.out_is_imm = false,
		.verify = NULL,
		.wrapper = MLX4_CMD_QUERY_IF_STAT_wrapper
	},
	{
		.opcode = MLX4_CMD_SET_NODE,
		.has_inbox = false,
		.has_outbox = false,
		.out_is_imm = false,
		.verify = NULL,
		.wrapper = MLX4_CMD_SET_NODE_wrapper
	},


	/* Ethernet specific commands */
	{
		.opcode = MLX4_CMD_SET_VLAN_FLTR,
		.has_inbox = true,
		.has_outbox = false,
		.out_is_imm = false,
		.verify = NULL,
		.wrapper = mlx4_SET_VLAN_FLTR_wrapper
	},
	{
		.opcode = MLX4_CMD_SET_MCAST_FLTR,
		.has_inbox = false,
		.has_outbox = false,
		.out_is_imm = false,
		.verify = NULL,
		.wrapper = mlx4_SET_MCAST_FLTR_wrapper
	},
	{
		.opcode = MLX4_CMD_DUMP_ETH_STATS,
		.has_inbox = false,
		.has_outbox = true,
		.out_is_imm = false,
		.verify = NULL,
		.wrapper = mlx4_DUMP_ETH_STATS_wrapper
	},
};

static int mlx4_master_process_vhcr(struct mlx4_dev *dev, int slave)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_cmd_info *cmd = NULL;
	struct mlx4_vhcr *vhcr = priv->mfunc.vhcr;
	struct mlx4_cmd_mailbox *inbox = NULL;
	struct mlx4_cmd_mailbox *outbox = NULL;
	u64 in_param;
	u64 out_param;
	int ret;
	int i;

	/* DMA in the vHCR */
	ret = mlx4_ACCESS_MEM(dev, priv->mfunc.vhcr_dma, slave,
			      priv->mfunc.master.slave_state[slave].vhcr_dma,
			      ALIGN(sizeof(struct mlx4_vhcr),
				    MLX4_ACCESS_MEM_ALIGN), 1);
	if (ret) {
		mlx4_err(dev, "Failed reading vhcr\n");
		return ret;
	}

	/* Lookup command */
	for (i = 0; i < ARRAY_SIZE(cmd_info); ++i) {
		if (vhcr->op == cmd_info[i].opcode) {
			cmd = &cmd_info[i];
			break;
		}
	}
	if (!cmd) {
		mlx4_err(dev, "Unknown command:0x%x accepted from slave:%d\n",
							      vhcr->op, slave);
		vhcr->errno = -EINVAL;
		goto out_status;
	}

	/* Read inbox */
	if (cmd->has_inbox) {
		vhcr->in_param &= INBOX_MASK;
		inbox = mlx4_alloc_cmd_mailbox(dev);
		if (IS_ERR(inbox)) {
			ret = PTR_ERR(inbox);
			inbox = NULL;
			goto out;
		}

		/* FIXME: add mailbox size per-command */
		ret = mlx4_ACCESS_MEM(dev, inbox->dma, slave,
				      vhcr->in_param,
				      MLX4_MAILBOX_SIZE, 1);
		if (ret) {
			mlx4_err(dev, "Failed reading inbox\n");
			goto out;
		}
	}

	/* Apply permission and bound checks if applicable */
	if (cmd->verify && cmd->verify(dev, slave, vhcr, inbox)) {
		mlx4_warn(dev, "Command:0x%x from slave: %d failed protection checks for resource_id:%d\n", vhcr->op, slave, vhcr->in_modifier);
		vhcr->errno = -EPERM;
		goto out_status;
	}

	/* Allocate outbox */
	if (cmd->has_outbox) {
		outbox = mlx4_alloc_cmd_mailbox(dev);
		if (IS_ERR(outbox)) {
			ret = PTR_ERR(outbox);
			outbox = NULL;
			goto out;
		}
	}

	/* Execute the command! */
	if (cmd->wrapper)
		vhcr->errno = cmd->wrapper(dev, slave, vhcr, inbox, outbox);
	else {
		in_param = cmd->has_inbox ? (u64) inbox->dma : vhcr->in_param;
		out_param = cmd->has_outbox ? (u64) outbox->dma : vhcr->out_param;
		vhcr->errno = __mlx4_cmd(dev, in_param, &out_param,
							cmd->out_is_imm,
							vhcr->in_modifier,
							vhcr->op_modifier,
							vhcr->op,
							vhcr->timeout);
		if (cmd->out_is_imm)
			vhcr->out_param = out_param;
	}

	/* Write outbox if command completed successfully */
	if (cmd->has_outbox && !vhcr->errno) {
		ret = mlx4_ACCESS_MEM(dev, outbox->dma, slave,
				      vhcr->out_param,
				      MLX4_MAILBOX_SIZE, 0);
		if (ret) {
			mlx4_err(dev, "Failed writing outbox\n");
			goto out;
		}
	}

out_status:
	/* DMA back vhcr result */
	ret = mlx4_ACCESS_MEM(dev, priv->mfunc.vhcr_dma, slave,
			      priv->mfunc.master.slave_state[slave].vhcr_dma,
			      ALIGN(sizeof(struct mlx4_vhcr),
				    MLX4_ACCESS_MEM_ALIGN), 0);
	if (ret)
		mlx4_err(dev, "Failed writing vhcr result\n");

	if (vhcr->errno)
		mlx4_warn(dev, "vhcr command:0x%x slave:%d failed with error:%d\n",
							vhcr->op, slave, vhcr->errno);
	/* Fall through... */

out:
	if (inbox)
		inbox->dma &= INBOX_MASK;
	mlx4_free_cmd_mailbox(dev, inbox);
	mlx4_free_cmd_mailbox(dev, outbox);
	return ret;
}

static void mlx4_master_do_cmd(struct mlx4_dev *dev, int slave, u8 cmd, u16 param, u8 toggle)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_slave_state *slave_state = priv->mfunc.master.slave_state;
	u8 toggle_next;
	u32 reply;

	if (cmd == MLX4_COMM_CMD_RESET) {
		mlx4_warn(dev, "Received reset from slave:%d\n", slave);
		slave_state[slave].active = false;
		goto reset_slave;
	}

	/* Increment next toggle token */
	toggle_next = slave_state[slave].comm_toggle + 1;
	if (toggle_next > 2)
		toggle_next = 1;
	if (toggle != toggle_next) {
		mlx4_warn(dev, "Incorrect token:%d from slave:%d expected:%d\n",
							toggle, toggle_next, slave);
		goto reset_slave;
	}

	switch (cmd) {
	case MLX4_COMM_CMD_VHCR0:
		if (slave_state[slave].last_cmd != MLX4_COMM_CMD_RESET)
			goto reset_slave;
		slave_state[slave].vhcr_dma = ((u64) param) << 48;
		break;
	case MLX4_COMM_CMD_VHCR1:
		if (slave_state[slave].last_cmd != MLX4_COMM_CMD_VHCR0)
			goto reset_slave;
		slave_state[slave].vhcr_dma |= ((u64) param) << 32;
		break;
	case MLX4_COMM_CMD_VHCR2:
		if (slave_state[slave].last_cmd != MLX4_COMM_CMD_VHCR1)
			goto reset_slave;
		slave_state[slave].vhcr_dma |= ((u64) param) << 16;
		break;
	case MLX4_COMM_CMD_VHCR_EN:
		if (slave_state[slave].last_cmd != MLX4_COMM_CMD_VHCR2)
			goto reset_slave;
		slave_state[slave].vhcr_dma |= param;
		if (mlx4_QUERY_FUNC(dev, slave, &slave_state[slave].pf_num)) {
			mlx4_err(dev, "Failed to determine physical function "
				      "number for slave %d\n", slave);
			goto reset_slave;
		}
		slave_state[slave].vep_num = slave_state[slave_state[slave].pf_num].vep_num;
		slave_state[slave].port_num = slave_state[slave_state[slave].pf_num].port_num;
		slave_state[slave].active = true;
		break;
	case MLX4_COMM_CMD_VHCR_POST:
		if ((slave_state[slave].last_cmd != MLX4_COMM_CMD_VHCR_EN) &&
		    (slave_state[slave].last_cmd != MLX4_COMM_CMD_VHCR_POST))
			goto reset_slave;
		if (mlx4_master_process_vhcr(dev, slave)) {
			mlx4_err(dev, "Failed processing vhcr for slave:%d, reseting slave.\n", slave);
			goto reset_slave;
		}
		break;
	default:
		mlx4_warn(dev, "Bad comm cmd:%d from slave:%d\n", cmd, slave);
		goto reset_slave;
	}

	slave_state[slave].last_cmd = cmd;
	slave_state[slave].comm_toggle = toggle_next;
	reply = (u32) toggle_next << 30;
	__raw_writel((__force u32) cpu_to_be32(reply),
		     &priv->mfunc.comm[slave].slave_read);
	wmb();
	if (mlx4_GEN_EQE(dev, slave, &priv->mfunc.master.cmd_eqe))
		mlx4_warn(dev, "Failed to generate command completion eqe "
			       "for slave %d\n", slave);

	return;

reset_slave:
	/* cleanup any slave resources */
	mlx4_delete_all_resources_for_slave(dev, slave);
	slave_state[slave].last_cmd = MLX4_COMM_CMD_RESET;
	slave_state[slave].comm_toggle = 0;
	memset(&slave_state[slave].event_eq, 0,
	       sizeof(struct mlx4_slave_event_eq_info));
	__raw_writel((__force u32) 0, &priv->mfunc.comm[slave].slave_write);
	__raw_writel((__force u32) 0, &priv->mfunc.comm[slave].slave_read);
	wmb();
}

/* master command processing */
void mlx4_master_comm_channel(struct work_struct *work)
{
	struct mlx4_mfunc_master_ctx *master = container_of(work,
							   struct mlx4_mfunc_master_ctx,
							   comm_work);
	struct mlx4_mfunc *mfunc = container_of(master, struct mlx4_mfunc, master);
	struct mlx4_priv *priv = container_of(mfunc, struct mlx4_priv, mfunc);
	struct mlx4_dev *dev = &priv->dev;
	u32 *bit_vec;
	u32 comm_cmd;
	u32 vec;
	int i, j, slave;

	bit_vec = master->comm_arm_bit_vector;
	for (i = 0; i < COMM_CHANNEL_BIT_ARRAY_SIZE; i++) {
		vec = be32_to_cpu(bit_vec[i]);
		for (j = 0; j < 32; j++) {
			if (!(vec & (1 << j)))
				continue;
			slave = (i * 32) + j;
			comm_cmd = swab32(readl(&mfunc->comm[slave].slave_write));
			if (comm_cmd >> 30 != master->slave_state[slave].comm_toggle)
				mlx4_master_do_cmd(dev, slave, comm_cmd >> 16, comm_cmd, comm_cmd >> 30);
		}
	}

	if (mlx4_ARM_COMM_CHANNEL(dev))
		mlx4_warn(dev, "Failed to arm comm channel events");
}

int mlx4_multi_func_init(struct mlx4_dev *dev)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_slave_state *s_state;
	int i, err, port;

	priv->mfunc.vhcr = dma_alloc_coherent(&(dev->pdev->dev), PAGE_SIZE,
					    &priv->mfunc.vhcr_dma,
					    GFP_KERNEL);
	if (!priv->mfunc.vhcr) {
		mlx4_err(dev, "Couldn't allocate vhcr.\n");
		return -ENOMEM;
	}

	if (mlx4_is_master(dev))
		priv->mfunc.comm = ioremap(pci_resource_start(dev->pdev,
							    priv->fw.comm_bar) +
								priv->fw.comm_base,
							    MLX4_COMM_PAGESIZE);
	else
		priv->mfunc.comm = ioremap(pci_resource_start(dev->pdev, 2) +
							    MLX4_SLAVE_COMM_BASE,
							    MLX4_COMM_PAGESIZE);
	if (!priv->mfunc.comm) {
		mlx4_err(dev, "Couldn't map communication vector.");
		goto err_vhcr;
	}

	if (mlx4_is_master(dev)) {
		priv->mfunc.master.slave_state = kzalloc(dev->num_slaves *
					   sizeof(struct mlx4_slave_state),
					   GFP_KERNEL);
		if (!priv->mfunc.master.slave_state)
			goto err_comm;

		for (i = 0; i < dev->num_slaves; ++i) {
			s_state = &priv->mfunc.master.slave_state[i];
			s_state->last_cmd = MLX4_COMM_CMD_RESET;
			for (port = 1; port <= MLX4_MAX_PORTS; port++) {
				s_state->vlan_filter[port] =
					kzalloc(sizeof(struct mlx4_vlan_fltr),
						GFP_KERNEL);
				if (!s_state->vlan_filter[port]) {
					if (--port)
						kfree(s_state->vlan_filter[port]);
					goto err_slaves;
				}
				INIT_LIST_HEAD(&s_state->mcast_filters[port]);
			}
			spin_lock_init(&s_state->lock);
		}

		memset(&priv->mfunc.master.cmd_eqe, 0, sizeof(struct mlx4_eqe));
		priv->mfunc.master.cmd_eqe.type = MLX4_EVENT_TYPE_CMD;
		INIT_WORK(&priv->mfunc.master.comm_work, mlx4_master_comm_channel);
		INIT_WORK(&priv->mfunc.master.slave_event_work, mlx4_gen_slave_eqe);
		INIT_WORK(&priv->mfunc.master.vep_config_work, mlx4_update_vep_config);
		spin_lock_init(&priv->mfunc.master.vep_config_lock);
		priv->mfunc.master.comm_wq = create_singlethread_workqueue("mlx4_comm");
		if (!priv->mfunc.master.comm_wq)
			goto err_slaves;

		if (mlx4_init_resource_tracker(dev))
			goto err_thread;


		err = mlx4_set_vep_maps(dev);
		if (err) {
			mlx4_err(dev, "Failed to set veps mapping\n");
			goto err_resource;
		}
		dev->caps.vep_num = priv->mfunc.master.slave_state[dev->caps.function].vep_num;

		mlx4_QUERY_VEP_CFG(dev, priv->mfunc.master.slave_state[dev->caps.function].vep_num,
				   priv->mfunc.master.slave_state[dev->caps.function].port_num,
				   &priv->mfunc.master.slave_state[dev->caps.function].vep_cfg);
		dev->caps.def_mac[priv->mfunc.master.slave_state[dev->caps.function].port_num] =
			priv->mfunc.master.slave_state[dev->caps.function].vep_cfg.mac;

		err = mlx4_ARM_COMM_CHANNEL(dev);
		if (err) {
			mlx4_err(dev, " Failed to arm comm channel eq: %x\n", err);
			goto err_resource;
		}

	} else {
		sema_init(&priv->cmd.slave_sem, 1);
		priv->cmd.comm_toggle = 0;
	}
	return 0;

err_resource:
	mlx4_free_resource_tracker(dev);
err_thread:
	flush_workqueue(priv->mfunc.master.comm_wq);
	destroy_workqueue(priv->mfunc.master.comm_wq);
err_slaves:
	while (--i) {
		for (port = 1; port <= MLX4_MAX_PORTS; port++)
			kfree(priv->mfunc.master.slave_state[i].vlan_filter[port]);
	}
	kfree(priv->mfunc.master.slave_state);
err_comm:
	iounmap(priv->mfunc.comm);
err_vhcr:
	dma_free_coherent(&(dev->pdev->dev), PAGE_SIZE,
					     priv->mfunc.vhcr,
					     priv->mfunc.vhcr_dma);
	priv->mfunc.vhcr = NULL;
	return -ENOMEM;
}

int mlx4_cmd_init(struct mlx4_dev *dev)
{
	struct mlx4_priv *priv = mlx4_priv(dev);

	mutex_init(&priv->cmd.hcr_mutex);
	sema_init(&priv->cmd.poll_sem, 1);
	priv->cmd.use_events = 0;
	priv->cmd.toggle     = 1;

	priv->cmd.hcr = NULL;
	priv->mfunc.vhcr = NULL;

	if (!mlx4_is_slave(dev)) {
		priv->cmd.hcr = ioremap(pci_resource_start(dev->pdev, 0) +
					MLX4_HCR_BASE, MLX4_HCR_SIZE);
		if (!priv->cmd.hcr) {
			mlx4_err(dev, "Couldn't map command register.");
			return -ENOMEM;
		}
	}

	priv->cmd.pool = pci_pool_create("mlx4_cmd", dev->pdev,
					 MLX4_MAILBOX_SIZE,
					 MLX4_MAILBOX_SIZE, 0);
	if (!priv->cmd.pool)
		goto err_hcr;

	return 0;

err_hcr:
	if (!mlx4_is_slave(dev))
		iounmap(priv->cmd.hcr);
	return -ENOMEM;
}

void mlx4_multi_func_cleanup(struct mlx4_dev *dev)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	int i, port;

	if (mlx4_is_master(dev)) {
		flush_workqueue(priv->mfunc.master.comm_wq);
		destroy_workqueue(priv->mfunc.master.comm_wq);
		for (i = 0; i < dev->num_slaves; i++) {
			for (port = 1; port <= MLX4_MAX_PORTS; port++)
				kfree(priv->mfunc.master.slave_state[i].vlan_filter[port]);
		}
		kfree(priv->mfunc.master.slave_state);
	}

	iounmap(priv->mfunc.comm);
	dma_free_coherent(&(dev->pdev->dev), PAGE_SIZE,
			  priv->mfunc.vhcr,
			  priv->mfunc.vhcr_dma);
	priv->mfunc.vhcr = NULL;
}

void mlx4_cmd_cleanup(struct mlx4_dev *dev)
{
	struct mlx4_priv *priv = mlx4_priv(dev);

	pci_pool_destroy(priv->cmd.pool);

	if (!mlx4_is_slave(dev))
		iounmap(priv->cmd.hcr);
}

/*
 * Switch to using events to issue FW commands (can only be called
 * after event queue for command events has been initialized).
 */
int mlx4_cmd_use_events(struct mlx4_dev *dev)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	int i;
	int err = 0;

	priv->cmd.context = kmalloc(priv->cmd.max_cmds *
				   sizeof (struct mlx4_cmd_context),
				   GFP_KERNEL);
	if (!priv->cmd.context)
		return -ENOMEM;

	for (i = 0; i < priv->cmd.max_cmds; ++i) {
		priv->cmd.context[i].token = i;
		priv->cmd.context[i].next  = i + 1;
	}

	priv->cmd.context[priv->cmd.max_cmds - 1].next = -1;
	priv->cmd.free_head = 0;

	sema_init(&priv->cmd.event_sem, priv->cmd.max_cmds);
	spin_lock_init(&priv->cmd.context_lock);

	for (priv->cmd.token_mask = 1;
	     priv->cmd.token_mask < priv->cmd.max_cmds;
	     priv->cmd.token_mask <<= 1)
		; /* nothing */
	--priv->cmd.token_mask;

	priv->cmd.use_events = 1;

	if (mlx4_is_slave(dev)) {
		err = mlx4_cmd(dev, 0, 1, 0, MLX4_CMD_COMM_INT, MLX4_CMD_TIME_CLASS_A);
		if (err) {
			mlx4_err(dev, "Failed to move to events for the slave\n");
			priv->cmd.use_events = 0;
		}
	}

	down(&priv->cmd.poll_sem);

	return err;
}

/*
 * Switch back to polling (used when shutting down the device)
 */
void mlx4_cmd_use_polling(struct mlx4_dev *dev)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	int i;

	priv->cmd.use_events = 0;

	for (i = 0; i < priv->cmd.max_cmds; ++i)
		down(&priv->cmd.event_sem);

	kfree(priv->cmd.context);

	up(&priv->cmd.poll_sem);

	if (mlx4_is_slave(dev))
		mlx4_cmd(dev, 0, 0, 0, MLX4_CMD_COMM_INT, MLX4_CMD_TIME_CLASS_A);
}

struct mlx4_cmd_mailbox *mlx4_alloc_cmd_mailbox(struct mlx4_dev *dev)
{
	struct mlx4_cmd_mailbox *mailbox;

	mailbox = kmalloc(sizeof *mailbox, GFP_KERNEL);
	if (!mailbox)
		return ERR_PTR(-ENOMEM);

	mailbox->buf = pci_pool_alloc(mlx4_priv(dev)->cmd.pool, GFP_KERNEL,
				      &mailbox->dma);
	if (!mailbox->buf) {
		kfree(mailbox);
		return ERR_PTR(-ENOMEM);
	}

	return mailbox;
}
EXPORT_SYMBOL_GPL(mlx4_alloc_cmd_mailbox);

void mlx4_free_cmd_mailbox(struct mlx4_dev *dev, struct mlx4_cmd_mailbox *mailbox)
{
	if (!mailbox)
		return;

	pci_pool_free(mlx4_priv(dev)->cmd.pool, mailbox->buf, mailbox->dma);
	kfree(mailbox);
}
EXPORT_SYMBOL_GPL(mlx4_free_cmd_mailbox);

int mlx4_COMM_INT_wrapper(struct mlx4_dev *dev, int slave, struct mlx4_vhcr *vhcr,
			  struct mlx4_cmd_mailbox *inbox,
			  struct mlx4_cmd_mailbox *outbox)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_slave_event_eq_info *event_eq =
		&priv->mfunc.master.slave_state[slave].event_eq;

	if (vhcr->in_modifier)
		event_eq->use_int = true;
	else
		event_eq->use_int = false;

	return 0;
}

int MLX4_CMD_QUERY_IF_STAT_wrapper(struct mlx4_dev *dev, int slave,
				   struct mlx4_vhcr *vhcr,
				   struct mlx4_cmd_mailbox *inbox,
				   struct mlx4_cmd_mailbox *outbox) {
	return mlx4_cmd_box(dev, 0, outbox->dma, vhcr->in_modifier, 0,
			   MLX4_CMD_QUERY_IF_STAT, MLX4_CMD_TIME_CLASS_C);
}

int MLX4_CMD_SET_NODE_wrapper(struct mlx4_dev *dev, int slave,
				   struct mlx4_vhcr *vhcr,
				   struct mlx4_cmd_mailbox *inbox,
				   struct mlx4_cmd_mailbox *outbox) {
	return 0;
}

