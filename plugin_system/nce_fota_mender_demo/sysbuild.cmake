#
# Copyright (c) 2024 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#
if(SB_CONFIG_THINGY91_STATIC_PARTITIONS_FACTORY)
set(PM_STATIC_YML_FILE ${CMAKE_CURRENT_LIST_DIR}/thingy91_pm_static.yml CACHE INTERNAL "")
endif()