NXP i.MX8MQ EVK Evaluation Board
================================

Board comes with:

* 3GiB of LPDDR4 RAM
* XGiB eMMC
* 

Not including booting via serial, the device can boot from either SD or eMMC.

In order to use the board with Linux the following prerequsites must be met:

Downloading DDR PHY Firmware
----------------------------

As a part of DDR intialization routine NXP i.MX8MQ EVK requires and
uses several binary firmware blob that are distributed under a
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

FIXME FIXME FIXME FIXME FIXME FIXME FIXME FIXME FIXME FIXME
FIXME FIXME FIXME FIXME FIXME FIXME FIXME FIXME FIXME FIXME
FIXME FIXME FIXME FIXME FIXME FIXME FIXME FIXME FIXME FIXME

Build Barebox
=============

 make imv_v8_defconfig
 make
  
Get and Build the ARM Trusted firmware
======================================

 git clone https://source.codeaurora.org/external/imx/imx-atf
 cd imx-atf/
 git checkout origin/imx_4.9.51_imx8m_beta
 make PLAT=imx8mq bl31

FIXME FIXME FIXME FIXME FIXME FIXME FIXME FIXME FIXME FIXME
FIXME FIXME FIXME FIXME FIXME FIXME FIXME FIXME FIXME FIXME
FIXME FIXME FIXME FIXME FIXME FIXME FIXME FIXME FIXME FIXME

Flashing the binary onto the SD card
====================================

Due to changes to MaskROM behaviour, i.MX8 deviates in this procedure
slighty and command for placing barebox onto an SD card is as follows::

  dd if=images/barebox-nxp-imx8mq-evk.img of=/dev/sdb bs=1024 skip=1 seek=33

Necessary Hardware Switch Configuration
=======================================

In order to configure the board to boot from SD card select:

The WaRP7 IO Board has a double DIP switch where switch number two defines the
boot source of the i.MX7 SoC::

  +-----+
  |     |
  | | O | <--- on = high level
  | | | |
  | O | | <--- off = low level
  |     |
  | 1 2 |
  +-----+

Bootsource is the internal eMMC::

  +---------+	    
  |         |	    
  | O O | | |
  | | | | | |  <---- eMMC	    
  | | | O O |
  |         |	    
  | 1 2 3 4 |	    
  +---------+

Bootsource is the USB::


  +---------+	    
  |         |	    
  | O O | | |
  | | | | | |  <---- eMMC	    
  | | | O O |
  |         |	    
  | 1 2 3 4 |	    
  +---------+  





  SW802: ON-OFF