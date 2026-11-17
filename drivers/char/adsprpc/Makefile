# SPDX-License-Identifier: GPL-2.0-only

ccflags-y += -I$(src)/include/linux -I$(src)/include/uapi -I$(src)/dsp

obj-$(CONFIG_MSM_ADSPRPC) += frpc-adsprpc.o
frpc-adsprpc-y := dsp/adsprpc.o dsp/adsprpc_rpmsg.o
frpc-adsprpc-$(CONFIG_COMPAT) += dsp/adsprpc_compat.o
