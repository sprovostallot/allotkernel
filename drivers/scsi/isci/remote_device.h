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

#if !defined(_ISCI_REMOTE_DEVICE_H_)
#define _ISCI_REMOTE_DEVICE_H_
#include "scic_user_callback.h"

struct isci_host;
struct scic_sds_remote_device;

struct isci_remote_device {
	struct scic_sds_remote_device *sci_device_handle;
	enum isci_status status;
	struct isci_port *isci_port;
	struct domain_device *domain_dev;
	struct completion *completion;
	struct list_head node;
	struct list_head reqs_in_process;
	struct work_struct stop_work;
	spinlock_t state_lock;
	spinlock_t host_quiesce_lock;
	bool host_quiesce;
};

#define to_isci_remote_device(p)	\
	container_of(p, struct isci_remote_device, sci_remote_device);

#define ISCI_REMOTE_DEVICE_START_TIMEOUT 5000


/**
 * This function gets the status of the remote_device object.
 * @isci_device: This parameter points to the isci_remote_device object
 *
 * status of the object as a isci_status enum.
 */
static inline
enum isci_status isci_remote_device_get_state(
	struct isci_remote_device *isci_device)
{
	return (isci_device->host_quiesce)
	       ? isci_host_quiesce
	       : isci_device->status;
}


/**
 * isci_dev_from_domain_dev() - This accessor retrieves the remote_device
 *    object reference from the Linux domain_device reference.
 * @domdev,: This parameter points to the Linux domain_device object .
 *
 * A reference to the associated isci remote device.
 */
#define isci_dev_from_domain_dev(domdev) \
	((struct isci_remote_device *)(domdev)->lldd_dev)

void isci_remote_device_start_complete(
	struct isci_host *,
	struct isci_remote_device *,
	enum sci_status);

void isci_remote_device_stop_complete(
	struct isci_host *,
	struct isci_remote_device *,
	enum sci_status);

enum sci_status isci_remote_device_stop(
	struct isci_remote_device *isci_device);

void isci_remote_device_nuke_requests(
	struct isci_remote_device *isci_device);

void isci_remote_device_ready(
	struct isci_remote_device *);

void isci_remote_device_not_ready(
	struct isci_remote_device *,
	u32);

void isci_remote_device_gone(
	struct domain_device *domain_dev);

int isci_remote_device_found(
	struct domain_device *domain_dev);

bool isci_device_is_reset_pending(
	struct isci_host *isci_host,
	struct isci_remote_device *isci_device);

void isci_device_clear_reset_pending(
	struct isci_remote_device *isci_device);

void isci_device_set_host_quiesce_lock_state(
	struct isci_remote_device *isci_device,
	bool lock_state);

void isci_remote_device_change_state(
	struct isci_remote_device *isci_device,
	enum isci_status status);

#endif /* !defined(_ISCI_REMOTE_DEVICE_H_) */

