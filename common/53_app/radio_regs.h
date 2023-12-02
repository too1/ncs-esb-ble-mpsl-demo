/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/* Copy of the radio bitfields, as they are needed by esb.h and don't exist in
 * the nrf5340 app core device header.
 */
#ifndef __RADIO_REGS_H
#define __RADIO_REGS_H

/* Bits 7..0 : RADIO output power */
#define RADIO_TXPOWER_TXPOWER_Pos (0UL) /*!< Position of TXPOWER field. */
#define RADIO_TXPOWER_TXPOWER_Msk (0xFFUL << RADIO_TXPOWER_TXPOWER_Pos) /*!< Bit mask of TXPOWER field. */
#define RADIO_TXPOWER_TXPOWER_0dBm (0x0UL) /*!< 0 dBm */
#define RADIO_TXPOWER_TXPOWER_Neg40dBm (0xD8UL) /*!< -40 dBm */
#define RADIO_TXPOWER_TXPOWER_Neg30dBm (0xE2UL) /*!< Deprecated enumerator -  -40 dBm */
#define RADIO_TXPOWER_TXPOWER_Neg20dBm (0xECUL) /*!< -20 dBm */
#define RADIO_TXPOWER_TXPOWER_Neg16dBm (0xF0UL) /*!< -16 dBm */
#define RADIO_TXPOWER_TXPOWER_Neg12dBm (0xF4UL) /*!< -12 dBm */
#define RADIO_TXPOWER_TXPOWER_Neg8dBm (0xF8UL) /*!< -8 dBm */
#define RADIO_TXPOWER_TXPOWER_Neg7dBm (0xF9UL) /*!< -7 dBm */
#define RADIO_TXPOWER_TXPOWER_Neg6dBm (0xFAUL) /*!< -6 dBm */
#define RADIO_TXPOWER_TXPOWER_Neg5dBm (0xFBUL) /*!< -5 dBm */
#define RADIO_TXPOWER_TXPOWER_Neg4dBm (0xFCUL) /*!< -4 dBm */
#define RADIO_TXPOWER_TXPOWER_Neg3dBm (0xFDUL) /*!< -3 dBm */
#define RADIO_TXPOWER_TXPOWER_Neg2dBm (0xFEUL) /*!< -2 dBm */
#define RADIO_TXPOWER_TXPOWER_Neg1dBm (0xFFUL) /*!< -1 dBm */
#define RADIO_TXPOWER_TXPOWER_Pos4dBm (0xFFUL) /*!< -1 dBm */

/* Bits 3..0 : Radio data rate and modulation setting. The radio supports frequency-shift keying (FSK) modulation. */
#define RADIO_MODE_MODE_Pos (0UL) /*!< Position of MODE field. */
#define RADIO_MODE_MODE_Msk (0xFUL << RADIO_MODE_MODE_Pos) /*!< Bit mask of MODE field. */
#define RADIO_MODE_MODE_Nrf_1Mbit (0UL) /*!< 1 Mbps Nordic proprietary radio mode */
#define RADIO_MODE_MODE_Nrf_2Mbit (1UL) /*!< 2 Mbps Nordic proprietary radio mode */
#define RADIO_MODE_MODE_Ble_1Mbit (3UL) /*!< 1 Mbps BLE */
#define RADIO_MODE_MODE_Ble_2Mbit (4UL) /*!< 2 Mbps BLE */
#define RADIO_MODE_MODE_Ble_LR125Kbit (5UL) /*!< Long Range 125 kbps TX, 125 kbps and 500 kbps RX */
#define RADIO_MODE_MODE_Ble_LR500Kbit (6UL) /*!< Long Range 500 kbps TX, 125 kbps and 500 kbps RX */
#define RADIO_MODE_MODE_Ieee802154_250Kbit (15UL) /*!< IEEE 802.15.4-2006 250 kbps */

/* Bits 1..0 : CRC length in number of bytes For MODE Ble_LR125Kbit and Ble_LR500Kbit, only LEN set to 3 is supported */
#define RADIO_CRCCNF_LEN_Pos (0UL) /*!< Position of LEN field. */
#define RADIO_CRCCNF_LEN_Msk (0x3UL << RADIO_CRCCNF_LEN_Pos) /*!< Bit mask of LEN field. */
#define RADIO_CRCCNF_LEN_Disabled (0UL) /*!< CRC length is zero and CRC calculation is disabled */
#define RADIO_CRCCNF_LEN_One (1UL) /*!< CRC length is one byte and CRC calculation is enabled */
#define RADIO_CRCCNF_LEN_Two (2UL) /*!< CRC length is two bytes and CRC calculation is enabled */
#define RADIO_CRCCNF_LEN_Three (3UL) /*!< CRC length is three bytes and CRC calculation is enabled */

/* NOTE: This should be kept in sync with the config value on the network core. */
#define CONFIG_ESB_MAX_PAYLOAD_LENGTH 32

#endif