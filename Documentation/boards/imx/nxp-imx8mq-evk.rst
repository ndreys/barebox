NXP i.MX8MQ EVK Evaluation Board
================================

Board comes with:

* 3GiB of LPDDR4 RAM
* 16GiB eMMC

Not including booting via serial, the device can boot from either SD or eMMC.

Downloading DDR PHY Firmware
----------------------------

As a part of DDR intialization routine NXP i.MX8MQ EVK requires and
uses several binary firmware blobs that are distributed under a
separate EULA and cannot be included in Barebox. In order to obtain
the do the following::

 wget https://www.nxp.com/lgfiles/NMG/MAD/YOCTO/firmware-imx-7.2.bin
 chmod +x firmware-imx-7.2.bin
 ./firmware-imx-7.2.bin

Executing that file should produce a EULA acceptance dialog as well as
result int the following files:

- lpddr4_pmu_train_1d_dmem.bin
- lpddr4_pmu_train_1d_imem.bin
- lpddr4_pmu_train_2d_dmem.bin
- lpddr4_pmu_train_2d_imem.bin

As a last step of this process those files need to be placed in
"arch/arm/boards/nxp-imx8mq-evk"::

  for f in lpddr4_pmu_train_1d_dmem  \
           lpddr4_pmu_train_1d_imem  \
	   lpddr4_pmu_train_2d_dmem  \
	   lpddr4_pmu_train_2d_imem; \
  do \
	   cp firmware-imx-7.2/firmware/ddr/synopsys/${f}.bin \
	      arch/arm/boards/nxp-imx8mq-evk/${f}.ddr-phy-fw; \
  done

DDR Configuration Code
======================

The following two files:

  - ddr_init.c
  - ddrphy_train.c

were obtained by running i.MX 8M DDR Tool that can be found here:

https://community.nxp.com/docs/DOC-340179

Only minimal amount of necessary changes were made to those files.
All of the "impedance matching" code is located in "ddr.h".

Build Barebox
=============

 make imv_v8_defconfig
 make

Flashing the binary onto the SD card
====================================

Due to changes to MaskROM behaviour, i.MX8 deviates in this procedure
slighty and command for placing barebox onto an SD card is as follows::

  dd if=images/barebox-nxp-imx8mq-evk.img of=/dev/sdb bs=1024 skip=1 seek=33

Boot Configuration
==================

The NXM i.MX8MQ EVK Evaluation Board has has two switches responsible
for configuring bootsource/boot mode:

 * SW802 for selecting appropriate BMOD
 * SW801 for selecting appropriate boot medium

In order to select internal boot set SW802 as follows::

  +-----+
  |     |
  | O | | <--- on = high level
  | | | |
  | | O | <--- off = low level
  |     |
  | 1 2 |
  +-----+

Bootsource is the internal eMMC::

  +---------+
  |         |
  | | | O | |
  | | | | | |  <---- eMMC
  | O O O O |
  |         |
  | 1 2 3 4 |
  +---------+

Bootsource is the SD2 slot::

  +---------+
  |         |
  | O O | | |
  | | | | | |  <---- SD2
  | | | O O |
  |         |
  | 1 2 3 4 |
  +---------+


Serial boot SW802 setting needed for i.MX8 DDR Tool is as follows::

  +-----+
  |     |
  | | O | <--- on = high level
  | | | |
  | O | | <--- off = low level
  |     |
  | 1 2 |
  +-----+