# SPDX-License-Identifier: GPL-2.0-only

obj-$(CONFIG_QPNP_SMB5) += qpnp-smb5-main.o
qpnp-smb5-main-y += step-chg-jeita.o battery.o qpnp-smb5.o smb5-lib.o storm-watch.o schgm-flash.o smb5-iio.o
obj-$(CONFIG_QPNP_QG)	+= qcom-qpnp-qg.o
qcom-qpnp-qg-y	+= qpnp-qg.o qg-util.o qg-soc.o qg-sdam.o qg-battery-profile.o qg-profile-lib.o fg-alg.o

# Shared by both drivers, which are built in
ifneq ($(CONFIG_QPNP_SMB5)$(CONFIG_QPNP_QG),)
obj-y += pmic-voter.o battery-profile-loader.o
endif
