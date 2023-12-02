/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef __ESB_RPC_IDS_H
#define __ESB_RPC_IDS_H

/* The command and event IDs need to be the same for both RPC sides. */

enum rpc_command {
	RPC_COMMAND_INIT = 0x01,
	RPC_COMMAND_TX = 0x02,
};

enum rpc_event {
	RPC_EVENT_RX_CB = 0x01,
};

#endif