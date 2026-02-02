.. _uart_passthrough:

UART Passthrough (P1.10 to Console)
###################################

This sample reads UART input from **P1.10** (RX) on the nRF54H20 DK and forwards
it to the default console (VCOM), so everything appears in the same serial
terminal you use for normal console output. The input UART uses a separate DMA
region (``dma_fast_region``) so its buffers do not overlap with the console
UART's DMA region. LED0 blinks to indicate the application is running.

Requirements
************

* nRF54H20 DK
* External UART source connected to **P1.10** (RX), 115200 8N1, same GND as DK

Building and running
********************

.. code-block:: bash

   west build -b nrf54h20dk/nrf54h20/cpuapp zephyr/samples/drivers/uart/uart_passthrough
   west flash

Open the DK's VCOM port (e.g. 115200 8N1). You will see the startup message and
any data received on P1.10. LED0 blinks every 500 ms.
