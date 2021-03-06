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

#include "intel_sas.h"
#include "sci_base_port.h"
#include "scic_controller.h"
#include "scic_phy.h"
#include "scic_port.h"
#include "scic_sds_controller.h"
#include "scic_sds_phy.h"
#include "scic_sds_phy_registers.h"
#include "scic_sds_port.h"
#include "scic_sds_port_registers.h"
#include "scic_sds_remote_device.h"
#include "scic_sds_remote_node_context.h"
#include "scic_sds_request.h"
#include "scic_user_callback.h"
#include "sci_environment.h"


static void scic_sds_port_invalid_link_up(
	struct scic_sds_port *this_port,
	struct scic_sds_phy *phy);
static void scic_sds_port_timeout_handler(
	void *port);
#define SCIC_SDS_PORT_MIN_TIMER_COUNT  (SCI_MAX_PORTS)
#define SCIC_SDS_PORT_MAX_TIMER_COUNT  (SCI_MAX_PORTS)

#define SCIC_SDS_PORT_HARD_RESET_TIMEOUT  (1000)

void sci_base_port_construct(
	struct sci_base_port *base_port,
	const struct sci_base_state *state_table)
{
	base_port->parent.private = NULL;
	sci_base_state_machine_construct(
		&base_port->state_machine,
		&base_port->parent,
		state_table,
		SCI_BASE_PORT_STATE_STOPPED
		);

	sci_base_state_machine_start(
		&base_port->state_machine
		);
}

/**
 *
 * @this_port: This is the port object to which the phy is being assigned.
 * @phy_index: This is the phy index that is being assigned to the port.
 *
 * This method will return a true value if the specified phy can be assigned to
 * this port The following is a list of phys for each port that are allowed: -
 * Port 0 - 3 2 1 0 - Port 1 -     1 - Port 2 - 3 2 - Port 3 - 3 This method
 * doesn't preclude all configurations.  It merely ensures that a phy is part
 * of the allowable set of phy identifiers for that port.  For example, one
 * could assign phy 3 to port 0 and no other phys.  Please refer to
 * scic_sds_port_is_phy_mask_valid() for information regarding whether the
 * phy_mask for a port can be supported. bool true if this is a valid phy
 * assignment for the port false if this is not a valid phy assignment for the
 * port
 */
bool scic_sds_port_is_valid_phy_assignment(
	struct scic_sds_port *this_port,
	u32 phy_index)
{
	/* Initialize to invalid value. */
	u32 existing_phy_index = SCI_MAX_PHYS;
	u32 index;

	if ((this_port->physical_port_index == 1) && (phy_index != 1)) {
		return false;
	}

	if (this_port->physical_port_index == 3 && phy_index != 3) {
		return false;
	}

	if (
		(this_port->physical_port_index == 2)
		&& ((phy_index == 0) || (phy_index == 1))
		) {
		return false;
	}

	for (index = 0; index < SCI_MAX_PHYS; index++) {
		if ((this_port->phy_table[index] != NULL)
		    && (index != phy_index)) {
			existing_phy_index = index;
		}
	}

	/*
	 * Ensure that all of the phys in the port are capable of
	 * operating at the same maximum link rate. */
	if (
		(existing_phy_index < SCI_MAX_PHYS)
		&& (this_port->owning_controller->user_parameters.sds1.phys[
			    phy_index].max_speed_generation !=
		    this_port->owning_controller->user_parameters.sds1.phys[
			    existing_phy_index].max_speed_generation)
		)
		return false;

	return true;
}

/**
 * This method requests a list (mask) of the phys contained in the supplied SAS
 *    port.
 * @this_port: a handle corresponding to the SAS port for which to return the
 *    phy mask.
 *
 * Return a bit mask indicating which phys are a part of this port. Each bit
 * corresponds to a phy identifier (e.g. bit 0 = phy id 0).
 */
u32 scic_sds_port_get_phys(struct scic_sds_port *this_port)
{
	u32 index;
	u32 mask;

	mask = 0;

	for (index = 0; index < SCI_MAX_PHYS; index++) {
		if (this_port->phy_table[index] != NULL) {
			mask |= (1 << index);
		}
	}

	return mask;
}

/**
 *
 * @this_port: This is the port object for which to determine if the phy mask
 *    can be supported.
 *
 * This method will return a true value if the port's phy mask can be supported
 * by the SCU. The following is a list of valid PHY mask configurations for
 * each port: - Port 0 - [[3  2] 1] 0 - Port 1 -        [1] - Port 2 - [[3] 2]
 * - Port 3 -  [3] This method returns a boolean indication specifying if the
 * phy mask can be supported. true if this is a valid phy assignment for the
 * port false if this is not a valid phy assignment for the port
 */
bool scic_sds_port_is_phy_mask_valid(
	struct scic_sds_port *this_port,
	u32 phy_mask)
{
	if (this_port->physical_port_index == 0) {
		if (((phy_mask & 0x0F) == 0x0F)
		    || ((phy_mask & 0x03) == 0x03)
		    || ((phy_mask & 0x01) == 0x01)
		    || (phy_mask == 0))
			return true;
	} else if (this_port->physical_port_index == 1) {
		if (((phy_mask & 0x02) == 0x02)
		    || (phy_mask == 0))
			return true;
	} else if (this_port->physical_port_index == 2) {
		if (((phy_mask & 0x0C) == 0x0C)
		    || ((phy_mask & 0x04) == 0x04)
		    || (phy_mask == 0))
			return true;
	} else if (this_port->physical_port_index == 3) {
		if (((phy_mask & 0x08) == 0x08)
		    || (phy_mask == 0))
			return true;
	}

	return false;
}

/**
 *
 * @this_port: This parameter specifies the port from which to return a
 *    connected phy.
 *
 * This method retrieves a currently active (i.e. connected) phy contained in
 * the port.  Currently, the lowest order phy that is connected is returned.
 * This method returns a pointer to a SCIS_SDS_PHY object. NULL This value is
 * returned if there are no currently active (i.e. connected to a remote end
 * point) phys contained in the port. All other values specify a struct scic_sds_phy
 * object that is active in the port.
 */
static struct scic_sds_phy *scic_sds_port_get_a_connected_phy(
	struct scic_sds_port *this_port
	) {
	u32 index;
	struct scic_sds_phy *phy;

	for (index = 0; index < SCI_MAX_PHYS; index++) {
		/*
		 * Ensure that the phy is both part of the port and currently
		 * connected to the remote end-point. */
		phy = this_port->phy_table[index];
		if (
			(phy != NULL)
			&& scic_sds_port_active_phy(this_port, phy)
			) {
			return phy;
		}
	}

	return NULL;
}

/**
 * scic_sds_port_set_phy() -
 * @out]: port The port object to which the phy assignement is being made.
 * @out]: phy The phy which is being assigned to the port.
 *
 * This method attempts to make the assignment of the phy to the port. If
 * successful the phy is assigned to the ports phy table. bool true if the phy
 * assignment can be made. false if the phy assignement can not be made. This
 * is a functional test that only fails if the phy is currently assigned to a
 * different port.
 */
enum sci_status scic_sds_port_set_phy(
	struct scic_sds_port *port,
	struct scic_sds_phy *phy)
{
	/*
	 * Check to see if we can add this phy to a port
	 * that means that the phy is not part of a port and that the port does
	 * not already have a phy assinged to the phy index. */
	if (
		(port->phy_table[phy->phy_index] == NULL)
		&& (scic_sds_phy_get_port(phy) == NULL)
		&& scic_sds_port_is_valid_phy_assignment(port, phy->phy_index)
		) {
		/*
		 * Phy is being added in the stopped state so we are in MPC mode
		 * make logical port index = physical port index */
		port->logical_port_index = port->physical_port_index;
		port->phy_table[phy->phy_index] = phy;
		scic_sds_phy_set_port(phy, port);

		return SCI_SUCCESS;
	}

	return SCI_FAILURE;
}

/**
 * scic_sds_port_clear_phy() -
 * @out]: port The port from which the phy is being cleared.
 * @out]: phy The phy being cleared from the port.
 *
 * This method will clear the phy assigned to this port.  This method fails if
 * this phy is not currently assinged to this port. bool true if the phy is
 * removed from the port. false if this phy is not assined to this port.
 */
enum sci_status scic_sds_port_clear_phy(
	struct scic_sds_port *port,
	struct scic_sds_phy *phy)
{
	/* Make sure that this phy is part of this port */
	if (
		(port->phy_table[phy->phy_index] == phy)
		&& (scic_sds_phy_get_port(phy) == port)
		) {
		/* Yep it is assigned to this port so remove it */
		scic_sds_phy_set_port(
			phy,
			&scic_sds_port_get_controller(port)->port_table[SCI_MAX_PORTS]
			);

		port->phy_table[phy->phy_index] = NULL;

		return SCI_SUCCESS;
	}

	return SCI_FAILURE;
}

/**
 * scic_sds_port_add_phy() -
 * @this_port: This parameter specifies the port in which the phy will be added.
 * @the_phy: This parameter is the phy which is to be added to the port.
 *
 * This method will add a PHY to the selected port. This method returns an
 * enum sci_status. SCI_SUCCESS the phy has been added to the port. Any other status
 * is failre to add the phy to the port.
 */
enum sci_status scic_sds_port_add_phy(
	struct scic_sds_port *this_port,
	struct scic_sds_phy *the_phy)
{
	return this_port->state_handlers->parent.add_phy_handler(
		       &this_port->parent, &the_phy->parent);
}


/**
 * scic_sds_port_remove_phy() -
 * @this_port: This parameter specifies the port in which the phy will be added.
 * @the_phy: This parameter is the phy which is to be added to the port.
 *
 * This method will remove the PHY from the selected PORT. This method returns
 * an enum sci_status. SCI_SUCCESS the phy has been removed from the port. Any other
 * status is failre to add the phy to the port.
 */
enum sci_status scic_sds_port_remove_phy(
	struct scic_sds_port *this_port,
	struct scic_sds_phy *the_phy)
{
	return this_port->state_handlers->parent.remove_phy_handler(
		       &this_port->parent, &the_phy->parent);
}

/**
 * This method requests the SAS address for the supplied SAS port from the SCI
 *    implementation.
 * @this_port: a handle corresponding to the SAS port for which to return the
 *    SAS address.
 * @sas_address: This parameter specifies a pointer to a SAS address structure
 *    into which the core will copy the SAS address for the port.
 *
 */
void scic_sds_port_get_sas_address(
	struct scic_sds_port *this_port,
	struct sci_sas_address *sas_address)
{
	u32 index;

	sas_address->high = 0;
	sas_address->low  = 0;

	for (index = 0; index < SCI_MAX_PHYS; index++) {
		if (this_port->phy_table[index] != NULL) {
			scic_sds_phy_get_sas_address(this_port->phy_table[index], sas_address);
		}
	}
}

/**
 * This method will indicate which protocols are supported by this port.
 * @this_port: a handle corresponding to the SAS port for which to return the
 *    supported protocols.
 * @protocols: This parameter specifies a pointer to an IAF protocol field
 *    structure into which the core will copy the protocol values for the port.
 *     The values are returned as part of a bit mask in order to allow for
 *    multi-protocol support.
 *
 */
static void scic_sds_port_get_protocols(
	struct scic_sds_port *this_port,
	struct sci_sas_identify_address_frame_protocols *protocols)
{
	u8 index;

	protocols->u.all = 0;

	for (index = 0; index < SCI_MAX_PHYS; index++) {
		if (this_port->phy_table[index] != NULL) {
			scic_sds_phy_get_protocols(this_port->phy_table[index], protocols);
		}
	}
}

/**
 * This method requests the SAS address for the device directly attached to
 *    this SAS port.
 * @this_port: a handle corresponding to the SAS port for which to return the
 *    SAS address.
 * @sas_address: This parameter specifies a pointer to a SAS address structure
 *    into which the core will copy the SAS address for the device directly
 *    attached to the port.
 *
 */
void scic_sds_port_get_attached_sas_address(
	struct scic_sds_port *this_port,
	struct sci_sas_address *sas_address)
{
	struct sci_sas_identify_address_frame_protocols protocols;
	struct scic_sds_phy *phy;

	/*
	 * Ensure that the phy is both part of the port and currently
	 * connected to the remote end-point. */
	phy = scic_sds_port_get_a_connected_phy(this_port);
	if (phy != NULL) {
		scic_sds_phy_get_attached_phy_protocols(phy, &protocols);

		if (!protocols.u.bits.stp_target) {
			scic_sds_phy_get_attached_sas_address(phy, sas_address);
		} else {
			scic_sds_phy_get_sas_address(phy, sas_address);
			sas_address->low += phy->phy_index;
		}
	} else {
		sas_address->high = 0;
		sas_address->low  = 0;
	}
}

/**
 * This method will indicate which protocols are supported by this remote
 *    device.
 * @this_port: a handle corresponding to the SAS port for which to return the
 *    supported protocols.
 * @protocols: This parameter specifies a pointer to an IAF protocol field
 *    structure into which the core will copy the protocol values for the port.
 *     The values are returned as part of a bit mask in order to allow for
 *    multi-protocol support.
 *
 */
void scic_sds_port_get_attached_protocols(
	struct scic_sds_port *this_port,
	struct sci_sas_identify_address_frame_protocols *protocols)
{
	struct scic_sds_phy *phy;

	/*
	 * Ensure that the phy is both part of the port and currently
	 * connected to the remote end-point. */
	phy = scic_sds_port_get_a_connected_phy(this_port);
	if (phy != NULL)
		scic_sds_phy_get_attached_phy_protocols(phy, protocols);
	else
		protocols->u.all = 0;
}

/**
 * This method returns the amount of memory requred for a port object.
 *
 * u32
 */

/**
 * This method returns the minimum number of timers required for all port
 *    objects.
 *
 * u32
 */

/**
 * This method returns the maximum number of timers required for all port
 *    objects.
 *
 * u32
 */

/**
 *
 * @this_port:
 * @port_index:
 *
 *
 */
void scic_sds_port_construct(
	struct scic_sds_port *this_port,
	u8 port_index,
	struct scic_sds_controller *owning_controller)
{
	u32 index;

	sci_base_port_construct(
		&this_port->parent,
		scic_sds_port_state_table
		);

	sci_base_state_machine_construct(
		scic_sds_port_get_ready_substate_machine(this_port),
		&this_port->parent.parent,
		scic_sds_port_ready_substate_table,
		SCIC_SDS_PORT_READY_SUBSTATE_WAITING
		);

	this_port->logical_port_index  = SCIC_SDS_DUMMY_PORT;
	this_port->physical_port_index = port_index;
	this_port->active_phy_mask     = 0;

	this_port->owning_controller = owning_controller;

	this_port->started_request_count = 0;
	this_port->assigned_device_count = 0;

	this_port->timer_handle = NULL;

	this_port->transport_layer_registers = NULL;
	this_port->port_task_scheduler_registers = NULL;

	for (index = 0; index < SCI_MAX_PHYS; index++) {
		this_port->phy_table[index] = NULL;
	}
}

/**
 * This method performs initialization of the supplied port. Initialization
 *    includes: - state machine initialization - member variable initialization
 *    - configuring the phy_mask
 * @this_port:
 * @transport_layer_registers:
 * @port_task_scheduler_registers:
 * @port_configuration_regsiter:
 *
 * enum sci_status SCI_FAILURE_UNSUPPORTED_PORT_CONFIGURATION This value is returned
 * if the phy being added to the port
 */
enum sci_status scic_sds_port_initialize(
	struct scic_sds_port *this_port,
	void *transport_layer_registers,
	void *port_task_scheduler_registers,
	void *port_configuration_regsiter,
	void *viit_registers)
{
	u32 tl_control;

	this_port->transport_layer_registers      = transport_layer_registers;
	this_port->port_task_scheduler_registers  = port_task_scheduler_registers;
	this_port->port_pe_configuration_register = port_configuration_regsiter;
	this_port->viit_registers                 = viit_registers;

	scic_sds_port_set_direct_attached_device_id(
		this_port,
		SCIC_SDS_REMOTE_NODE_CONTEXT_INVALID_INDEX
		);

	/*
	 * Hardware team recommends that we enable the STP prefetch
	 * for all ports */
	tl_control = SCU_TLCR_READ(this_port);
	tl_control |= SCU_TLCR_GEN_BIT(STP_WRITE_DATA_PREFETCH);
	SCU_TLCR_WRITE(this_port, tl_control);

	/*
	 * If this is not the dummy port make the assignment of
	 * the timer and start the state machine */
	if (this_port->physical_port_index != SCI_MAX_PORTS) {
		/* / @todo should we create the timer at create time? */
		this_port->timer_handle = scic_cb_timer_create(
			scic_sds_port_get_controller(this_port),
			scic_sds_port_timeout_handler,
			this_port
			);

	} else {
		/*
		 * Force the dummy port into a condition where it rejects all requests
		 * as its in an invalid state for any operation.
		 * / @todo should we set a set of specical handlers for the dummy port? */
		scic_sds_port_set_base_state_handlers(
			this_port, SCI_BASE_PORT_STATE_STOPPED
			);
	}

	return SCI_SUCCESS;
}

/**
 *
 * @this_port: This is the struct scic_sds_port object for which has a phy that has
 *    gone link up.
 * @the_phy: This is the struct scic_sds_phy object that has gone link up.
 * @do_notify_user: This parameter specifies whether to inform the user (via
 *    scic_cb_port_link_up()) as to the fact that a new phy as become ready.
 *
 * This method is the a general link up handler for the struct scic_sds_port object.
 * This function will determine if this struct scic_sds_phy can be assigned to this
 * struct scic_sds_port object. If the struct scic_sds_phy object can is not a valid PHY for
 * this port then the function will notify the SCIC_USER. A PHY can only be
 * part of a port if it's attached SAS ADDRESS is the same as all other PHYs in
 * the same port. none
 */
void scic_sds_port_general_link_up_handler(
	struct scic_sds_port *this_port,
	struct scic_sds_phy *the_phy,
	bool do_notify_user)
{
	struct sci_sas_address port_sas_address;
	struct sci_sas_address phy_sas_address;

	scic_sds_port_get_attached_sas_address(this_port, &port_sas_address);
	scic_sds_phy_get_attached_sas_address(the_phy, &phy_sas_address);

	/*
	 * If the SAS address of the new phy matches the SAS address of
	 * other phys in the port OR this is the first phy in the port,
	 * then activate the phy and allow it to be used for operations
	 * in this port. */
	if (
		(
			(phy_sas_address.high == port_sas_address.high)
			&& (phy_sas_address.low  == port_sas_address.low)
		)
		|| (this_port->active_phy_mask == 0)
		) {
		scic_sds_port_activate_phy(this_port, the_phy, do_notify_user);

		if (this_port->parent.state_machine.current_state_id
		    == SCI_BASE_PORT_STATE_RESETTING) {
			sci_base_state_machine_change_state(
				&this_port->parent.state_machine, SCI_BASE_PORT_STATE_READY
				);
		}
	} else {
		scic_sds_port_invalid_link_up(this_port, the_phy);
	}
}


enum sci_status scic_port_start(struct scic_sds_port *port)
{
	return port->state_handlers->parent.start_handler(&port->parent);
}


enum sci_status scic_port_stop(struct scic_sds_port *port)
{
	return port->state_handlers->parent.stop_handler(&port->parent);
}


enum sci_status scic_port_get_properties(
	struct scic_sds_port *port,
	struct scic_port_properties *prop)
{
	if ((port == NULL) ||
	    (port->logical_port_index == SCIC_SDS_DUMMY_PORT))
		return SCI_FAILURE_INVALID_PORT;

	prop->index    = port->logical_port_index;
	prop->phy_mask = scic_sds_port_get_phys(port);
	scic_sds_port_get_sas_address(port, &prop->local.sas_address);
	scic_sds_port_get_protocols(port, &prop->local.protocols);
	scic_sds_port_get_attached_sas_address(port, &prop->remote.sas_address);
	scic_sds_port_get_attached_protocols(port, &prop->remote.protocols);

	return SCI_SUCCESS;
}


enum sci_status scic_port_hard_reset(
	struct scic_sds_port *port,
	u32 reset_timeout)
{
	return port->state_handlers->parent.reset_handler(
		       &port->parent, reset_timeout);
}

/**
 *
 * @this_port: The port for which the direct attached device id is to be
 *    assigned.
 *
 * This method assigns the direct attached device ID for this port.
 */
void scic_sds_port_set_direct_attached_device_id(
	struct scic_sds_port *this_port,
	u32 device_id)
{
	u32 tl_control;

	SCU_STPTLDARNI_WRITE(this_port, device_id);

	/*
	 * The read should guarntee that the first write gets posted
	 * before the next write */
	tl_control = SCU_TLCR_READ(this_port);
	tl_control |= SCU_TLCR_GEN_BIT(CLEAR_TCI_NCQ_MAPPING_TABLE);
	SCU_TLCR_WRITE(this_port, tl_control);
}


/**
 *
 * @this_port: This is the port on which the phy should be enabled.
 * @the_phy: This is the specific phy which to enable.
 * @do_notify_user: This parameter specifies whether to inform the user (via
 *    scic_cb_port_link_up()) as to the fact that a new phy as become ready.
 *
 * This method will activate the phy in the port. Activation includes: - adding
 * the phy to the port - enabling the Protocol Engine in the silicon. -
 * notifying the user that the link is up. none
 */
void scic_sds_port_activate_phy(
	struct scic_sds_port *this_port,
	struct scic_sds_phy *the_phy,
	bool do_notify_user)
{
	struct scic_sds_controller *controller;
	struct sci_sas_identify_address_frame_protocols protocols;

	controller = scic_sds_port_get_controller(this_port);
	scic_sds_phy_get_attached_phy_protocols(the_phy, &protocols);

	/* If this is sata port then the phy has already been resumed */
	if (!protocols.u.bits.stp_target) {
		scic_sds_phy_resume(the_phy);
	}

	this_port->active_phy_mask |= 1 << the_phy->phy_index;

	scic_sds_controller_clear_invalid_phy(controller, the_phy);

	if (do_notify_user == true)
		scic_cb_port_link_up(this_port->owning_controller, this_port, the_phy);
}

/**
 *
 * @this_port: This is the port on which the phy should be deactivated.
 * @the_phy: This is the specific phy that is no longer active in the port.
 * @do_notify_user: This parameter specifies whether to inform the user (via
 *    scic_cb_port_link_down()) as to the fact that a new phy as become ready.
 *
 * This method will deactivate the supplied phy in the port. none
 */
void scic_sds_port_deactivate_phy(
	struct scic_sds_port *this_port,
	struct scic_sds_phy *the_phy,
	bool do_notify_user)
{
	this_port->active_phy_mask &= ~(1 << the_phy->phy_index);

	the_phy->max_negotiated_speed = SCI_SAS_NO_LINK_RATE;

	/* Re-assign the phy back to the LP as if it were a narrow port */
	SCU_PCSPExCR_WRITE(this_port, the_phy->phy_index, the_phy->phy_index);

	if (do_notify_user == true)
		scic_cb_port_link_down(this_port->owning_controller, this_port, the_phy);
}

/**
 *
 * @this_port: This is the port on which the phy should be disabled.
 * @the_phy: This is the specific phy which to disabled.
 *
 * This method will disable the phy and report that the phy is not valid for
 * this port object. None
 */
static void scic_sds_port_invalid_link_up(
	struct scic_sds_port *this_port,
	struct scic_sds_phy *the_phy)
{
	struct scic_sds_controller *controller = scic_sds_port_get_controller(this_port);

	/*
	 * Check to see if we have alreay reported this link as bad and if not go
	 * ahead and tell the SCI_USER that we have discovered an invalid link. */
	if ((controller->invalid_phy_mask & (1 << the_phy->phy_index)) == 0) {
		scic_sds_controller_set_invalid_phy(controller, the_phy);

		scic_cb_port_invalid_link_up(controller, this_port, the_phy);
	}
}

/**
 * This method returns false if the port only has a single phy object assigned.
 *     If there are no phys or more than one phy then the method will return
 *    true.
 * @this_port: The port for which the wide port condition is to be checked.
 *
 * bool true Is returned if this is a wide ported port. false Is returned if
 * this is a narrow port.
 */
static bool scic_sds_port_is_wide(struct scic_sds_port *this_port)
{
	u32 index;
	u32 phy_count = 0;

	for (index = 0; index < SCI_MAX_PHYS; index++) {
		if (this_port->phy_table[index] != NULL) {
			phy_count++;
		}
	}

	return phy_count != 1;
}

/**
 * This method is called by the PHY object when the link is detected. if the
 *    port wants the PHY to continue on to the link up state then the port
 *    layer must return true.  If the port object returns false the phy object
 *    must halt its attempt to go link up.
 * @this_port: The port associated with the phy object.
 * @the_phy: The phy object that is trying to go link up.
 *
 * true if the phy object can continue to the link up condition. true Is
 * returned if this phy can continue to the ready state. false Is returned if
 * can not continue on to the ready state. This notification is in place for
 * wide ports and direct attached phys.  Since there are no wide ported SATA
 * devices this could become an invalid port configuration.
 */
bool scic_sds_port_link_detected(
	struct scic_sds_port *this_port,
	struct scic_sds_phy *the_phy)
{
	struct sci_sas_identify_address_frame_protocols protocols;

	scic_sds_phy_get_attached_phy_protocols(the_phy, &protocols);

	if (
		(this_port->logical_port_index != SCIC_SDS_DUMMY_PORT)
		&& (protocols.u.bits.stp_target)
		&& scic_sds_port_is_wide(this_port)
		) {
		scic_sds_port_invalid_link_up(this_port, the_phy);

		return false;
	}

	return true;
}

/**
 * This method is the entry point for the phy to inform the port that it is now
 *    in a ready state
 * @this_port:
 *
 *
 */
void scic_sds_port_link_up(
	struct scic_sds_port *this_port,
	struct scic_sds_phy *the_phy)
{
	the_phy->is_in_link_training = false;

	this_port->state_handlers->link_up_handler(this_port, the_phy);
}

/**
 * This method is the entry point for the phy to inform the port that it is no
 *    longer in a ready state
 * @this_port:
 *
 *
 */
void scic_sds_port_link_down(
	struct scic_sds_port *this_port,
	struct scic_sds_phy *the_phy)
{
	this_port->state_handlers->link_down_handler(this_port, the_phy);
}

/**
 * This method is called to start an IO request on this port.
 * @this_port:
 * @the_device:
 * @the_io_request:
 *
 * enum sci_status
 */
enum sci_status scic_sds_port_start_io(
	struct scic_sds_port *this_port,
	struct scic_sds_remote_device *the_device,
	struct scic_sds_request *the_io_request)
{
	return this_port->state_handlers->start_io_handler(
		       this_port, the_device, the_io_request);
}

/**
 * This method is called to complete an IO request to the port.
 * @this_port:
 * @the_device:
 * @the_io_request:
 *
 * enum sci_status
 */
enum sci_status scic_sds_port_complete_io(
	struct scic_sds_port *this_port,
	struct scic_sds_remote_device *the_device,
	struct scic_sds_request *the_io_request)
{
	return this_port->state_handlers->complete_io_handler(
		       this_port, the_device, the_io_request);
}

/**
 * This method is provided to timeout requests for port operations. Mostly its
 *    for the port reset operation.
 *
 *
 */
static void scic_sds_port_timeout_handler(void *port)
{
	struct scic_sds_port *this_port = port;
	u32 current_state;

	current_state = sci_base_state_machine_get_state(
		&this_port->parent.state_machine);

	if (current_state == SCI_BASE_PORT_STATE_RESETTING) {
		/*
		 * if the port is still in the resetting state then the timeout fired
		 * before the reset completed. */
		sci_base_state_machine_change_state(
			&this_port->parent.state_machine,
			SCI_BASE_PORT_STATE_FAILED
			);
	} else if (current_state == SCI_BASE_PORT_STATE_STOPPED) {
		/*
		 * if the port is stopped then the start request failed
		 * In this case stay in the stopped state. */
		dev_err(sciport_to_dev(this_port),
			"%s: SCIC Port 0x%p failed to stop before tiemout.\n",
			__func__,
			this_port);
	} else if (current_state == SCI_BASE_PORT_STATE_STOPPING) {
		/* if the port is still stopping then the stop has not completed */
		scic_cb_port_stop_complete(
			scic_sds_port_get_controller(this_port),
			port,
			SCI_FAILURE_TIMEOUT
			);
	} else {
		/*
		 * The port is in the ready state and we have a timer reporting a timeout
		 * this should not happen. */
		dev_err(sciport_to_dev(this_port),
			"%s: SCIC Port 0x%p is processing a timeout operation "
			"in state %d.\n",
			__func__,
			this_port,
			current_state);
	}
}

/* --------------------------------------------------------------------------- */

/**
 * This function updates the hardwares VIIT entry for this port.
 *
 *
 */
void scic_sds_port_update_viit_entry(struct scic_sds_port *this_port)
{
	struct sci_sas_address sas_address;

	scic_sds_port_get_sas_address(this_port, &sas_address);

	scu_port_viit_register_write(
		this_port, initiator_sas_address_hi, sas_address.high);

	scu_port_viit_register_write(
		this_port, initiator_sas_address_lo, sas_address.low);

	/* This value get cleared just in case its not already cleared */
	scu_port_viit_register_write(
		this_port, reserved, 0);

	/* We are required to update the status register last */
	scu_port_viit_register_write(
		this_port, status, (
			SCU_VIIT_ENTRY_ID_VIIT
			| SCU_VIIT_IPPT_INITIATOR
			| ((1 << this_port->physical_port_index) << SCU_VIIT_ENTRY_LPVIE_SHIFT)
			| SCU_VIIT_STATUS_ALL_VALID
			)
		);
}

/**
 * This method returns the maximum allowed speed for data transfers on this
 *    port.  This maximum allowed speed evaluates to the maximum speed of the
 *    slowest phy in the port.
 * @this_port: This parameter specifies the port for which to retrieve the
 *    maximum allowed speed.
 *
 * This method returns the maximum negotiated speed of the slowest phy in the
 * port.
 */
enum sci_sas_link_rate scic_sds_port_get_max_allowed_speed(
	struct scic_sds_port *this_port)
{
	u16 index             = 0;
	enum sci_sas_link_rate max_allowed_speed = SCI_SAS_600_GB;
	struct scic_sds_phy *phy               = NULL;

	/*
	 * Loop through all of the phys in this port and find the phy with the
	 * lowest maximum link rate. */
	for (index = 0; index < SCI_MAX_PHYS; index++) {
		phy = this_port->phy_table[index];
		if (
			(phy != NULL)
			&& (scic_sds_port_active_phy(this_port, phy) == true)
			&& (phy->max_negotiated_speed < max_allowed_speed)
			)
			max_allowed_speed = phy->max_negotiated_speed;
	}

	return max_allowed_speed;
}


/**
 * This method passes the event to core user.
 * @this_port: The port that a BCN happens.
 * @this_phy: The phy that receives BCN.
 *
 */
void scic_sds_port_broadcast_change_received(
	struct scic_sds_port *this_port,
	struct scic_sds_phy *this_phy)
{
	/* notify the user. */
	scic_cb_port_bc_change_primitive_received(
		this_port->owning_controller, this_port, this_phy
		);
}


/**
 * This API methhod enables the broadcast change notification from underneath
 *    hardware.
 * @this_port: The port that a BCN had been disabled from.
 *
 */
void scic_port_enable_broadcast_change_notification(
	struct scic_sds_port *port)
{
	struct scic_sds_phy *phy;
	u32 register_value;
	u8 index;

	/* Loop through all of the phys to enable BCN. */
	for (index = 0; index < SCI_MAX_PHYS; index++) {
		phy = port->phy_table[index];
		if (phy != NULL) {
			register_value = SCU_SAS_LLCTL_READ(phy);

			/* clear the bit by writing 1. */
			SCU_SAS_LLCTL_WRITE(phy, register_value);
		}
	}
}

/*
 * ****************************************************************************
 * *  READY SUBSTATE HANDLERS
 * **************************************************************************** */

/**
 *
 * @port: This is the struct sci_base_port object which is cast into a struct scic_sds_port
 *    object.
 *
 * This method is the general ready state stop handler for the struct scic_sds_port
 * object.  This function will transition the ready substate machine to its
 * final state. enum sci_status SCI_SUCCESS
 */
static enum sci_status scic_sds_port_ready_substate_stop_handler(
	struct sci_base_port *port)
{
	struct scic_sds_port *this_port = (struct scic_sds_port *)port;

	sci_base_state_machine_change_state(
		&this_port->parent.state_machine,
		SCI_BASE_PORT_STATE_STOPPING
		);

	return SCI_SUCCESS;
}

/**
 *
 * @port: This is the struct sci_base_port object which is cast into a struct scic_sds_port
 *    object.
 * @device: This is the struct sci_base_remote_device object which is not used in this
 *    function.
 * @io_request: This is the struct sci_base_request object which is not used in this
 *    function.
 *
 * This method is the general ready substate complete io handler for the
 * struct scic_sds_port object.  This function decrments the outstanding request count
 * for this port object. enum sci_status SCI_SUCCESS
 */
static enum sci_status scic_sds_port_ready_substate_complete_io_handler(
	struct scic_sds_port *port,
	struct scic_sds_remote_device *device,
	struct scic_sds_request *io_request)
{
	struct scic_sds_port *this_port = (struct scic_sds_port *)port;

	scic_sds_port_decrement_request_count(this_port);

	return SCI_SUCCESS;
}

static enum sci_status scic_sds_port_ready_substate_add_phy_handler(
	struct sci_base_port *port,
	struct sci_base_phy *phy)
{
	struct scic_sds_port *this_port = (struct scic_sds_port *)port;
	struct scic_sds_phy *this_phy  = (struct scic_sds_phy *)phy;
	enum sci_status status;

	status = scic_sds_port_set_phy(this_port, this_phy);

	if (status == SCI_SUCCESS) {
		scic_sds_port_general_link_up_handler(this_port, this_phy, true);

		this_port->not_ready_reason = SCIC_PORT_NOT_READY_RECONFIGURING;

		sci_base_state_machine_change_state(
			&this_port->ready_substate_machine,
			SCIC_SDS_PORT_READY_SUBSTATE_CONFIGURING
			);
	}

	return status;
}


static enum sci_status scic_sds_port_ready_substate_remove_phy_handler(
	struct sci_base_port *port,
	struct sci_base_phy *phy)
{
	struct scic_sds_port *this_port = (struct scic_sds_port *)port;
	struct scic_sds_phy *this_phy  = (struct scic_sds_phy *)phy;
	enum sci_status status;

	status = scic_sds_port_clear_phy(this_port, this_phy);

	if (status == SCI_SUCCESS) {
		scic_sds_port_deactivate_phy(this_port, this_phy, true);

		this_port->not_ready_reason = SCIC_PORT_NOT_READY_RECONFIGURING;

		sci_base_state_machine_change_state(
			&this_port->ready_substate_machine,
			SCIC_SDS_PORT_READY_SUBSTATE_CONFIGURING
			);
	}

	return status;
}

/*
 * ****************************************************************************
 * *  READY SUBSTATE WAITING HANDLERS
 * **************************************************************************** */

/**
 *
 * @this_port: This is the struct scic_sds_port object that which has a phy that has
 *    gone link up.
 * @the_phy: This is the struct scic_sds_phy object that has gone link up.
 *
 * This method is the ready waiting substate link up handler for the
 * struct scic_sds_port object.  This methos will report the link up condition for
 * this port and will transition to the ready operational substate. none
 */
static void scic_sds_port_ready_waiting_substate_link_up_handler(
	struct scic_sds_port *this_port,
	struct scic_sds_phy *the_phy)
{
	/*
	 * Since this is the first phy going link up for the port we can just enable
	 * it and continue. */
	scic_sds_port_activate_phy(this_port, the_phy, true);

	sci_base_state_machine_change_state(
		&this_port->ready_substate_machine,
		SCIC_SDS_PORT_READY_SUBSTATE_OPERATIONAL
		);
}

/**
 *
 * @port: This is the struct sci_base_port object which is cast into a struct scic_sds_port
 *    object.
 * @device: This is the struct sci_base_remote_device object which is not used in this
 *    request.
 * @io_request: This is the struct sci_base_request object which is not used in this
 *    function.
 *
 * This method is the ready waiting substate start io handler for the
 * struct scic_sds_port object. The port object can not accept new requests so the
 * request is failed. enum sci_status SCI_FAILURE_INVALID_STATE
 */
static enum sci_status scic_sds_port_ready_waiting_substate_start_io_handler(
	struct scic_sds_port *port,
	struct scic_sds_remote_device *device,
	struct scic_sds_request *io_request)
{
	return SCI_FAILURE_INVALID_STATE;
}

/*
 * ****************************************************************************
 * *  READY SUBSTATE OPERATIONAL HANDLERS
 * **************************************************************************** */

/**
 *
 * @port: This is the struct sci_base_port object which is cast into a struct scic_sds_port
 *    object.
 * @timeout: This is the timeout for the reset request to complete.
 *
 * This method will casue the port to reset. enum sci_status SCI_SUCCESS
 */
static enum sci_status scic_sds_port_ready_operational_substate_reset_handler(
	struct sci_base_port *port,
	u32 timeout)
{
	enum sci_status status = SCI_FAILURE_INVALID_PHY;
	u32 phy_index;
	struct scic_sds_port *this_port = (struct scic_sds_port *)port;
	struct scic_sds_phy *selected_phy = NULL;


	/* Select a phy on which we can send the hard reset request. */
	for (
		phy_index = 0;
		(phy_index < SCI_MAX_PHYS)
		&& (selected_phy == NULL);
		phy_index++
		) {
		selected_phy = this_port->phy_table[phy_index];

		if (
			(selected_phy != NULL)
			&& !scic_sds_port_active_phy(this_port, selected_phy)
			) {
			/* We found a phy but it is not ready select different phy */
			selected_phy = NULL;
		}
	}

	/* If we have a phy then go ahead and start the reset procedure */
	if (selected_phy != NULL) {
		status = scic_sds_phy_reset(selected_phy);

		if (status == SCI_SUCCESS) {
			scic_cb_timer_start(
				scic_sds_port_get_controller(this_port),
				this_port->timer_handle,
				timeout
				);

			this_port->not_ready_reason = SCIC_PORT_NOT_READY_HARD_RESET_REQUESTED;

			sci_base_state_machine_change_state(
				&this_port->parent.state_machine,
				SCI_BASE_PORT_STATE_RESETTING
				);
		}
	}

	return status;
}

/**
 * scic_sds_port_ready_operational_substate_link_up_handler() -
 * @this_port: This is the struct scic_sds_port object that which has a phy that has
 *    gone link up.
 * @the_phy: This is the struct scic_sds_phy object that has gone link up.
 *
 * This method is the ready operational substate link up handler for the
 * struct scic_sds_port object. This function notifies the SCI User that the phy has
 * gone link up. none
 */
static void scic_sds_port_ready_operational_substate_link_up_handler(
	struct scic_sds_port *this_port,
	struct scic_sds_phy *the_phy)
{
	scic_sds_port_general_link_up_handler(this_port, the_phy, true);
}

/**
 * scic_sds_port_ready_operational_substate_link_down_handler() -
 * @this_port: This is the struct scic_sds_port object that which has a phy that has
 *    gone link down.
 * @the_phy: This is the struct scic_sds_phy object that has gone link down.
 *
 * This method is the ready operational substate link down handler for the
 * struct scic_sds_port object. This function notifies the SCI User that the phy has
 * gone link down and if this is the last phy in the port the port will change
 * state to the ready waiting substate. none
 */
static void scic_sds_port_ready_operational_substate_link_down_handler(
	struct scic_sds_port *this_port,
	struct scic_sds_phy *the_phy)
{
	scic_sds_port_deactivate_phy(this_port, the_phy, true);

	/*
	 * If there are no active phys left in the port, then transition
	 * the port to the WAITING state until such time as a phy goes
	 * link up. */
	if (this_port->active_phy_mask == 0) {
		sci_base_state_machine_change_state(
			scic_sds_port_get_ready_substate_machine(this_port),
			SCIC_SDS_PORT_READY_SUBSTATE_WAITING
			);
	}
}

/**
 *
 * @port: This is the struct sci_base_port object which is cast into a struct scic_sds_port
 *    object.
 * @device: This is the struct sci_base_remote_device object which is not used in this
 *    function.
 * @io_request: This is the struct sci_base_request object which is not used in this
 *    function.
 *
 * This method is the ready operational substate start io handler for the
 * struct scic_sds_port object.  This function incremetns the outstanding request
 * count for this port object. enum sci_status SCI_SUCCESS
 */
static enum sci_status scic_sds_port_ready_operational_substate_start_io_handler(
	struct scic_sds_port *port,
	struct scic_sds_remote_device *device,
	struct scic_sds_request *io_request)
{
	struct scic_sds_port *this_port = (struct scic_sds_port *)port;

	scic_sds_port_increment_request_count(this_port);

	return SCI_SUCCESS;
}

/*
 * ****************************************************************************
 * *  READY SUBSTATE OPERATIONAL HANDLERS
 * **************************************************************************** */

/**
 * scic_sds_port_ready_configuring_substate_add_phy_handler() -
 * @port: This is the struct sci_base_port object which is cast into a struct scic_sds_port
 *    object.
 *
 * This is the default method for a port add phy request.  It will report a
 * warning and exit. enum sci_status SCI_FAILURE_INVALID_STATE
 */
static enum sci_status scic_sds_port_ready_configuring_substate_add_phy_handler(
	struct sci_base_port *port,
	struct sci_base_phy *phy)
{
	struct scic_sds_port *this_port = (struct scic_sds_port *)port;
	struct scic_sds_phy *this_phy  = (struct scic_sds_phy *)phy;
	enum sci_status status;

	status = scic_sds_port_set_phy(this_port, this_phy);

	if (status == SCI_SUCCESS) {
		scic_sds_port_general_link_up_handler(this_port, this_phy, true);

		/*
		 * Re-enter the configuring state since this may be the last phy in
		 * the port. */
		sci_base_state_machine_change_state(
			&this_port->ready_substate_machine,
			SCIC_SDS_PORT_READY_SUBSTATE_CONFIGURING
			);
	}

	return status;
}

/**
 * scic_sds_port_ready_configuring_substate_remove_phy_handler() -
 * @port: This is the struct sci_base_port object which is cast into a struct scic_sds_port
 *    object.
 *
 * This is the default method for a port remove phy request.  It will report a
 * warning and exit. enum sci_status SCI_FAILURE_INVALID_STATE
 */
static enum sci_status scic_sds_port_ready_configuring_substate_remove_phy_handler(
	struct sci_base_port *port,
	struct sci_base_phy *phy)
{
	struct scic_sds_port *this_port = (struct scic_sds_port *)port;
	struct scic_sds_phy *this_phy  = (struct scic_sds_phy *)phy;
	enum sci_status status;

	status = scic_sds_port_clear_phy(this_port, this_phy);

	if (status == SCI_SUCCESS) {
		scic_sds_port_deactivate_phy(this_port, this_phy, true);

		/*
		 * Re-enter the configuring state since this may be the last phy in
		 * the port. */
		sci_base_state_machine_change_state(
			&this_port->ready_substate_machine,
			SCIC_SDS_PORT_READY_SUBSTATE_CONFIGURING
			);
	}

	return status;
}

/**
 * scic_sds_port_ready_configuring_substate_complete_io_handler() -
 * @port: This is the port that is being requested to complete the io request.
 * @device: This is the device on which the io is completing.
 *
 * This method will decrement the outstanding request count for this port. If
 * the request count goes to 0 then the port can be reprogrammed with its new
 * phy data.
 */
static enum sci_status scic_sds_port_ready_configuring_substate_complete_io_handler(
	struct scic_sds_port *port,
	struct scic_sds_remote_device *device,
	struct scic_sds_request *io_request)
{
	scic_sds_port_decrement_request_count(port);

	if (port->started_request_count == 0) {
		sci_base_state_machine_change_state(
			&port->ready_substate_machine,
			SCIC_SDS_PORT_READY_SUBSTATE_OPERATIONAL
			);
	}

	return SCI_SUCCESS;
}

/* --------------------------------------------------------------------------- */

struct scic_sds_port_state_handler
scic_sds_port_ready_substate_handler_table[SCIC_SDS_PORT_READY_MAX_SUBSTATES] =
{
	/* SCIC_SDS_PORT_READY_SUBSTATE_WAITING */
	{
		{
			scic_sds_port_default_start_handler,
			scic_sds_port_ready_substate_stop_handler,
			scic_sds_port_default_destruct_handler,
			scic_sds_port_default_reset_handler,
			scic_sds_port_ready_substate_add_phy_handler,
			scic_sds_port_default_remove_phy_handler
		},
		scic_sds_port_default_frame_handler,
		scic_sds_port_default_event_handler,
		scic_sds_port_ready_waiting_substate_link_up_handler,
		scic_sds_port_default_link_down_handler,
		scic_sds_port_ready_waiting_substate_start_io_handler,
		scic_sds_port_ready_substate_complete_io_handler,
	},
	/* SCIC_SDS_PORT_READY_SUBSTATE_OPERATIONAL */
	{
		{
			scic_sds_port_default_start_handler,
			scic_sds_port_ready_substate_stop_handler,
			scic_sds_port_default_destruct_handler,
			scic_sds_port_ready_operational_substate_reset_handler,
			scic_sds_port_ready_substate_add_phy_handler,
			scic_sds_port_ready_substate_remove_phy_handler
		},
		scic_sds_port_default_frame_handler,
		scic_sds_port_default_event_handler,
		scic_sds_port_ready_operational_substate_link_up_handler,
		scic_sds_port_ready_operational_substate_link_down_handler,
		scic_sds_port_ready_operational_substate_start_io_handler,
		scic_sds_port_ready_substate_complete_io_handler
	},
	/* SCIC_SDS_PORT_READY_SUBSTATE_CONFIGURING */
	{
		{
			scic_sds_port_default_start_handler,
			scic_sds_port_ready_substate_stop_handler,
			scic_sds_port_default_destruct_handler,
			scic_sds_port_default_reset_handler,
			scic_sds_port_ready_configuring_substate_add_phy_handler,
			scic_sds_port_ready_configuring_substate_remove_phy_handler
		},
		scic_sds_port_default_frame_handler,
		scic_sds_port_default_event_handler,
		scic_sds_port_default_link_up_handler,
		scic_sds_port_default_link_down_handler,
		scic_sds_port_default_start_io_handler,
		scic_sds_port_ready_configuring_substate_complete_io_handler
	}
};


/**
 * scic_sds_port_set_ready_state_handlers() -
 *
 * This macro sets the port ready substate handlers.
 */
#define scic_sds_port_set_ready_state_handlers(port, state_id) \
	scic_sds_port_set_state_handlers(\
		port, &scic_sds_port_ready_substate_handler_table[(state_id)] \
		)

/*
 * ******************************************************************************
 * *  PORT STATE PRIVATE METHODS
 * ****************************************************************************** */

/**
 *
 * @this_port: This is the struct scic_sds_port object to suspend.
 *
 * This method will susped the port task scheduler for this port object. none
 */
static void scic_sds_port_suspend_port_task_scheduler(
	struct scic_sds_port *this_port)
{
	u32 pts_control_value;
	u32 tl_control_value;

	pts_control_value = scu_port_task_scheduler_read(this_port, control);
	tl_control_value = scu_transport_layer_read(this_port, control);

	pts_control_value |= SCU_PTSxCR_GEN_BIT(SUSPEND);
	tl_control_value  |= SCU_TLCR_GEN_BIT(CLEAR_TCI_NCQ_MAPPING_TABLE);

	scu_port_task_scheduler_write(this_port, control, pts_control_value);
	scu_transport_layer_write(this_port, control, tl_control_value);
}

/**
 *
 * @this_port: This is the struct scic_sds_port object to resume.
 *
 * This method will resume the port task scheduler for this port object. none
 */
static void scic_sds_port_resume_port_task_scheduler(
	struct scic_sds_port *this_port)
{
	u32 pts_control_value;

	pts_control_value = scu_port_task_scheduler_read(this_port, control);

	pts_control_value &= ~SCU_PTSxCR_GEN_BIT(SUSPEND);

	scu_port_task_scheduler_write(this_port, control, pts_control_value);
}

/*
 * ******************************************************************************
 * *  PORT READY SUBSTATE METHODS
 * ****************************************************************************** */

/**
 *
 * @object: This is the struct sci_base_object which is cast to a struct scic_sds_port object.
 *
 * This method will perform the actions required by the struct scic_sds_port on
 * entering the SCIC_SDS_PORT_READY_SUBSTATE_WAITING. This function checks the
 * port for any ready phys.  If there is at least one phy in a ready state then
 * the port transitions to the ready operational substate. none
 */
static void scic_sds_port_ready_substate_waiting_enter(
	struct sci_base_object *object)
{
	struct scic_sds_port *this_port = (struct scic_sds_port *)object;

	scic_sds_port_set_ready_state_handlers(
		this_port, SCIC_SDS_PORT_READY_SUBSTATE_WAITING
		);

	scic_sds_port_suspend_port_task_scheduler(this_port);

	this_port->not_ready_reason = SCIC_PORT_NOT_READY_NO_ACTIVE_PHYS;

	if (this_port->active_phy_mask != 0) {
		/* At least one of the phys on the port is ready */
		sci_base_state_machine_change_state(
			&this_port->ready_substate_machine,
			SCIC_SDS_PORT_READY_SUBSTATE_OPERATIONAL
			);
	}
}

/**
 *
 * @object: This is the struct sci_base_object which is cast to a struct scic_sds_port object.
 *
 * This method will perform the actions required by the struct scic_sds_port on
 * entering the SCIC_SDS_PORT_READY_SUBSTATE_OPERATIONAL. This function sets
 * the state handlers for the port object, notifies the SCI User that the port
 * is ready, and resumes port operations. none
 */
static void scic_sds_port_ready_substate_operational_enter(
	struct sci_base_object *object)
{
	u32 index;
	struct scic_sds_port *this_port = (struct scic_sds_port *)object;

	scic_sds_port_set_ready_state_handlers(
		this_port, SCIC_SDS_PORT_READY_SUBSTATE_OPERATIONAL
		);

	scic_cb_port_ready(
		scic_sds_port_get_controller(this_port), this_port
		);

	for (index = 0; index < SCI_MAX_PHYS; index++) {
		if (this_port->phy_table[index] != NULL) {
			scic_sds_port_write_phy_assignment(
				this_port, this_port->phy_table[index]
				);
		}
	}

	scic_sds_port_update_viit_entry(this_port);

	scic_sds_port_resume_port_task_scheduler(this_port);
}

/**
 *
 * @object: This is the struct sci_base_object which is cast to a struct scic_sds_port object.
 *
 * This method will perform the actions required by the struct scic_sds_port on
 * exiting the SCIC_SDS_PORT_READY_SUBSTATE_OPERATIONAL. This function reports
 * the port not ready and suspends the port task scheduler. none
 */
static void scic_sds_port_ready_substate_operational_exit(
	struct sci_base_object *object)
{
	struct scic_sds_port *this_port = (struct scic_sds_port *)object;

	scic_cb_port_not_ready(
		scic_sds_port_get_controller(this_port),
		this_port,
		this_port->not_ready_reason
		);
}

/*
 * ******************************************************************************
 * *  PORT READY CONFIGURING METHODS
 * ****************************************************************************** */

/**
 * scic_sds_port_ready_substate_configuring_enter() -
 * @object: This is the struct sci_base_object which is cast to a struct scic_sds_port object.
 *
 * This method will perform the actions required by the struct scic_sds_port on
 * exiting the SCIC_SDS_PORT_READY_SUBSTATE_OPERATIONAL. This function reports
 * the port not ready and suspends the port task scheduler. none
 */
static void scic_sds_port_ready_substate_configuring_enter(
	struct sci_base_object *object)
{
	struct scic_sds_port *this_port = (struct scic_sds_port *)object;

	scic_sds_port_set_ready_state_handlers(
		this_port, SCIC_SDS_PORT_READY_SUBSTATE_CONFIGURING
		);

	if (this_port->active_phy_mask == 0) {
		scic_cb_port_not_ready(
			scic_sds_port_get_controller(this_port),
			this_port,
			SCIC_PORT_NOT_READY_NO_ACTIVE_PHYS
			);

		sci_base_state_machine_change_state(
			&this_port->ready_substate_machine,
			SCIC_SDS_PORT_READY_SUBSTATE_WAITING
			);
	} else if (this_port->started_request_count == 0) {
		sci_base_state_machine_change_state(
			&this_port->ready_substate_machine,
			SCIC_SDS_PORT_READY_SUBSTATE_OPERATIONAL
			);
	}
}

static void scic_sds_port_ready_substate_configuring_exit(
	struct sci_base_object *object)
{
	struct scic_sds_port *this_port = (struct scic_sds_port *)object;

	scic_sds_port_suspend_port_task_scheduler(this_port);
}

/* --------------------------------------------------------------------------- */

const struct sci_base_state scic_sds_port_ready_substate_table[] = {
	[SCIC_SDS_PORT_READY_SUBSTATE_WAITING] = {
		.enter_state = scic_sds_port_ready_substate_waiting_enter,
	},
	[SCIC_SDS_PORT_READY_SUBSTATE_OPERATIONAL] = {
		.enter_state = scic_sds_port_ready_substate_operational_enter,
		.exit_state  = scic_sds_port_ready_substate_operational_exit
	},
	[SCIC_SDS_PORT_READY_SUBSTATE_CONFIGURING] = {
		.enter_state = scic_sds_port_ready_substate_configuring_enter,
		.exit_state  = scic_sds_port_ready_substate_configuring_exit
	},
};

/*
 * ***************************************************************************
 * *  DEFAULT HANDLERS
 * *************************************************************************** */

/**
 *
 * @port: This is the struct sci_base_port object which is cast into a struct scic_sds_port
 *    object.
 *
 * This is the default method for port a start request.  It will report a
 * warning and exit. enum sci_status SCI_FAILURE_INVALID_STATE
 */
enum sci_status scic_sds_port_default_start_handler(
	struct sci_base_port *port)
{
	struct scic_sds_port *sci_port = (struct scic_sds_port *)port;

	dev_warn(sciport_to_dev(sci_port),
		 "%s: SCIC Port 0x%p requested to start while in invalid "
		 "state %d\n",
		 __func__,
		 port,
		 sci_base_state_machine_get_state(
			 scic_sds_port_get_base_state_machine(
				 (struct scic_sds_port *)port)));

	return SCI_FAILURE_INVALID_STATE;
}

/**
 *
 * @port: This is the struct sci_base_port object which is cast into a struct scic_sds_port
 *    object.
 *
 * This is the default method for a port stop request.  It will report a
 * warning and exit. enum sci_status SCI_FAILURE_INVALID_STATE
 */
static enum sci_status scic_sds_port_default_stop_handler(
	struct sci_base_port *port)
{
	struct scic_sds_port *sci_port = (struct scic_sds_port *)port;

	dev_warn(sciport_to_dev(sci_port),
		 "%s: SCIC Port 0x%p requested to stop while in invalid "
		 "state %d\n",
		 __func__,
		 port,
		 sci_base_state_machine_get_state(
			 scic_sds_port_get_base_state_machine(
				 (struct scic_sds_port *)port)));

	return SCI_FAILURE_INVALID_STATE;
}

/**
 *
 * @port: This is the struct sci_base_port object which is cast into a struct scic_sds_port
 *    object.
 *
 * This is the default method for a port destruct request.  It will report a
 * warning and exit. enum sci_status SCI_FAILURE_INVALID_STATE
 */
enum sci_status scic_sds_port_default_destruct_handler(
	struct sci_base_port *port)
{
	struct scic_sds_port *sci_port = (struct scic_sds_port *)port;

	dev_warn(sciport_to_dev(sci_port),
		 "%s: SCIC Port 0x%p requested to destruct while in invalid "
		 "state %d\n",
		 __func__,
		 port,
		 sci_base_state_machine_get_state(
			 scic_sds_port_get_base_state_machine(
				 (struct scic_sds_port *)port)));

	return SCI_FAILURE_INVALID_STATE;
}

/**
 *
 * @port: This is the struct sci_base_port object which is cast into a struct scic_sds_port
 *    object.
 * @timeout: This is the timeout for the reset request to complete.
 *
 * This is the default method for a port reset request.  It will report a
 * warning and exit. enum sci_status SCI_FAILURE_INVALID_STATE
 */
enum sci_status scic_sds_port_default_reset_handler(
	struct sci_base_port *port,
	u32 timeout)
{
	struct scic_sds_port *sci_port = (struct scic_sds_port *)port;

	dev_warn(sciport_to_dev(sci_port),
		 "%s: SCIC Port 0x%p requested to reset while in invalid "
		 "state %d\n",
		 __func__,
		 port,
		 sci_base_state_machine_get_state(
			 scic_sds_port_get_base_state_machine(
				 (struct scic_sds_port *)port)));

	return SCI_FAILURE_INVALID_STATE;
}

/**
 *
 * @port: This is the struct sci_base_port object which is cast into a struct scic_sds_port
 *    object.
 *
 * This is the default method for a port add phy request.  It will report a
 * warning and exit. enum sci_status SCI_FAILURE_INVALID_STATE
 */
static enum sci_status scic_sds_port_default_add_phy_handler(
	struct sci_base_port *port,
	struct sci_base_phy *phy)
{
	struct scic_sds_port *sci_port = (struct scic_sds_port *)port;

	dev_warn(sciport_to_dev(sci_port),
		 "%s: SCIC Port 0x%p requested to add phy 0x%p while in "
		 "invalid state %d\n",
		 __func__,
		 port,
		 phy,
		 sci_base_state_machine_get_state(
			 scic_sds_port_get_base_state_machine(
				 (struct scic_sds_port *)port)));

	return SCI_FAILURE_INVALID_STATE;
}

/**
 *
 * @port: This is the struct sci_base_port object which is cast into a struct scic_sds_port
 *    object.
 *
 * This is the default method for a port remove phy request.  It will report a
 * warning and exit. enum sci_status SCI_FAILURE_INVALID_STATE
 */
enum sci_status scic_sds_port_default_remove_phy_handler(
	struct sci_base_port *port,
	struct sci_base_phy *phy)
{
	struct scic_sds_port *sci_port = (struct scic_sds_port *)port;

	dev_warn(sciport_to_dev(sci_port),
		 "%s: SCIC Port 0x%p requested to remove phy 0x%p while in "
		 "invalid state %d\n",
		 __func__,
		 port,
		 phy,
		 sci_base_state_machine_get_state(
			 scic_sds_port_get_base_state_machine(
				 (struct scic_sds_port *)port)));

	return SCI_FAILURE_INVALID_STATE;
}

/**
 *
 * @port: This is the struct sci_base_port object which is cast into a struct scic_sds_port
 *    object.
 *
 * This is the default method for a port unsolicited frame request.  It will
 * report a warning and exit. enum sci_status SCI_FAILURE_INVALID_STATE Is it even
 * possible to receive an unsolicited frame directed to a port object?  It
 * seems possible if we implementing virtual functions but until then?
 */
enum sci_status scic_sds_port_default_frame_handler(
	struct scic_sds_port *port,
	u32 frame_index)
{
	dev_warn(sciport_to_dev(port),
		 "%s: SCIC Port 0x%p requested to process frame %d while in "
		 "invalid state %d\n",
		 __func__,
		 port,
		 frame_index,
		 sci_base_state_machine_get_state(
			 scic_sds_port_get_base_state_machine(port)));

	scic_sds_controller_release_frame(
		scic_sds_port_get_controller(port), frame_index
		);

	return SCI_FAILURE_INVALID_STATE;
}

/**
 *
 * @port: This is the struct sci_base_port object which is cast into a struct scic_sds_port
 *    object.
 *
 * This is the default method for a port event request.  It will report a
 * warning and exit. enum sci_status SCI_FAILURE_INVALID_STATE
 */
enum sci_status scic_sds_port_default_event_handler(
	struct scic_sds_port *port,
	u32 event_code)
{
	dev_warn(sciport_to_dev(port),
		 "%s: SCIC Port 0x%p requested to process event 0x%x while "
		 "in invalid state %d\n",
		 __func__,
		 port,
		 event_code,
		 sci_base_state_machine_get_state(
			 scic_sds_port_get_base_state_machine(
				 (struct scic_sds_port *)port)));

	return SCI_FAILURE_INVALID_STATE;
}

/**
 *
 * @port: This is the struct sci_base_port object which is cast into a struct scic_sds_port
 *    object.
 *
 * This is the default method for a port link up notification.  It will report
 * a warning and exit. enum sci_status SCI_FAILURE_INVALID_STATE
 */
void scic_sds_port_default_link_up_handler(
	struct scic_sds_port *this_port,
	struct scic_sds_phy *phy)
{
	dev_warn(sciport_to_dev(this_port),
		 "%s: SCIC Port 0x%p received link_up notification from phy "
		 "0x%p while in invalid state %d\n",
		 __func__,
		 this_port,
		 phy,
		 sci_base_state_machine_get_state(
			 scic_sds_port_get_base_state_machine(this_port)));
}

/**
 *
 * @port: This is the struct sci_base_port object which is cast into a struct scic_sds_port
 *    object.
 *
 * This is the default method for a port link down notification.  It will
 * report a warning and exit. enum sci_status SCI_FAILURE_INVALID_STATE
 */
void scic_sds_port_default_link_down_handler(
	struct scic_sds_port *this_port,
	struct scic_sds_phy *phy)
{
	dev_warn(sciport_to_dev(this_port),
		 "%s: SCIC Port 0x%p received link down notification from "
		 "phy 0x%p while in invalid state %d\n",
		 __func__,
		 this_port,
		 phy,
		 sci_base_state_machine_get_state(
			 scic_sds_port_get_base_state_machine(this_port)));
}

/**
 *
 * @port: This is the struct sci_base_port object which is cast into a struct scic_sds_port
 *    object.
 *
 * This is the default method for a port start io request.  It will report a
 * warning and exit. enum sci_status SCI_FAILURE_INVALID_STATE
 */
enum sci_status scic_sds_port_default_start_io_handler(
	struct scic_sds_port *this_port,
	struct scic_sds_remote_device *device,
	struct scic_sds_request *io_request)
{
	dev_warn(sciport_to_dev(this_port),
		 "%s: SCIC Port 0x%p requested to start io request 0x%p "
		 "while in invalid state %d\n",
		 __func__,
		 this_port,
		 io_request,
		 sci_base_state_machine_get_state(
			 scic_sds_port_get_base_state_machine(this_port)));

	return SCI_FAILURE_INVALID_STATE;
}

/**
 *
 * @port: This is the struct sci_base_port object which is cast into a struct scic_sds_port
 *    object.
 *
 * This is the default method for a port complete io request.  It will report a
 * warning and exit. enum sci_status SCI_FAILURE_INVALID_STATE
 */
static enum sci_status scic_sds_port_default_complete_io_handler(
	struct scic_sds_port *this_port,
	struct scic_sds_remote_device *device,
	struct scic_sds_request *io_request)
{
	dev_warn(sciport_to_dev(this_port),
		 "%s: SCIC Port 0x%p requested to complete io request 0x%p "
		 "while in invalid state %d\n",
		 __func__,
		 this_port,
		 io_request,
		 sci_base_state_machine_get_state(
			 scic_sds_port_get_base_state_machine(this_port)));

	return SCI_FAILURE_INVALID_STATE;
}

/*
 * ****************************************************************************
 * * GENERAL STATE HANDLERS
 * **************************************************************************** */

/**
 *
 * @port: This is the struct scic_sds_port object on which the io request count will
 *    be decremented.
 * @device: This is the struct scic_sds_remote_device object to which the io request
 *    is being directed.  This parameter is not required to complete this
 *    operation.
 * @io_request: This is the request that is being completed on this port
 *    object.  This parameter is not required to complete this operation.
 *
 * This is a general complete io request handler for the struct scic_sds_port object.
 * enum sci_status SCI_SUCCESS
 */
static enum sci_status scic_sds_port_general_complete_io_handler(
	struct scic_sds_port *port,
	struct scic_sds_remote_device *device,
	struct scic_sds_request *io_request)
{
	struct scic_sds_port *this_port = (struct scic_sds_port *)port;

	scic_sds_port_decrement_request_count(this_port);

	return SCI_SUCCESS;
}

/*
 * ****************************************************************************
 * * STOPPED STATE HANDLERS
 * **************************************************************************** */

/**
 *
 * @port: This is the struct sci_base_port object which is cast into a struct scic_sds_port
 *    object.
 *
 * This method takes the struct scic_sds_port from a stopped state and attempts to
 * start it.  To start a port it must have no assiged devices and it must have
 * at least one phy assigned to it.  If those conditions are met then the port
 * can transition to the ready state. enum sci_status
 * SCI_FAILURE_UNSUPPORTED_PORT_CONFIGURATION This struct scic_sds_port object could
 * not be started because the port configuration is not valid. SCI_SUCCESS the
 * start request is successful and the struct scic_sds_port object has transitioned to
 * the SCI_BASE_PORT_STATE_READY.
 */
static enum sci_status scic_sds_port_stopped_state_start_handler(
	struct sci_base_port *port)
{
	u32 phy_mask;
	struct scic_sds_port *this_port = (struct scic_sds_port *)port;

	if (this_port->assigned_device_count > 0) {
		/*
		 * / @todo This is a start failure operation because there are still
		 * /       devices assigned to this port.  There must be no devices
		 * /       assigned to a port on a start operation. */
		return SCI_FAILURE_UNSUPPORTED_PORT_CONFIGURATION;
	}

	phy_mask = scic_sds_port_get_phys(this_port);

	/*
	 * There are one or more phys assigned to this port.  Make sure
	 * the port's phy mask is in fact legal and supported by the
	 * silicon. */
	if (scic_sds_port_is_phy_mask_valid(this_port, phy_mask) == true) {
		sci_base_state_machine_change_state(
			scic_sds_port_get_base_state_machine(this_port),
			SCI_BASE_PORT_STATE_READY
			);

		return SCI_SUCCESS;
	}

	return SCI_FAILURE_UNSUPPORTED_PORT_CONFIGURATION;
}

/**
 *
 * @port: This is the struct sci_base_port object which is cast into a struct scic_sds_port
 *    object.
 *
 * This method takes the struct scic_sds_port that is in a stopped state and handles a
 * stop request.  This function takes no action. enum sci_status SCI_SUCCESS the
 * stop request is successful as the struct scic_sds_port object is already stopped.
 */
static enum sci_status scic_sds_port_stopped_state_stop_handler(
	struct sci_base_port *port)
{
	/* We are already stopped so there is nothing to do here */
	return SCI_SUCCESS;
}

/**
 *
 * @port: This is the struct sci_base_port object which is cast into a struct scic_sds_port
 *    object.
 *
 * This method takes the struct scic_sds_port that is in a stopped state and handles
 * the destruct request.  The stopped state is the only state in which the
 * struct scic_sds_port can be destroyed.  This function causes the port object to
 * transition to the SCI_BASE_PORT_STATE_FINAL. enum sci_status SCI_SUCCESS
 */
static enum sci_status scic_sds_port_stopped_state_destruct_handler(
	struct sci_base_port *port)
{
	struct scic_sds_port *this_port = (struct scic_sds_port *)port;

	sci_base_state_machine_stop(&this_port->parent.state_machine);

	return SCI_SUCCESS;
}

/**
 *
 * @port: This is the struct sci_base_port object which is cast into a struct scic_sds_port
 *    object.
 * @phy: This is the struct sci_base_phy object which is cast into a struct scic_sds_phy
 *    object.
 *
 * This method takes the struct scic_sds_port that is in a stopped state and handles
 * the add phy request.  In MPC mode the only time a phy can be added to a port
 * is in the SCI_BASE_PORT_STATE_STOPPED. enum sci_status
 * SCI_FAILURE_UNSUPPORTED_PORT_CONFIGURATION is returned when the phy can not
 * be added to the port. SCI_SUCCESS if the phy is added to the port.
 */
static enum sci_status scic_sds_port_stopped_state_add_phy_handler(
	struct sci_base_port *port,
	struct sci_base_phy *phy)
{
	struct scic_sds_port *this_port = (struct scic_sds_port *)port;
	struct scic_sds_phy *this_phy  = (struct scic_sds_phy *)phy;
	struct sci_sas_address port_sas_address;

	/* Read the port assigned SAS Address if there is one */
	scic_sds_port_get_sas_address(this_port, &port_sas_address);

	if (port_sas_address.high != 0 && port_sas_address.low != 0) {
		struct sci_sas_address phy_sas_address;

		/*
		 * Make sure that the PHY SAS Address matches the SAS Address
		 * for this port. */
		scic_sds_phy_get_sas_address(this_phy, &phy_sas_address);

		if (
			(port_sas_address.high != phy_sas_address.high)
			|| (port_sas_address.low  != phy_sas_address.low)
			) {
			return SCI_FAILURE_UNSUPPORTED_PORT_CONFIGURATION;
		}
	}

	return scic_sds_port_set_phy(this_port, this_phy);
}


/**
 *
 * @port: This is the struct sci_base_port object which is cast into a struct scic_sds_port
 *    object.
 * @phy: This is the struct sci_base_phy object which is cast into a struct scic_sds_phy
 *    object.
 *
 * This method takes the struct scic_sds_port that is in a stopped state and handles
 * the remove phy request.  In MPC mode the only time a phy can be removed from
 * a port is in the SCI_BASE_PORT_STATE_STOPPED. enum sci_status
 * SCI_FAILURE_UNSUPPORTED_PORT_CONFIGURATION is returned when the phy can not
 * be added to the port. SCI_SUCCESS if the phy is added to the port.
 */
static enum sci_status scic_sds_port_stopped_state_remove_phy_handler(
	struct sci_base_port *port,
	struct sci_base_phy *phy)
{
	struct scic_sds_port *this_port = (struct scic_sds_port *)port;
	struct scic_sds_phy *this_phy  = (struct scic_sds_phy *)phy;

	return scic_sds_port_clear_phy(this_port, this_phy);
}

/*
 * ****************************************************************************
 * *  READY STATE HANDLERS
 * **************************************************************************** */

/*
 * ****************************************************************************
 * *  RESETTING STATE HANDLERS
 * **************************************************************************** */

/*
 * ****************************************************************************
 * *  STOPPING STATE HANDLERS
 * **************************************************************************** */

/**
 *
 * @port: This is the struct scic_sds_port object on which the io request count will
 *    be decremented.
 * @device: This is the struct scic_sds_remote_device object to which the io request
 *    is being directed.  This parameter is not required to complete this
 *    operation.
 * @io_request: This is the request that is being completed on this port
 *    object.  This parameter is not required to complete this operation.
 *
 * This method takes the struct scic_sds_port that is in a stopping state and handles
 * the complete io request. Should the request count reach 0 then the port
 * object will transition to the stopped state. enum sci_status SCI_SUCCESS
 */
static enum sci_status scic_sds_port_stopping_state_complete_io_handler(
	struct scic_sds_port *port,
	struct scic_sds_remote_device *device,
	struct scic_sds_request *io_request)
{
	struct scic_sds_port *this_port = (struct scic_sds_port *)port;

	scic_sds_port_decrement_request_count(this_port);

	if (this_port->started_request_count == 0) {
		sci_base_state_machine_change_state(
			scic_sds_port_get_base_state_machine(this_port),
			SCI_BASE_PORT_STATE_STOPPED
			);
	}

	return SCI_SUCCESS;
}

/*
 * ****************************************************************************
 * *  RESETTING STATE HANDLERS
 * **************************************************************************** */

/**
 *
 * @port: This is the port object which is being requested to stop.
 *
 * This method will stop a failed port.  This causes a transition to the
 * stopping state. enum sci_status SCI_SUCCESS
 */
static enum sci_status scic_sds_port_reset_state_stop_handler(
	struct sci_base_port *port)
{
	struct scic_sds_port *this_port = (struct scic_sds_port *)port;

	sci_base_state_machine_change_state(
		&this_port->parent.state_machine,
		SCI_BASE_PORT_STATE_STOPPING
		);

	return SCI_SUCCESS;
}

/**
 *
 * @port: This is the struct sci_base_port object which is cast into a struct scic_sds_port
 *    object.
 *
 * This method will transition a failed port to its ready state.  The port
 * failed because a hard reset request timed out but at some time later one or
 * more phys in the port became ready. enum sci_status SCI_SUCCESS
 */
static void scic_sds_port_reset_state_link_up_handler(
	struct scic_sds_port *this_port,
	struct scic_sds_phy *phy)
{
	/*
	 * / @todo We should make sure that the phy that has gone link up is the same
	 * /       one on which we sent the reset.  It is possible that the phy on
	 * /       which we sent the reset is not the one that has gone link up and we
	 * /       want to make sure that phy being reset comes back.  Consider the
	 * /       case where a reset is sent but before the hardware processes the
	 * /       reset it get a link up on the port because of a hot plug event.
	 * /       because of the reset request this phy will go link down almost
	 * /       immediately. */

	/*
	 * In the resetting state we don't notify the user regarding
	 * link up and link down notifications. */
	scic_sds_port_general_link_up_handler(this_port, phy, false);
}

/**
 *
 * @port: This is the struct sci_base_port object which is cast into a struct scic_sds_port
 *    object.
 *
 * This method process link down notifications that occur during a port reset
 * operation. Link downs can occur during the reset operation. enum sci_status
 * SCI_SUCCESS
 */
static void scic_sds_port_reset_state_link_down_handler(
	struct scic_sds_port *this_port,
	struct scic_sds_phy *phy)
{
	/*
	 * In the resetting state we don't notify the user regarding
	 * link up and link down notifications. */
	scic_sds_port_deactivate_phy(this_port, phy, false);
}

/* --------------------------------------------------------------------------- */

struct scic_sds_port_state_handler
scic_sds_port_state_handler_table[SCI_BASE_PORT_MAX_STATES] =
{
	/* SCI_BASE_PORT_STATE_STOPPED */
	{
		{
			scic_sds_port_stopped_state_start_handler,
			scic_sds_port_stopped_state_stop_handler,
			scic_sds_port_stopped_state_destruct_handler,
			scic_sds_port_default_reset_handler,
			scic_sds_port_stopped_state_add_phy_handler,
			scic_sds_port_stopped_state_remove_phy_handler
		},
		scic_sds_port_default_frame_handler,
		scic_sds_port_default_event_handler,
		scic_sds_port_default_link_up_handler,
		scic_sds_port_default_link_down_handler,
		scic_sds_port_default_start_io_handler,
		scic_sds_port_default_complete_io_handler
	},
	/* SCI_BASE_PORT_STATE_STOPPING */
	{
		{
			scic_sds_port_default_start_handler,
			scic_sds_port_default_stop_handler,
			scic_sds_port_default_destruct_handler,
			scic_sds_port_default_reset_handler,
			scic_sds_port_default_add_phy_handler,
			scic_sds_port_default_remove_phy_handler
		},
		scic_sds_port_default_frame_handler,
		scic_sds_port_default_event_handler,
		scic_sds_port_default_link_up_handler,
		scic_sds_port_default_link_down_handler,
		scic_sds_port_default_start_io_handler,
		scic_sds_port_stopping_state_complete_io_handler
	},
	/* SCI_BASE_PORT_STATE_READY */
	{
		{
			scic_sds_port_default_start_handler,
			scic_sds_port_default_stop_handler,
			scic_sds_port_default_destruct_handler,
			scic_sds_port_default_reset_handler,
			scic_sds_port_default_add_phy_handler,
			scic_sds_port_default_remove_phy_handler
		},
		scic_sds_port_default_frame_handler,
		scic_sds_port_default_event_handler,
		scic_sds_port_default_link_up_handler,
		scic_sds_port_default_link_down_handler,
		scic_sds_port_default_start_io_handler,
		scic_sds_port_general_complete_io_handler
	},
	/* SCI_BASE_PORT_STATE_RESETTING */
	{
		{
			scic_sds_port_default_start_handler,
			scic_sds_port_reset_state_stop_handler,
			scic_sds_port_default_destruct_handler,
			scic_sds_port_default_reset_handler,
			scic_sds_port_default_add_phy_handler,
			scic_sds_port_default_remove_phy_handler
		},
		scic_sds_port_default_frame_handler,
		scic_sds_port_default_event_handler,
		scic_sds_port_reset_state_link_up_handler,
		scic_sds_port_reset_state_link_down_handler,
		scic_sds_port_default_start_io_handler,
		scic_sds_port_general_complete_io_handler
	},
	/* SCI_BASE_PORT_STATE_FAILED */
	{
		{
			scic_sds_port_default_start_handler,
			scic_sds_port_default_stop_handler,
			scic_sds_port_default_destruct_handler,
			scic_sds_port_default_reset_handler,
			scic_sds_port_default_add_phy_handler,
			scic_sds_port_default_remove_phy_handler
		},
		scic_sds_port_default_frame_handler,
		scic_sds_port_default_event_handler,
		scic_sds_port_default_link_up_handler,
		scic_sds_port_default_link_down_handler,
		scic_sds_port_default_start_io_handler,
		scic_sds_port_general_complete_io_handler
	}
};

/*
 * ******************************************************************************
 * *  PORT STATE PRIVATE METHODS
 * ****************************************************************************** */

/**
 *
 * @this_port: This is the port object which to suspend.
 *
 * This method will enable the SCU Port Task Scheduler for this port object but
 * will leave the port task scheduler in a suspended state. none
 */
static void scic_sds_port_enable_port_task_scheduler(
	struct scic_sds_port *this_port)
{
	u32 pts_control_value;

	pts_control_value = scu_port_task_scheduler_read(this_port, control);

	pts_control_value |= SCU_PTSxCR_GEN_BIT(ENABLE) | SCU_PTSxCR_GEN_BIT(SUSPEND);

	scu_port_task_scheduler_write(this_port, control, pts_control_value);
}

/**
 *
 * @this_port: This is the port object which to resume.
 *
 * This method will disable the SCU port task scheduler for this port object.
 * none
 */
static void scic_sds_port_disable_port_task_scheduler(
	struct scic_sds_port *this_port)
{
	u32 pts_control_value;

	pts_control_value = scu_port_task_scheduler_read(this_port, control);

	pts_control_value &= ~(SCU_PTSxCR_GEN_BIT(ENABLE)
			       | SCU_PTSxCR_GEN_BIT(SUSPEND));

	scu_port_task_scheduler_write(this_port, control, pts_control_value);
}

/*
 * ******************************************************************************
 * *  PORT STATE METHODS
 * ****************************************************************************** */

/**
 *
 * @object: This is the struct sci_base_object which is cast to a struct scic_sds_port object.
 *
 * This method will perform the actions required by the struct scic_sds_port on
 * entering the SCI_BASE_PORT_STATE_STOPPED. This function sets the stopped
 * state handlers for the struct scic_sds_port object and disables the port task
 * scheduler in the hardware. none
 */
static void scic_sds_port_stopped_state_enter(
	struct sci_base_object *object)
{
	struct scic_sds_port *this_port;

	this_port = (struct scic_sds_port *)object;

	scic_sds_port_set_base_state_handlers(
		this_port, SCI_BASE_PORT_STATE_STOPPED
		);

	if (
		SCI_BASE_PORT_STATE_STOPPING
		== this_port->parent.state_machine.previous_state_id
		) {
		/*
		 * If we enter this state becasuse of a request to stop
		 * the port then we want to disable the hardwares port
		 * task scheduler. */
		scic_sds_port_disable_port_task_scheduler(this_port);
	}
}

/**
 *
 * @object: This is the struct sci_base_object which is cast to a struct scic_sds_port object.
 *
 * This method will perform the actions required by the struct scic_sds_port on
 * exiting the SCI_BASE_STATE_STOPPED. This function enables the SCU hardware
 * port task scheduler. none
 */
static void scic_sds_port_stopped_state_exit(
	struct sci_base_object *object)
{
	struct scic_sds_port *this_port;

	this_port = (struct scic_sds_port *)object;

	/* Enable and suspend the port task scheduler */
	scic_sds_port_enable_port_task_scheduler(this_port);
}

/**
 *
 * @object: This is the struct sci_base_object which is cast to a struct scic_sds_port object.
 *
 * This method will perform the actions required by the struct scic_sds_port on
 * entering the SCI_BASE_PORT_STATE_READY. This function sets the ready state
 * handlers for the struct scic_sds_port object, reports the port object as not ready
 * and starts the ready substate machine. none
 */
static void scic_sds_port_ready_state_enter(
	struct sci_base_object *object)
{
	struct scic_sds_port *this_port;

	this_port = (struct scic_sds_port *)object;

	/* Put the ready state handlers in place though they will not be there long */
	scic_sds_port_set_base_state_handlers(
		this_port, SCI_BASE_PORT_STATE_READY
		);

	if (
		SCI_BASE_PORT_STATE_RESETTING
		== this_port->parent.state_machine.previous_state_id
		) {
		scic_cb_port_hard_reset_complete(
			scic_sds_port_get_controller(this_port),
			this_port,
			SCI_SUCCESS
			);
	} else {
		/* Notify the caller that the port is not yet ready */
		scic_cb_port_not_ready(
			scic_sds_port_get_controller(this_port),
			this_port,
			SCIC_PORT_NOT_READY_NO_ACTIVE_PHYS
			);
	}

	/* Start the ready substate machine */
	sci_base_state_machine_start(
		scic_sds_port_get_ready_substate_machine(this_port)
		);
}

/**
 *
 * @object: This is the struct sci_base_object which is cast to a struct scic_sds_port object.
 *
 * This method will perform the actions required by the struct scic_sds_port on
 * exiting the SCI_BASE_STATE_READY. This function does nothing. none
 */
static void scic_sds_port_ready_state_exit(
	struct sci_base_object *object)
{
	struct scic_sds_port *this_port;

	this_port = (struct scic_sds_port *)object;

	sci_base_state_machine_stop(&this_port->ready_substate_machine);
}

/**
 *
 * @object: This is the struct sci_base_object which is cast to a struct scic_sds_port object.
 *
 * This method will perform the actions required by the struct scic_sds_port on
 * entering the SCI_BASE_PORT_STATE_RESETTING. This function sets the resetting
 * state handlers for the struct scic_sds_port object. none
 */
static void scic_sds_port_resetting_state_enter(
	struct sci_base_object *object)
{
	struct scic_sds_port *this_port;

	this_port = (struct scic_sds_port *)object;

	scic_sds_port_set_base_state_handlers(
		this_port, SCI_BASE_PORT_STATE_RESETTING
		);

	scic_sds_port_set_direct_attached_device_id(
		this_port,
		SCIC_SDS_REMOTE_NODE_CONTEXT_INVALID_INDEX
		);
}

/**
 *
 * @object: This is the struct sci_base_object which is cast to a struct scic_sds_port object.
 *
 * This method will perform the actions required by the struct scic_sds_port on
 * exiting the SCI_BASE_STATE_RESETTING. This function does nothing. none
 */
static void scic_sds_port_resetting_state_exit(
	struct sci_base_object *object)
{
	struct scic_sds_port *this_port;

	this_port = (struct scic_sds_port *)object;

	scic_cb_timer_stop(
		scic_sds_port_get_controller(this_port),
		this_port->timer_handle
		);
}

/**
 *
 * @object: This is the struct sci_base_object which is cast to a struct scic_sds_port object.
 *
 * This method will perform the actions required by the struct scic_sds_port on
 * entering the SCI_BASE_PORT_STATE_STOPPING. This function sets the stopping
 * state handlers for the struct scic_sds_port object. none
 */
static void scic_sds_port_stopping_state_enter(
	struct sci_base_object *object)
{
	struct scic_sds_port *this_port;

	this_port = (struct scic_sds_port *)object;

	scic_sds_port_set_base_state_handlers(
		this_port, SCI_BASE_PORT_STATE_STOPPING
		);
}

/**
 *
 * @object: This is the struct sci_base_object which is cast to a struct scic_sds_port object.
 *
 * This method will perform the actions required by the struct scic_sds_port on
 * exiting the SCI_BASE_STATE_STOPPING. This function does nothing. none
 */
static void scic_sds_port_stopping_state_exit(
	struct sci_base_object *object)
{
	struct scic_sds_port *this_port;

	this_port = (struct scic_sds_port *)object;

	scic_cb_timer_stop(
		scic_sds_port_get_controller(this_port),
		this_port->timer_handle
		);
}

/**
 *
 * @object: This is the struct sci_base_object which is cast to a struct scic_sds_port object.
 *
 * This method will perform the actions required by the struct scic_sds_port on
 * entering the SCI_BASE_PORT_STATE_STOPPING. This function sets the stopping
 * state handlers for the struct scic_sds_port object. none
 */
static void scic_sds_port_failed_state_enter(
	struct sci_base_object *object)
{
	struct scic_sds_port *this_port;

	this_port = (struct scic_sds_port *)object;

	scic_sds_port_set_base_state_handlers(
		this_port,
		SCI_BASE_PORT_STATE_FAILED
		);

	scic_cb_port_hard_reset_complete(
		scic_sds_port_get_controller(this_port),
		this_port,
		SCI_FAILURE_TIMEOUT
		);
}

/* --------------------------------------------------------------------------- */

const struct sci_base_state scic_sds_port_state_table[] = {
	[SCI_BASE_PORT_STATE_STOPPED] = {
		.enter_state = scic_sds_port_stopped_state_enter,
		.exit_state  = scic_sds_port_stopped_state_exit
	},
	[SCI_BASE_PORT_STATE_STOPPING] = {
		.enter_state = scic_sds_port_stopping_state_enter,
		.exit_state  = scic_sds_port_stopping_state_exit
	},
	[SCI_BASE_PORT_STATE_READY] = {
		.enter_state = scic_sds_port_ready_state_enter,
		.exit_state  = scic_sds_port_ready_state_exit
	},
	[SCI_BASE_PORT_STATE_RESETTING] = {
		.enter_state = scic_sds_port_resetting_state_enter,
		.exit_state  = scic_sds_port_resetting_state_exit
	},
	[SCI_BASE_PORT_STATE_FAILED] = {
		.enter_state = scic_sds_port_failed_state_enter,
	}
};

