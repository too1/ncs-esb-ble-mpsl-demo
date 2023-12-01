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

# NORDIC SDK APP START
target_sources(app PRIVATE
  src/main.c
  ../common/hci_rpmsg_module.c
#  ../common/app_bt_lbs.c
  ../common/timeslot_handler.c
  ../common/app_esb.c
)
# NORDIC SDK APP END
zephyr_library_include_directories(../common)
