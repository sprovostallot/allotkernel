/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2008 - 2011 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 * The full GNU General Public License is included in this distribution
 * in the file called LICENSE.GPL.
 *
 * BSD LICENSE
 *
 * Copyright(c) 2008 - 2011 Intel Corporation. All rights reserved.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#if !defined(_ISCI_TASK_H_)
#define _ISCI_TASK_H_

struct isci_request;
struct isci_host;

/**
 * enum isci_tmf_cb_state - This enum defines the possible states in which the
 *    TMF callback function is invoked during the TMF execution process.
 *
 *
 */
enum isci_tmf_cb_state {

	isci_tmf_init_state = 0,
	isci_tmf_started,
	isci_tmf_timed_out
};

/**
 * enum isci_tmf_function_codes - This enum defines the possible preparations
 *    of task management requests.
 *
 *
 */
enum isci_tmf_function_codes {

	isci_tmf_func_none      = 0,
	isci_tmf_ssp_task_abort = TMF_ABORT_TASK,
	isci_tmf_ssp_lun_reset  = TMF_LU_RESET,
	isci_tmf_sata_srst_high = TMF_LU_RESET + 0x100, /* Non SCSI */
	isci_tmf_sata_srst_low  = TMF_LU_RESET + 0x101  /* Non SCSI */
};
/**
 * struct isci_tmf - This class represents the task management object which
 *    acts as an interface to libsas for processing task management requests
 *
 *
 */
struct isci_tmf {

	struct completion *complete;
	enum sas_protocol proto;
	union {
		struct sci_ssp_response_iu resp_iu;
		struct dev_to_host_fis d2h_fis;
	}                            resp;
	unsigned char lun[8];
	u16 io_tag;
	struct isci_remote_device *device;
	enum isci_tmf_function_codes tmf_code;
	int status;

	struct isci_timer *timeout_timer;

	/* The optional callback function allows the user process to
	 * track the TMF transmit / timeout conditions.
	 */
	void (*cb_state_func)(
		enum isci_tmf_cb_state,
		struct isci_tmf *, void *);
	void *cb_data;

};

static inline void isci_print_tmf(
	struct isci_tmf *tmf)
{
	if (SAS_PROTOCOL_SATA == tmf->proto)
		dev_dbg(&tmf->device->isci_port->isci_host->pdev->dev,
			"%s: status = %x\n"
			"tmf->resp.d2h_fis.status = %x\n"
			"tmf->resp.d2h_fis.error = %x\n",
			__func__,
			tmf->status,
			tmf->resp.d2h_fis.status,
			tmf->resp.d2h_fis.error);
	else
		dev_dbg(&tmf->device->isci_port->isci_host->pdev->dev,
			"%s: status = %x\n"
			"tmf->resp.resp_iu.data_present = %x\n"
			"tmf->resp.resp_iu.status = %x\n"
			"tmf->resp.resp_iu.data_length = %x\n"
			"tmf->resp.resp_iu.data[0] = %x\n"
			"tmf->resp.resp_iu.data[1] = %x\n"
			"tmf->resp.resp_iu.data[2] = %x\n"
			"tmf->resp.resp_iu.data[3] = %x\n",
			__func__,
			tmf->status,
			tmf->resp.resp_iu.data_present,
			tmf->resp.resp_iu.status,
			(tmf->resp.resp_iu.response_data_length[0] << 24) +
			(tmf->resp.resp_iu.response_data_length[1] << 16) +
			(tmf->resp.resp_iu.response_data_length[2] << 8) +
			tmf->resp.resp_iu.response_data_length[3],
			tmf->resp.resp_iu.data[0],
			tmf->resp.resp_iu.data[1],
			tmf->resp.resp_iu.data[2],
			tmf->resp.resp_iu.data[3]);
}


int isci_task_execute_task(
	struct sas_task *task,
	int num,
	gfp_t gfp_flags);

int isci_task_abort_task(
	struct sas_task *task);

int isci_task_abort_task_set(
	struct domain_device *d_device,
	u8 *lun);

int isci_task_clear_aca(
	struct domain_device *d_device,
	u8 *lun);

int isci_task_clear_task_set(
	struct domain_device *d_device,
	u8 *lun);

int isci_task_query_task(
	struct sas_task *task);

int isci_task_lu_reset(
	struct domain_device *d_device,
	u8 *lun);

int isci_task_clear_nexus_port(
	struct asd_sas_port *port);

int isci_task_clear_nexus_ha(
	struct sas_ha_struct *ha);

int isci_task_I_T_nexus_reset(
	struct domain_device *d_device);

void isci_task_request_complete(
	struct isci_host *isci_host,
	struct isci_request *request,
	enum sci_task_status completion_status);

u16 isci_task_ssp_request_get_io_tag_to_manage(
	struct isci_request *request);

u8 isci_task_ssp_request_get_function(
	struct isci_request *request);

u32 isci_task_ssp_request_get_lun(
	struct isci_request *request);

void *isci_task_ssp_request_get_response_data_address(
	struct isci_request *request);

u32 isci_task_ssp_request_get_response_data_length(
	struct isci_request *request);

int isci_queuecommand(
	struct scsi_cmnd *scsi_cmd,
	void (*donefunc)(struct scsi_cmnd *));

int isci_bus_reset_handler(struct scsi_cmnd *cmd);

void isci_task_build_tmf(
	struct isci_tmf *tmf,
	struct isci_remote_device *isci_device,
	enum isci_tmf_function_codes code,
	void (*tmf_sent_cb)(
		enum isci_tmf_cb_state,
		struct isci_tmf *, void *),
	void *cb_data);

int isci_task_execute_tmf(
	struct isci_host *isci_host,
	struct isci_tmf *tmf,
	unsigned long timeout_ms);

/**
 * enum isci_completion_selection - This enum defines the possible actions to
 *    take with respect to a given request's notification back to libsas.
 *
 *
 */
enum isci_completion_selection {

	isci_perform_normal_io_completion,      /* Normal notify (task_done) */
	isci_perform_aborted_io_completion,     /* No notification.   */
	isci_perform_error_io_completion        /* Use sas_task_abort */
};

static inline void isci_set_task_doneflags(
	struct sas_task *task)
{
	/* Since no futher action will be taken on this task,
	 * make sure to mark it complete from the lldd perspective.
	 */
	task->task_state_flags |= SAS_TASK_STATE_DONE;
	task->task_state_flags &= ~SAS_TASK_AT_INITIATOR;
	task->task_state_flags &= ~SAS_TASK_STATE_PENDING;
}
/**
 * isci_task_all_done() - This function clears the task bits to indicate the
 *    LLDD is done with the task.
 *
 *
 */
static inline void isci_task_all_done(
	struct sas_task *task)
{
	unsigned long flags;

	/* Since no futher action will be taken on this task,
	 * make sure to mark it complete from the lldd perspective.
	 */
	spin_lock_irqsave(&task->task_state_lock, flags);
	isci_set_task_doneflags(task);
	spin_unlock_irqrestore(&task->task_state_lock, flags);
}

/**
 * isci_task_set_completion_status() - This function sets the completion status
 *    for the request.
 * @task: This parameter is the completed request.
 * @response: This parameter is the response code for the completed task.
 * @status: This parameter is the status code for the completed task.
 *
 * none.
 */
static inline void isci_task_set_completion_status(
	struct sas_task *task,
	enum service_response response,
	enum exec_status status,
	enum isci_completion_selection task_notification_selection)
{
	unsigned long flags;

	spin_lock_irqsave(&task->task_state_lock, flags);

	task->task_status.resp = response;
	task->task_status.stat = status;

	/* Don't set DONE (or clear AT_INITIATOR) for any task going into the
	 * error path, because the EH interprets that as a handled error condition.
	 * Also don't take action if there is a reset pending.
	 */
	if ((task_notification_selection != isci_perform_error_io_completion)
	    && !(task->task_state_flags & SAS_TASK_NEED_DEV_RESET))
		isci_set_task_doneflags(task);

	spin_unlock_irqrestore(&task->task_state_lock, flags);
}
/**
 * isci_task_complete_for_upper_layer() - This function completes the request
 *    to the upper layer driver.
 * @host: This parameter is a pointer to the host on which the the request
 *    should be queued (either as an error or success).
 * @request: This parameter is the completed request.
 * @response: This parameter is the response code for the completed task.
 * @status: This parameter is the status code for the completed task.
 *
 * none.
 */
static inline void isci_task_complete_for_upper_layer(
	struct sas_task *task,
	enum service_response response,
	enum exec_status status,
	enum isci_completion_selection task_notification_selection)
{
	isci_task_set_completion_status(task, response, status,
					 task_notification_selection);


	/* Tasks aborted specifically by a call to the lldd_abort_task
	 * function should not be completed to the host in the regular path.
	 */
	switch (task_notification_selection) {
	case isci_perform_normal_io_completion:
		/* Normal notification (task_done) */
		dev_dbg(task->dev->port->ha->dev,
			"%s: Normal - task = %p, response=%d, status=%d\n",
			__func__, task, response, status);
		task->task_done(task);
		task->lldd_task = NULL;
		break;

	case isci_perform_aborted_io_completion:
		/* No notification because this request is already in the
		 * abort path.
		 */
		dev_warn(task->dev->port->ha->dev,
			 "%s: Aborted - task = %p, response=%d, status=%d\n",
			 __func__, task, response, status);
		break;

	case isci_perform_error_io_completion:
		/* Use sas_task_abort */
		dev_warn(task->dev->port->ha->dev,
			 "%s: Error - task = %p, response=%d, status=%d\n",
			 __func__, task, response, status);
		sas_task_abort(task);
		break;

	default:
		dev_warn(task->dev->port->ha->dev,
			 "%s: isci task notification default case!",
			 __func__);
		sas_task_abort(task);
		break;
	}
}

#endif /* !defined(_SCI_TASK_H_) */
