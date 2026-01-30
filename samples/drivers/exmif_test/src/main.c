/*
 * Copyright (c) 2016 Intel Corporation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <stdio.h>
#include <string.h>

#define SPI_FLASH_TEST_REGION_OFFSET CONFIG_SPI_FLASH_EXMIF_TEST_REGION_OFFSET

#define SPI_FLASH_SECTOR_SIZE 4096

#if DT_HAS_COMPAT_STATUS_OKAY(jedec_spi_nor)
#define SPI_FLASH_COMPAT jedec_spi_nor
#elif DT_HAS_COMPAT_STATUS_OKAY(jedec_mspi_nor)
#define SPI_FLASH_COMPAT jedec_mspi_nor
#elif DT_HAS_COMPAT_STATUS_OKAY(nordic_qspi_nor)
#define SPI_FLASH_COMPAT nordic_qspi_nor
#else
#define SPI_FLASH_COMPAT invalid
#endif

const uint8_t erased[] = {0xff, 0xff, 0xff, 0xff};

#define EXMIF_SCAN_CHUNK_SIZE     256
#define EXMIF_SCAN_BYTES_PER_LINE 16
#define EXMIF_ERASED_BYTE         0xff

static void exmif_scan_region(const struct device *flash_dev)
{
	const uint32_t offset = CONFIG_SPI_FLASH_EXMIF_TEST_REGION_OFFSET;
	const uint32_t size = CONFIG_SPI_FLASH_EXMIF_TEST_REGION_SIZE;
	uint8_t buf[EXMIF_SCAN_CHUNK_SIZE];
	uint32_t addr = offset;
	uint32_t remaining = size;

	printf("\nEXMIF scan region 0x%08x .. 0x%08x (non-erased bytes only)\n", offset,
	       offset + size - 1);
	printf("-----------------------------------------------------------\n");

	while (remaining > 0) {
		size_t to_read = remaining;
		int rc;

		if (to_read > sizeof(buf)) {
			to_read = sizeof(buf);
		}

		rc = flash_read(flash_dev, addr, buf, to_read);
		if (rc != 0) {
			printf("Flash read failed at 0x%08x: %d\n", addr, rc);
			return;
		}

		for (size_t i = 0; i < to_read; i += EXMIF_SCAN_BYTES_PER_LINE) {
			size_t line_len = to_read - i;
			bool has_non_erased = false;

			if (line_len > EXMIF_SCAN_BYTES_PER_LINE) {
				line_len = EXMIF_SCAN_BYTES_PER_LINE;
			}

			for (size_t j = 0; j < line_len; j++) {
				if (buf[i + j] != EXMIF_ERASED_BYTE) {
					has_non_erased = true;
					break;
				}
			}

			if (has_non_erased) {
				uint32_t line_addr = addr + i;
				printf("%08x:", line_addr);
				for (size_t j = 0; j < line_len; j++) {
					printf(" %02x", buf[i + j]);
				}
				for (size_t j = line_len; j < EXMIF_SCAN_BYTES_PER_LINE; j++) {
					printf("   ");
				}
				printf("  |");
				for (size_t j = 0; j < line_len; j++) {
					uint8_t b = buf[i + j];
					printf("%c", (b >= 0x20 && b < 0x7f) ? b : '.');
				}
				printf("|\n");
			}
		}

		addr += to_read;
		remaining -= to_read;
	}

	printf("-----------------------------------------------------------\n");
	printf("Done\n");
}

void exmif_erase_page(const struct device *flash_dev, size_t page)
{
	const uintptr_t page_offset = page * SPI_FLASH_SECTOR_SIZE;
	const uint8_t expected[] = {0xff, 0xff, 0xff, 0xff};
	const size_t len = sizeof(expected);
	uint8_t buf[sizeof(expected)];
	int rc;

	printf("Erasing page at 0x%lx\n", page_offset);
	rc = flash_erase(flash_dev, page_offset, SPI_FLASH_SECTOR_SIZE);
	if (rc != 0) {
		printf("Flash erase failed! %d\n", rc);
	} else {
		/* Make this better later... */
		memset(buf, 0, len);
		rc = flash_read(flash_dev, page_offset, buf, len);
		if (rc != 0) {
			printf("Flash read failed! %d\n", rc);
			return;
		}
		if (memcmp(erased, buf, len) != 0) {
			printf("Flash erase failed at offset 0x%x got 0x%x\n", page_offset,
			       *(uint32_t *)buf);
			return;
		}
	}
}

/* Run this to verify connection to exmif works ok */
void single_sector_test(const struct device *flash_dev)
{
	const uint8_t expected[] = {0x55, 0xaa, 0x66, 0x99};
	const size_t len = sizeof(expected);
	uint8_t buf[sizeof(expected)];
	int rc;

	printf("\nPerform test on single sector");
	/* Write protection needs to be disabled before each write or
	 * erase, since the flash component turns on write protection
	 * automatically after completion of write and erase
	 * operations.
	 */
	printf("\nTest 1: Flash erase\n");

	exmif_erase_page(flash_dev, SPI_FLASH_TEST_REGION_OFFSET);

	printf("\nTest 2: Flash write\n");

	printf("Attempting to write %zu bytes\n", len);
	rc = flash_write(flash_dev, SPI_FLASH_TEST_REGION_OFFSET, expected, len);
	if (rc != 0) {
		printf("Flash write failed! %d\n", rc);
		return;
	}

	memset(buf, 0, len);
	rc = flash_read(flash_dev, SPI_FLASH_TEST_REGION_OFFSET, buf, len);
	if (rc != 0) {
		printf("Flash read failed! %d\n", rc);
		return;
	}

	if (memcmp(expected, buf, len) == 0) {
		printf("Data read matches data written. Good!!\n");
	} else {
		const uint8_t *wp = expected;
		const uint8_t *rp = buf;
		const uint8_t *rpe = rp + len;

		printf("Data read does not match data written!!\n");
		while (rp < rpe) {
			printf("%08x wrote %02x read %02x %s\n",
			       (uint32_t)(SPI_FLASH_TEST_REGION_OFFSET + (rp - buf)), *wp, *rp,
			       (*rp == *wp) ? "match" : "MISMATCH");
			++rp;
			++wp;
		}
	}
}

int main(void)
{
	const struct device *flash_dev = DEVICE_DT_GET_ONE(SPI_FLASH_COMPAT);

	if (!device_is_ready(flash_dev)) {
		printk("%s: device not ready.\n", flash_dev->name);
		return 0;
	}

	printf("\n%s SPI flash testing\n", flash_dev->name);
	printf("==========================\n");

	if (IS_ENABLED(CONFIG_SPI_FLASH_SINGLE_SECTOR_TEST)) {
		single_sector_test(flash_dev);
	}

	if (IS_ENABLED(CONFIG_SPI_FLASH_ERASE_TEST_REGION)) {
		const size_t num_pages =
			CONFIG_SPI_FLASH_EXMIF_TEST_REGION_SIZE / SPI_FLASH_SECTOR_SIZE;
		for (int i = 0; i < num_pages; i++) {
			exmif_erase_page(flash_dev, i);
		}
	}

#if (CONFIG_SPI_FLASH_EXMIF_TEST_REGION_SIZE > 0)
	exmif_scan_region(flash_dev);
#endif

	return 0;
}
