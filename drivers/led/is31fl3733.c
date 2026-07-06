/*
 * Copyright 2022-2023 Daniel DeGrasse <daniel@degrasse.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#define DT_DRV_COMPAT issi_is31fl3733

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/led.h>
#include <zephyr/kernel.h>

#include <zephyr/drivers/led/is31fl3733.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(is31fl3733, CONFIG_LED_LOG_LEVEL);

/* IS31FL3733 register definitions */
#define CMD_SEL_REG		0xFD /* Command/page selection reg */
#define CMD_SEL_LED		0x0  /* LED configuration page */
#define CMD_SEL_PWM		0x1  /* PWM configuration page */
#define CMD_SEL_ABM		0x2  /* Auto Breath Mode assignment page */
#define CMD_SEL_FUNC		0x3  /* Function configuration page */

#define CMD_LOCK_REG		0xFE /* Command selection lock reg */
#define CMD_LOCK_UNLOCK		0xC5 /* Command sel unlock value */

/* IS31FL3733 page specific register definitions */

/* Function configuration page */
#define CONF_REG		0x0 /* configuration register */
#define CONF_REG_SSD_MASK	0x1 /* Software shutdown mask */
#define CONF_REG_SSD_SHIFT	0x0 /* Software shutdown shift */
#define CONF_REG_ABM_ENABLE_MASK BIT(1) /* Auto Breath Mode enable (B_EN) */
#define CONF_REG_SYNC_SHIFT	0x6 /* Sync mode shift */
#define CONF_REG_SYNC_MASK	0xC /* Sync mode mask */

#define GLOBAL_CURRENT_CTRL_REG 0x1 /* global current control register */

#define RESET_REG		0x11 /* Reset all registers to POR state */

/*
 * Auto Breath Mode (ABM) timing registers on function page.
 * Each ABM channel occupies 4 consecutive registers:
 *   base+0: T1[7:5] (rise time index), T2[4:1] (hold-on time index)
 *   base+1: T3[7:5] (fall time index), T4[4:1] (hold-off time index)
 *   base+2: loop begin step / loop times high
 *   base+3: loop times low (0 = infinite)
 * ABM1=base 0x02, ABM2=base 0x06, ABM3=base 0x0A
 */
#define ABM_REG_BASE(n)		(0x02U + (n) * 4U)
#define ABM_TIME_UPDATE_REG	0x0EU /* write to latch new ABM timing */

/*
 * T2/T4 (hold-on/hold-off) time encoding table, ms per 4-bit index.
 * Index 0 = 0 ms (no hold). T1/T3 (rise/fall) use the same values
 * starting at index 1 (minimum 210 ms), with index 0 also = 210 ms.
 */
#define ABM_T2_TABLE_SIZE 8U
static const uint16_t abm_t2_ms[ABM_T2_TABLE_SIZE] = {
	0, 210, 420, 840, 1680, 3360, 6720, 13440
};

/* Number of hardware ABM channels */
#define IS31FL3733_ABM_COUNT 3U

/* Matrix Layout definitions */
#define IS31FL3733_ROW_COUNT 12
#define IS31FL3733_COL_COUNT 16
#define IS31FL3733_MAX_LED (IS31FL3733_ROW_COUNT * IS31FL3733_COL_COUNT)

/* Max brightness */
struct is31fl3733_config {
	struct i2c_dt_spec bus;
	struct gpio_dt_spec sdb;
	uint8_t current_limit;
	uint8_t sync;
};

struct is31fl3733_abm_state {
	uint32_t delay_on;
	uint32_t delay_off;
	uint8_t user_count;
};

struct is31fl3733_data {
	/* Active configuration page */
	uint32_t selected_page;
	/* Scratch buffer, used for bulk controller writes */
	uint8_t scratch_buf[IS31FL3733_MAX_LED + 1];
	/* LED config reg state, IS31FL3733 conf reg is write only */
	uint8_t conf_reg;
	/* ABM slot state (indexed 0–2 for ABM1–ABM3) */
	struct is31fl3733_abm_state abm[IS31FL3733_ABM_COUNT];
	/* Per-LED ABM assignment: 0=PWM mode, 1–3=ABM slot number */
	uint8_t led_abm_slot[IS31FL3733_MAX_LED];
};

/* Selects target register page for IS31FL3733. After setting the
 * target page, all I2C writes will use the selected page until the selected
 * page is changed.
 */
static int is31fl3733_select_page(const struct device *dev, uint8_t page)
{
	const struct is31fl3733_config *config = dev->config;
	struct is31fl3733_data *data = dev->data;
	int ret = 0U;

	if (data->selected_page == page) {
		/* No change necessary */
		return 0;
	}

	/* Unlock page selection register */
	ret = i2c_reg_write_byte_dt(&config->bus, CMD_LOCK_REG, CMD_LOCK_UNLOCK);
	if (ret < 0) {
		LOG_ERR("Could not unlock page selection register");
		return ret;
	}

	/* Write to function select to select active page */
	ret = i2c_reg_write_byte_dt(&config->bus, CMD_SEL_REG, page);
	if (ret < 0) {
		LOG_ERR("Could not select active page");
		return ret;
	}
	data->selected_page = page;

	return ret;
}

/* Returns the T2/T4 4-bit encoding index closest to the requested ms value. */
static uint8_t abm_nearest_t2(uint32_t ms)
{
	uint8_t best = 0U;
	uint32_t best_diff = (ms > abm_t2_ms[0]) ? (ms - abm_t2_ms[0]) : (abm_t2_ms[0] - ms);

	for (uint8_t i = 1U; i < ABM_T2_TABLE_SIZE; i++) {
		uint32_t diff = (ms > abm_t2_ms[i]) ? (ms - abm_t2_ms[i]) : (abm_t2_ms[i] - ms);

		if (diff < best_diff) {
			best_diff = diff;
			best = i;
		}
	}
	return best;
}

/* Writes ABM timing registers for slot (0-based) on the function page.
 * Uses T1=T3=0 (instant rise/fall) for a hard blink pattern.
 * T2 encodes delay_on, T4 encodes delay_off. Loop count 0 = infinite.
 * Caller must have already selected CMD_SEL_FUNC page.
 */
static int abm_write_timing(const struct device *dev, uint8_t slot,
			    uint32_t delay_on, uint32_t delay_off)
{
	const struct is31fl3733_config *config = dev->config;
	uint8_t t2_idx = abm_nearest_t2(delay_on);
	uint8_t t4_idx = abm_nearest_t2(delay_off);
	uint8_t base = ABM_REG_BASE(slot);
	int ret;

	/* T1[7:5]=0 (min 210ms rise), T2[4:1]=hold-on time (index << 1) */
	ret = i2c_reg_write_byte_dt(&config->bus, base, (uint8_t)((t2_idx & 0x7U) << 1));
	if (ret < 0) {
		return ret;
	}
	/* T3[7:5]=0 (min 210ms fall), T4[4:1]=hold-off time (index << 1) */
	ret = i2c_reg_write_byte_dt(&config->bus, base + 1U,
				    (uint8_t)((t4_idx & 0x7U) << 1));
	if (ret < 0) {
		return ret;
	}
	/* Loop begin at step 0, loop times high = 0 */
	ret = i2c_reg_write_byte_dt(&config->bus, base + 2U, 0U);
	if (ret < 0) {
		return ret;
	}
	/* Loop times low = 0 (infinite) */
	return i2c_reg_write_byte_dt(&config->bus, base + 3U, 0U);
}

static int is31fl3733_led_blink(const struct device *dev, uint32_t led,
				uint32_t delay_on, uint32_t delay_off)
{
	struct is31fl3733_data *data = dev->data;
	const struct is31fl3733_config *config = dev->config;
	uint8_t prev_slot = data->led_abm_slot[led];
	bool timing_changed;
	int ret;
	int slot = -1;

	if (led >= IS31FL3733_MAX_LED) {
		return -EINVAL;
	}

	/* Release the LED's current slot before searching so the freed slot
	 * can be considered as a candidate for reuse.
	 */
	if (prev_slot != 0U) {
		data->abm[prev_slot - 1U].user_count--;
		data->led_abm_slot[led] = 0U;
	}

	/* Find an existing slot with the same timing, or a free slot. */
	for (int i = 0; i < (int)IS31FL3733_ABM_COUNT; i++) {
		if (data->abm[i].user_count > 0U) {
			if (data->abm[i].delay_on == delay_on &&
			    data->abm[i].delay_off == delay_off) {
				slot = i;
				break;
			}
		} else if (slot < 0) {
			slot = i;
		}
	}

	if (slot < 0) {
		/* No slot available; restore the original assignment. */
		if (prev_slot != 0U) {
			data->abm[prev_slot - 1U].user_count++;
			data->led_abm_slot[led] = prev_slot;
		}
		return -EBUSY;
	}

	/* Write timing only when the slot is free and its cached timing differs.
	 * Skipping avoids restarting the breath engine when timing is unchanged.
	 */
	timing_changed = (data->abm[slot].user_count == 0U &&
			  (data->abm[slot].delay_on != delay_on ||
			   data->abm[slot].delay_off != delay_off));
	if (timing_changed) {
		ret = is31fl3733_select_page(dev, CMD_SEL_FUNC);
		if (ret < 0) {
			return ret;
		}
		ret = abm_write_timing(dev, (uint8_t)slot, delay_on, delay_off);
		if (ret < 0) {
			return ret;
		}
		data->abm[slot].delay_on = delay_on;
		data->abm[slot].delay_off = delay_off;
	}

	/* Assign this LED to the ABM slot on the ABM page.
	 * Each byte selects the ABM channel: 0=PWM, 1=ABM1, 2=ABM2, 3=ABM3.
	 */
	ret = is31fl3733_select_page(dev, CMD_SEL_ABM);
	if (ret < 0) {
		return ret;
	}
	ret = i2c_reg_write_byte_dt(&config->bus, (uint8_t)led,
				    (uint8_t)(slot + 1));
	if (ret < 0) {
		return ret;
	}

	/* Enable ABM globally in the configuration register if not already set. */
	ret = is31fl3733_select_page(dev, CMD_SEL_FUNC);
	if (ret < 0) {
		return ret;
	}
	if (!(data->conf_reg & CONF_REG_ABM_ENABLE_MASK)) {
		data->conf_reg |= CONF_REG_ABM_ENABLE_MASK;
		ret = i2c_reg_write_byte_dt(&config->bus, CONF_REG, data->conf_reg);
		if (ret < 0) {
			return ret;
		}
	}

	/* Latch new timing into the breath engine. Only needed when timing was
	 * actually written; skipping avoids a mid-cycle restart for unchanged
	 * timing. This write MUST happen after B_EN is enabled (IS31FL3733
	 * datasheet).
	 */
	if (timing_changed) {
		ret = i2c_reg_write_byte_dt(&config->bus, ABM_TIME_UPDATE_REG, 0U);
		if (ret < 0) {
			return ret;
		}
	}

	/* Update software state only after all hardware writes succeed. */
	data->led_abm_slot[led] = (uint8_t)(slot + 1);
	data->abm[slot].user_count++;

	return 0;
}

static int is31fl3733_led_set_brightness(const struct device *dev, uint32_t led, uint8_t value)
{
	const struct is31fl3733_config *config = dev->config;
	struct is31fl3733_data *data = dev->data;
	int ret;
	uint8_t led_brightness = (uint8_t)(((uint32_t)value * 255) / LED_BRIGHTNESS_MAX);

	if (led >= IS31FL3733_MAX_LED) {
		return -EINVAL;
	}

	/* If this LED is in ABM mode, switch it back to PWM mode first. */
	uint8_t prev_slot = data->led_abm_slot[led];

	if (prev_slot != 0U) {
		ret = is31fl3733_select_page(dev, CMD_SEL_ABM);
		if (ret < 0) {
			return ret;
		}
		ret = i2c_reg_write_byte_dt(&config->bus, (uint8_t)led, 0U);
		if (ret < 0) {
			return ret;
		}
		data->abm[prev_slot - 1U].user_count--;
		data->led_abm_slot[led] = 0U;

		/* Disable ABM globally when no slot has any users left. */
		if (data->conf_reg & CONF_REG_ABM_ENABLE_MASK) {
			bool any_active = false;

			for (int i = 0; i < (int)IS31FL3733_ABM_COUNT; i++) {
				if (data->abm[i].user_count > 0U) {
					any_active = true;
					break;
				}
			}
			if (!any_active) {
				data->conf_reg &= ~CONF_REG_ABM_ENABLE_MASK;
				ret = is31fl3733_select_page(dev, CMD_SEL_FUNC);
				if (ret < 0) {
					return ret;
				}
				ret = i2c_reg_write_byte_dt(&config->bus, CONF_REG,
							    data->conf_reg);
				if (ret < 0) {
					return ret;
				}
			}
		}
	}

	/* Configure PWM mode */
	ret = is31fl3733_select_page(dev, CMD_SEL_PWM);
	if (ret < 0) {
		return ret;
	}

	return i2c_reg_write_byte_dt(&config->bus, led, led_brightness);
}

static int is31fl3733_led_write_channels(const struct device *dev, uint32_t start_channel,
					 uint32_t num_channels, const uint8_t *buf)
{
	const struct is31fl3733_config *config = dev->config;
	struct is31fl3733_data *data = dev->data;
	int ret = 0U;
	uint8_t *pwm_start;

	if ((start_channel + num_channels) > IS31FL3733_MAX_LED) {
		return -EINVAL;
	}
	pwm_start = data->scratch_buf + start_channel;
	/* Set PWM and LED target registers as first byte of each transfer */
	*pwm_start = start_channel;
	memcpy((pwm_start + 1), buf, num_channels);

	/* Write LED PWM states */
	ret = is31fl3733_select_page(dev, CMD_SEL_PWM);
	if (ret < 0) {
		return ret;
	}
	LOG_HEXDUMP_DBG(pwm_start, (num_channels + 1), "PWM states");

	return i2c_write_dt(&config->bus, pwm_start, num_channels + 1);
}

static int is31fl3733_init(const struct device *dev)
{
	const struct is31fl3733_config *config = dev->config;
	struct is31fl3733_data *data = dev->data;
	int ret = 0U;
	uint8_t dummy;

	if (!i2c_is_ready_dt(&config->bus)) {
		LOG_ERR("I2C device not ready");
		return -ENODEV;
	}
	if (config->sdb.port != NULL) {
		if (!gpio_is_ready_dt(&config->sdb)) {
			LOG_ERR("GPIO SDB pin not ready");
			return -ENODEV;
		}
		/* Set SDB pin high to exit hardware shutdown */
		ret = gpio_pin_configure_dt(&config->sdb, GPIO_OUTPUT_ACTIVE);
		if (ret < 0) {
			return ret;
		}
	}

	ret = is31fl3733_select_page(dev, CMD_SEL_FUNC);
	if (ret < 0) {
		return ret;
	}
	/*
	 * read reset reg to reset all registers to POR state,
	 * in case we are booting from a warm reset.
	 */
	ret = i2c_reg_read_byte_dt(&config->bus, RESET_REG, &dummy);
	if (ret < 0) {
		return ret;
	}

	/* Select function page after LED controller reset */
	ret = is31fl3733_select_page(dev, CMD_SEL_FUNC);
	if (ret < 0) {
		return ret;
	}
	/* Set global current control register based off devicetree value */
	ret = i2c_reg_write_byte_dt(&config->bus, GLOBAL_CURRENT_CTRL_REG,
				config->current_limit);
	if (ret < 0) {
		return ret;
	}
	/* As a final step, we exit software shutdown, disabling display
	 * blanking. We also set the LED controller sync mode here.
	 */
	data->conf_reg = (config->sync << CONF_REG_SYNC_SHIFT) | CONF_REG_SSD_MASK;
	ret = i2c_reg_write_byte_dt(&config->bus, CONF_REG, data->conf_reg);
	if (ret < 0) {
		return ret;
	}

	/* Enable all LEDs. We only control LED brightness in this driver. */
	data->scratch_buf[0] = 0x0;
	memset(data->scratch_buf + 1, 0xFF, (IS31FL3733_MAX_LED / 8));
	ret = is31fl3733_select_page(dev, CMD_SEL_LED);
	if (ret < 0) {
		return ret;
	}

	return i2c_write_dt(&config->bus, data->scratch_buf,
			(IS31FL3733_MAX_LED / 8) + 1);
}

/* Custom IS31FL3733 specific APIs */

/**
 * @brief Blanks IS31FL3733 LED display.
 *
 * When blank_en is set, the LED display will be disabled. This can be used for
 * flicker-free display updates, or power saving.
 *
 * @param dev: LED device structure
 * @param blank_en: should blanking be enabled
 * @return 0 on success, or negative value on error.
 */
int is31fl3733_blank(const struct device *dev, bool blank_en)
{
	const struct is31fl3733_config *config = dev->config;
	struct is31fl3733_data *data = dev->data;
	int ret;

	ret = is31fl3733_select_page(dev, CMD_SEL_FUNC);
	if (ret < 0) {
		return ret;
	}

	if (blank_en) {
		data->conf_reg &= ~CONF_REG_SSD_MASK;
	} else {
		data->conf_reg |= CONF_REG_SSD_MASK;
	}

	return i2c_reg_write_byte_dt(&config->bus, CONF_REG, data->conf_reg);
}

/**
 * @brief Sets led current limit
 *
 * Sets the current limit for the LED driver. This is a separate value
 * from per-led brightness, and applies to all LEDs.
 * This value sets the output current limit according
 * to the following formula: (840/R_ISET) * (limit/256)
 * See table 14 of the datasheet for additional details.
 * @param dev: LED device structure
 * @param limit: current limit to apply
 * @return 0 on success, or negative value on error.
 */
int is31fl3733_current_limit(const struct device *dev, uint8_t limit)
{
	const struct is31fl3733_config *config = dev->config;
	int ret;

	ret = is31fl3733_select_page(dev, CMD_SEL_FUNC);
	if (ret < 0) {
		return ret;
	}

	/* Set global current control register */
	return i2c_reg_write_byte_dt(&config->bus, GLOBAL_CURRENT_CTRL_REG, limit);
}

static DEVICE_API(led, is31fl3733_api) = {
	.set_brightness = is31fl3733_led_set_brightness,
	.write_channels = is31fl3733_led_write_channels,
	.blink = is31fl3733_led_blink,
};

#define IS31FL3733_DEVICE(n)                                                                       \
	static const struct is31fl3733_config is31fl3733_config_##n = {                            \
		.bus = I2C_DT_SPEC_INST_GET(n),                                                    \
		.sdb = GPIO_DT_SPEC_INST_GET_OR(n, sdb_gpios, {}),                                 \
		.current_limit = DT_INST_PROP(n, current_limit),                                   \
		.sync = DT_INST_ENUM_IDX(n, sync_mode),						   \
	};                                                                                         \
                                                                                                   \
	static struct is31fl3733_data is31fl3733_data_##n = {                                      \
		.selected_page = CMD_SEL_LED,                                                      \
	};                                                                                         \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(n, &is31fl3733_init, NULL, &is31fl3733_data_##n,                     \
			      &is31fl3733_config_##n, POST_KERNEL, CONFIG_LED_INIT_PRIORITY,       \
			      &is31fl3733_api);

DT_INST_FOREACH_STATUS_OKAY(IS31FL3733_DEVICE)
