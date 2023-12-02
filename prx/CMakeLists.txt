#
# Copyright (c) 2019 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#
cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(NONE)

target_sources(app PRIVATE
  src/main.c
)

if (CONFIG_SOC_NRF5340_CPUNET)
  target_sources(app PRIVATE 
    ../common/53_net/hci_rpmsg_module.c
    ../common/53_net/app_esb_53_net.c
    ../common/timeslot_handler.c)
  zephyr_library_include_directories(../common/53_net)
elseif(CONFIG_SOC_NRF5340_CPUAPP)
  target_sources(app PRIVATE 
    ../common/app_bt_lbs.c
    ../common/53_app/app_esb_53_app.c)
else()
  # If we build for neither 5340 appcore or netcore, assume a 52 series board is selected
  target_sources(app PRIVATE 
    ../common/app_bt_lbs.c
    ../common/app_esb.c
    ../common/timeslot_handler.c)
endif()

zephyr_library_include_directories(../common)
