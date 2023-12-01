#
# Copyright (c) 2019 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#
cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(NONE)

if (CONFIG_SOC_NRF5340_CPUAPP)
  message(FATAL_ERROR "This sample is not supported on the nRF53 application core")
endif()

target_sources(app PRIVATE
  src/main.c
  ../common/timeslot_handler.c
  ../common/app_esb.c
)

if (CONFIG_SOC_NRF5340_CPUNET)
  target_sources(app PRIVATE ../common/53_net/hci_rpmsg_module.c)
  zephyr_library_include_directories(../common/53_net)
endif()

zephyr_library_include_directories(../common)
