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

/**
 * This file contains all of the unsolicited frame related management for the
 *    address table, the headers, and actual payload buffers.
 *
 *
 */

#ifndef _SCIC_SDS_UNSOLICITED_FRAME_CONTROL_H_
#define _SCIC_SDS_UNSOLICITED_FRAME_CONTROL_H_

#include "scu_unsolicited_frame.h"
#include "sci_memory_descriptor_list.h"
#include "scu_constants.h"
#include "sci_status.h"

/**
 * enum UNSOLICITED_FRAME_STATE -
 *
 * This enumeration represents the current unsolicited frame state.  The
 * controller object can not updtate the hardware unsolicited frame put pointer
 * unless it has already processed the priror unsolicited frames.
 */
enum UNSOLICITED_FRAME_STATE {
	/**
	 * This state is when the frame is empty and not in use.  It is
	 * different from the released state in that the hardware could DMA
	 * data to this frame buffer.
	 */
	UNSOLICITED_FRAME_EMPTY,

	/**
	 * This state is set when the frame buffer is in use by by some
	 * object in the system.
	 */
	UNSOLICITED_FRAME_IN_USE,

	/**
	 * This state is set when the frame is returned to the free pool
	 * but one or more frames prior to this one are still in use.
	 * Once all of the frame before this one are freed it will go to
	 * the empty state.
	 */
	UNSOLICITED_FRAME_RELEASED,

	UNSOLICITED_FRAME_MAX_STATES
};

/**
 * struct scic_sds_unsolicited_frame -
 *
 * This is the unsolicited frame data structure it acts as the container for
 * the current frame state, frame header and frame buffer.
 */
struct scic_sds_unsolicited_frame {
	/**
	 * This field contains the current frame state
	 */
	enum UNSOLICITED_FRAME_STATE state;

	/**
	 * This field points to the frame header data.
	 */
	struct scu_unsolicited_frame_header *header;

	/**
	 * This field points to the frame buffer data.
	 */
	void *buffer;

};

/**
 * struct scic_sds_uf_header_array -
 *
 * This structure contains all of the unsolicited frame header information.
 */
struct scic_sds_uf_header_array {
	/**
	 * This field is represents a virtual pointer to the start
	 * address of the UF address table.  The table contains
	 * 64-bit pointers as required by the hardware.
	 */
	struct scu_unsolicited_frame_header *array;

	/**
	 * This field specifies the physical address location for the UF
	 * buffer array.
	 */
	dma_addr_t physical_address;

};

/*
 * Determine the size of the unsolicited frame array including
 * unused buffers. */
#if SCU_UNSOLICITED_FRAME_COUNT <= SCU_MIN_UF_TABLE_ENTRIES
#define SCU_UNSOLICITED_FRAME_CONTROL_ARRAY_SIZE SCU_MIN_UF_TABLE_ENTRIES
#else
#define SCU_UNSOLICITED_FRAME_CONTROL_ARRAY_SIZE SCU_MAX_UNSOLICITED_FRAMES
#endif /* SCU_UNSOLICITED_FRAME_COUNT <= SCU_MIN_UF_TABLE_ENTRIES */

/**
 * struct scic_sds_uf_buffer_array -
 *
 * This structure contains all of the unsolicited frame buffer (actual payload)
 * information.
 */
struct scic_sds_uf_buffer_array {
	/**
	 * This field is the minimum number of unsolicited frames supported by the
	 * hardware and the number of unsolicited frames requested by the software.
	 */
	u32 count;

	/**
	 * This field is the SCIC_UNSOLICITED_FRAME data its used to manage
	 * the data for the unsolicited frame requests.  It also represents
	 * the virtual address location that corresponds to the
	 * physical_address field.
	 */
	struct scic_sds_unsolicited_frame array[SCU_UNSOLICITED_FRAME_CONTROL_ARRAY_SIZE];

	/**
	 * This field specifies the physical address location for the UF
	 * buffer array.
	 */
	dma_addr_t physical_address;

};

/**
 * struct scic_sds_uf_address_table_array -
 *
 * This object maintains all of the unsolicited frame address table specific
 * data.  The address table is a collection of 64-bit pointers that point to
 * 1KB buffers into which the silicon will DMA unsolicited frames.
 */
struct scic_sds_uf_address_table_array {
	/**
	 * This field specifies the actual programmed size of the
	 * unsolicited frame buffer address table.  The size of the table
	 * can be larger than the actual number of UF buffers, but it must
	 * be a power of 2 and the last entry in the table is not allowed
	 * to be NULL.
	 */
	u32 count;

	/**
	 * This field represents a virtual pointer that refers to the
	 * starting address of the UF address table.
	 * 64-bit pointers are required by the hardware.
	 */
	dma_addr_t *array;

	/**
	 * This field specifies the physical address location for the UF
	 * address table.
	 */
	dma_addr_t physical_address;

};

/**
 * struct scic_sds_unsolicited_frame_control -
 *
 * This object contains all of the data necessary to handle unsolicited frames.
 */
struct scic_sds_unsolicited_frame_control {
	/**
	 * This field is the software copy of the unsolicited frame queue
	 * get pointer.  The controller object writes this value to the
	 * hardware to let the hardware put more unsolicited frame entries.
	 */
	u32 get;

	/**
	 * This field contains all of the unsolicited frame header
	 * specific fields.
	 */
	struct scic_sds_uf_header_array headers;

	/**
	 * This field contains all of the unsolicited frame buffer
	 * specific fields.
	 */
	struct scic_sds_uf_buffer_array buffers;

	/**
	 * This field contains all of the unsolicited frame address table
	 * specific fields.
	 */
	struct scic_sds_uf_address_table_array address_table;

};

void scic_sds_unsolicited_frame_control_set_address_table_count(
	struct scic_sds_unsolicited_frame_control *uf_control);

struct scic_sds_controller;
void scic_sds_unsolicited_frame_control_construct(
	struct scic_sds_unsolicited_frame_control *uf_control,
	struct sci_physical_memory_descriptor *mde,
	struct scic_sds_controller *this_controller);

enum sci_status scic_sds_unsolicited_frame_control_get_header(
	struct scic_sds_unsolicited_frame_control *uf_control,
	u32 frame_index,
	void **frame_header);

enum sci_status scic_sds_unsolicited_frame_control_get_buffer(
	struct scic_sds_unsolicited_frame_control *uf_control,
	u32 frame_index,
	void **frame_buffer);

bool scic_sds_unsolicited_frame_control_release_frame(
	struct scic_sds_unsolicited_frame_control *uf_control,
	u32 frame_index);

/**
 * scic_sds_unsolicited_frame_control_get_mde_size() -
 *
 * This macro simply calculates the size of the memory descriptor entry that
 * relates to unsolicited frames and the surrounding silicon memory required to
 * utilize it.
 */
#define scic_sds_unsolicited_frame_control_get_mde_size(uf_control) \
	(((uf_control).buffers.count * SCU_UNSOLICITED_FRAME_BUFFER_SIZE) \
	 + ((uf_control).address_table.count * sizeof(dma_addr_t)) \
	 + ((uf_control).buffers.count * sizeof(struct scu_unsolicited_frame_header)))

#endif /* _SCIC_SDS_UNSOLICITED_FRAME_CONTROL_H_ */
