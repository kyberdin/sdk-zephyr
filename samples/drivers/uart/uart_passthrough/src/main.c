/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Reads UART input from P1.10 (uart135 RX) and forwards it to the default
 * console (VCOM). Uses interrupt-driven RX so bytes are moved into a ring
 * buffer as soon as they arrive; the single-byte hardware buffer can't be
 * overwritten and drop leading digits after newlines.
 * Blinks LED0 in a thread to show the application is running.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/ring_buffer.h>

#define DEV_CONSOLE DEVICE_DT_GET(DT_CHOSEN(zephyr_console))
#define DEV_UART_P1 DEVICE_DT_GET(DT_NODELABEL(uart135))

#define LED0_NODE DT_ALIAS(led0)
#define BLINK_INTERVAL_MS 500
#define RX_RING_SIZE 1024

static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

RING_BUF_DECLARE(rx_ring, RX_RING_SIZE);

static volatile bool rx_overflow;

/* Runs in ISR context: move byte from UART into ring immediately so it can't be overwritten. */
static void uart_p1_irq_cb(const struct device *dev, void *user_data)
{
	uint8_t byte;
	int n;

	ARG_UNUSED(user_data);

	while (uart_irq_update(dev) > 0 && uart_irq_rx_ready(dev)) {
		n = uart_fifo_read(dev, &byte, 1);
		if (n <= 0) {
			break;
		}
		if (ring_buf_put(&rx_ring, &byte, 1) != 1) {
			rx_overflow = true;
			uart_irq_rx_disable(dev);
			break;
		}
	}
}

/* Returns true if something was done (keep going); false when idle (sleep). */
static bool forward_to_console(void)
{
	uint8_t byte;

	if (rx_overflow) {
		rx_overflow = false;
		uart_irq_rx_enable(DEV_UART_P1);
		printk("\n[P1.10 overflow]\n");
		return true;
	}

	if (ring_buf_get(&rx_ring, &byte, 1) != 1) {
		return false;
	}
	if (byte == '\n') {
		uart_poll_out(DEV_CONSOLE, '\r');
	}
	uart_poll_out(DEV_CONSOLE, byte);
	return true;
}

static void blink_thread(void *p1, void *p2, void *p3)
{
	const struct gpio_dt_spec *led = p1;

	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	if (!gpio_is_ready_dt(led) || gpio_pin_configure_dt(led, GPIO_OUTPUT_ACTIVE) < 0) {
		return;
	}

	while (true) {
		gpio_pin_toggle_dt(led);
		k_msleep(BLINK_INTERVAL_MS);
	}
}

K_THREAD_DEFINE(blink_tid, 512, blink_thread, &led0, NULL, NULL,
		K_LOWEST_APPLICATION_THREAD_PRIO, 0, 0);

int main(void)
{

	if (!device_is_ready(DEV_CONSOLE)) {
		printk("Console device not ready\n");
		return -1;
	}
	if (!device_is_ready(DEV_UART_P1)) {
		printk("UART P1.10 (uart135) not ready\n");
		return -1;
	}

	uart_irq_callback_user_data_set(DEV_UART_P1, uart_p1_irq_cb, NULL);
	uart_irq_rx_enable(DEV_UART_P1);

	k_thread_start(blink_tid);

	printk("UART P1.10 -> console. Connect RX to P1.10, 115200 8N1. LED blinks.\n");

	for (;;) {
		if (!forward_to_console()) {
			k_msleep(1); /* Idle: yield so blink thread runs */
		}
	}
	return 0;
}
