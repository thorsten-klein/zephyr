.. zephyr:board:: CCM M3

Overview
********

This is a SoM that is used by BSH home appliances.

It is based on the NXP RW610, a wireless MCU with integrated radio for Wi-Fi 6, Bluetooth 5.3 and 802.15.4.

.. image:: img/ccm_m3.jpg
   :align: center
   :alt: CCM M3

Hardware
********

- NXP RW610
- 260 MHz ARM Cortex-M33, tri-radio cores for Wifi 6 + BLE 5.3 + 802.15.4
- 1.2 MB on-chip SRAM

Supported Features
==================

.. zephyr:board-supported-hw::

.. note::

   Power modes 1, 2 and 3 are supported when using System Power Management.

Programming and Debugging
*************************

.. zephyr:board-supported-runners::

Build and flash applications as usual (see :ref:`build_an_application` and
:ref:`application_run` for more details).

Configuring a Debug Probe
=========================

Currently only a Segger J-Link debug probe is supported for both flashing and 
debugging the board. 

Additionally, an adapter for the standard JTAG 20-pin connector is required to 
connect the J-Link to the board with the 0.05 inch 10-pin connector, such as:

- `Olimex ARM-JTAG-20-10 <https://www.olimex.com/Products/ARM/JTAG/ARM-JTAG-20-10/>`_

Configuring a Console
=====================

Connect a USB cable from your PC to the Micro-USB connector next to the power 
switch, and use the serial terminal of your choice (minicom, putty, etc.) with 
the following settings:

- Speed: 115200
- Data: 8 bits
- Parity: None
- Stop bits: 1

The host PC should automatically detect the USB connection and create 2 new 
serial ports (e.g. /dev/ttyACM0 and /dev/ttyACM1 on Linux). 
Use the second port to connect your serial terminal.

Flashing
========

Here is an example for the :zephyr:code-sample:`hello_world` application. This example uses the
:ref:`jlink-debug-host-tools` as default.

.. zephyr-app-commands::
   :zephyr-app: samples/hello_world
   :board: ccm_m3
   :goals: flash

Open a serial terminal, reset the board (press the RESET button), and you should
see the following message in the terminal:

.. code-block:: console

   ***** Booting Zephyr OS v3.6.0 *****
   Hello World! ccm_m3

Debugging
=========

Here is an example for the :zephyr:code-sample:`hello_world` application. This example uses the
:ref:`jlink-debug-host-tools` as default.

.. zephyr-app-commands::
   :zephyr-app: samples/hello_world
   :board: ccm_m3
   :goals: debug

Open a serial terminal, step through the application in your debugger, and you
should see the following message in the terminal:

.. code-block:: console

   ***** Booting Zephyr OS zephyr-v3.6.0 *****
   Hello World! ccm_m3


Resources
*********

- `NXP RW610 Product Page <https://www.nxp.com/products/wireless-connectivity/wi-fi-plus-bluetooth-plus-802-15-4/wireless-mcu-with-integrated-radio-1x1-wi-fi-6-plus-bluetooth-low-energy-5-4-radios:RW610>`_