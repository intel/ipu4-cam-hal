/*
* INTEL CONFIDENTIAL
*
* Copyright (C) 2017 - 2017 Intel Corporation.
* All Rights Reserved.
*
* The source code contained or described herein and all documents
* related to the source code ("Material") are owned by Intel Corporation
* or licensors. Title to the Material remains with Intel
* Corporation or its licensors. The Material contains trade
* secrets and proprietary and confidential information of Intel or its
* licensors. The Material is protected by worldwide copyright
* and trade secret laws and treaty provisions. No part of the Material may
* be used, copied, reproduced, modified, published, uploaded, posted,
* transmitted, distributed, or disclosed in any way without Intel's prior
* express written permission.
*
* No License under any patent, copyright, trade secret or other intellectual
* property right is granted to or conferred upon you by disclosure or
* delivery of the Materials, either expressly, by implication, inducement,
* estoppel or otherwise. Any license under such intellectual property rights
* must be express and approved by Intel in writing.
*/

#ifndef __IA_CSS_PSYS_TRANSPORT_DEP_H
#define __IA_CSS_PSYS_TRANSPORT_DEP_H

/*
 * The ID's of the Psys specific queues.
 */
typedef enum ia_css_psys_cmd_queues {
	/**< The in-order queue for scheduled process groups */
	IA_CSS_PSYS_CMD_QUEUE_COMMAND_ID = 0,
	/**< The in-order queue for commands changing psys or
	 * process group state
	 */
	IA_CSS_PSYS_CMD_QUEUE_DEVICE_ID,
	/**< An in-order queue for dedicated PPG commands */
	IA_CSS_PSYS_CMD_QUEUE_PPG0_COMMAND_ID,
	/**< An in-order queue for dedicated PPG commands */
	IA_CSS_PSYS_CMD_QUEUE_PPG1_COMMAND_ID,
	IA_CSS_N_PSYS_CMD_QUEUE_ID
} ia_css_psys_cmd_queue_ID_t;

#endif /* __IA_CSS_PSYS_TRANSPORT_DEP_H */
