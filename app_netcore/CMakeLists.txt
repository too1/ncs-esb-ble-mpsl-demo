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
  ../common/53_net/hci_rpmsg_module.c
  ../common/53_net/app_esb_53_net.c
  ../common/app_esb.c
  ../common/timeslot_handler.c
)

zephyr_library_include_directories(../common ../common/53_net)
