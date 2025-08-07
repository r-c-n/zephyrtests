
Overview
********

This application is a rather complete example of a simple program for
the Raspberry Pico 2 and can be used as a template for similar programs
and as an example of how to use certain Zephyr features. These are the
topics it covers:

- Support for a specific hardware and for the native_sim target. This
  allows development and debugging on the development machine and
  deployment on the real hardware.
- Device customization via device tree overlays.
- Thread creation.
- GPIO interrupt handling.
- Emulated GPIO usage for the native_sim target.
- Inter-thread communication via message queues.
- Workqueues.
- Simultaneous shell and logging output on different UARTs.
- Minimal USB CDC ACM device setup.
- Board as I2C target.
- Emulated I2C device for native_sim.

Application behavior
********************

The application starts a processing thread that receives data messages
through a message queue, does some processing on them and sends a
response through another message queue.

There's a GPIO callback registered to run when the sw0 button is
pressed. The press is de-bounced by scheduling the processing to a
workqueue after a few milliseconds. The processing of the button press
sends a message with a timestamp to the processing thread.

The main thread runs an infinite loop that's used for testing purposes
to receive and print the data from the processing thread and to simulate
button presses when running on native_sim.

Requirements
************

The application is meant to be run either on native_sim or flashed into
a Raspberry Pico 2. When running on the board, the USB port used for the
flashing can also be used as the logging terminal. A secondary UART for
shell interaction is configured in pins 11 (TX) and 12 (RX).


Building and running
********

To build the application for the native_sim target, run:

.. zephyr-app-commands::
   :zephyr-app: test_rpi
   :board: native_sim/native/64
   :goals: build
   :compact:

.. code-block:: console

   west build -p always -b native_sim/native/64 test_rpi

Then, you can start the native application by running:

.. code-block:: console

   ./build/zephyr/zephyr.exe

The log messages will be piped to standard output. To access the shell,
check the initial messages printed in the console and look for the
pseudo-terminal used for the secondary UART, something like this:

.. code-block:: none

   uart connected to pseudotty: /dev/pts/7

You can connect to it using any terminal emulator:

.. code-block:: console

   screen /dev/pts/7

To build it for the Raspberry Pico 2, run:

.. zephyr-app-commands::
   :zephyr-app: test_rpi
   :board: rpi_pico2/rp2350a/m33
   :goals: build
   :compact:

.. code-block:: console

   west build -p always -b rpi_pico2/rp2350a/m33 test_rpi

To flash the board, plug the USB cable while pressing the "BOOTSEL"
button, mount the mass storage device that will be detected and run:

.. code-block:: console

   west flash -r uf2

The primary UART is accessible through the ACM device created by the
firmware (eg. /dev/ttyACM0), you can connect to it using any suitable
serial terminal (baudrate is 115200). The shell UART is accessible
through pins 11 (TX) and 12 (RX).
