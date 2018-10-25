// SPDX-License-Identifier: GPL-2.0+

/*
 * Driver for LEDs connected to RAVE SP device, found on Zodiac
 * Inflight Innovations platforms
 *
 * Copyright (C) 2018 Zodiac Inflight Innovations
 */

#include <common.h>
#include <init.h>
#include <led.h>
#include <malloc.h>
#include <i2c/i2c.h>
#include <of_device.h>

#include <linux/bitfield.h>
#include <linux/mfd/rave-sp.h>

enum rave_sp_leds_source {
	RAVE_SP_LEDS_FRONT_PANEL,
	RAVE_SP_LEDS_MOOD,
	RAVE_SP_LEDS_READING,
	RAVE_SP_LEDS_NUM
};

enum rave_sp_leds_color {
	RAVE_SP_LEDS_COLOR_RED,
	RAVE_SP_LEDS_COLOR_GREEN,
	RAVE_SP_LEDS_COLOR_BLUE,
	RAVE_SP_LEDS_COLOR_NUM,

	/*
	 * "Legacy" LED configuration doesn't really group LEDs by
	 * color and instead each individual LED represents different
	 * indicator. However treating those LEDs as different colors
	 * allows us to integrate "legacy" LED handling within the
	 * rest of the driver in a relatively easy way
	 */
	RAVE_SP_LEDS_COLOR_POWER = RAVE_SP_LEDS_COLOR_RED,
	RAVE_SP_LEDS_COLOR_CONNECTOR = RAVE_SP_LEDS_COLOR_GREEN,
};

enum rave_sp_leds_request {
	RAVE_SP_LEDS_GET = 0,
	RAVE_SP_LEDS_SET = 1,
};

enum {
	RAVE_SP_LEDS_POWER_STATE	= GENMASK(2, 0),
	RAVE_SP_LEDS_POWER_ON		= 0x2,
	RAVE_SP_LEDS_CONNECTOR_ON	= BIT(4),
};

struct rave_sp_leds;
struct rave_sp_leds_type {
	int (*colors_set) (struct rave_sp_leds *, u8 []);
	int (*colors_get) (struct rave_sp_leds *, u8 []);
	u8 max_brightness;
};

struct rave_sp_led {
	struct led led;
	struct rave_sp_leds *priv;
};

#define to_rave_sp_led(ptr) container_of(ptr, struct rave_sp_led, led)

struct rave_sp_leds {
	struct rave_sp_led colors[RAVE_SP_LEDS_COLOR_NUM];
	const struct rave_sp_leds_type *type;
	struct rave_sp *sp;
	u32 source;
};

static enum rave_sp_leds_color
rave_sp_leds_get_color(struct rave_sp_leds *sp_led, struct led *color)
{
	/*
	 * Pointer arithmetic result will be color's index
	 */
	return to_rave_sp_led(color) - sp_led->colors;
}

static int rave_sp_leds_cmd_led_control(struct rave_sp_leds *sp_led,
					enum rave_sp_leds_request request,
					u8 colors[])
{
	u8 cmd[] = {
		[0]  = RAVE_SP_CMD_LED_CONTROL,
		[1]  = 0,
		[2]  = request,
		[3]  = sp_led->source,
		[4]  = true,
		[5]  = 0,
		[6]  = 0,
		/*
		 * Internally firmware will scale all of the colors by
		 * factor fof brightness / 255, so we set brightness
		 * to 255 in order to get 1:1 mapping
		 */
		[7]  = 255,
		[8]  = colors[RAVE_SP_LEDS_COLOR_RED],
		[9]  = colors[RAVE_SP_LEDS_COLOR_GREEN],
		[10] = colors[RAVE_SP_LEDS_COLOR_BLUE],
	};
	struct  {
		u8 on;
		u8 brightness;
		u8 colors[RAVE_SP_LEDS_COLOR_NUM];
	} __packed response;
	int ret;

	ret = rave_sp_exec(sp_led->sp, cmd, sizeof(cmd), &response,
			   sizeof(response));
	if (ret)
		return ret;

	if (response.on)
		memcpy(colors, &response.colors,
		       sizeof(response.colors));
	else
		memset(colors, 0, sizeof(response.colors));

	return 0;
}

static int rave_sp_leds_new_colors_get(struct rave_sp_leds *sp_led,
				     u8 colors[])
{
	return rave_sp_leds_cmd_led_control(sp_led, RAVE_SP_LEDS_GET,
					    colors);
}

static int rave_sp_leds_new_colors_set(struct rave_sp_leds *sp_led,
				     u8 colors[])
{
	return rave_sp_leds_cmd_led_control(sp_led, RAVE_SP_LEDS_SET,
					    colors);
}

static int rave_sp_leds_legacy_colors_get(struct rave_sp_leds *sp_led,
					  u8 colors[])
{
	struct rave_sp_status status;
	int ret;

	ret = rave_sp_get_status(sp_led->sp, &status);
	if (ret)
		return ret;

	colors[RAVE_SP_LEDS_COLOR_POWER] =
		!!FIELD_GET(RAVE_SP_LEDS_POWER_STATE, status.power_led_status);
	colors[RAVE_SP_LEDS_COLOR_POWER] = FIELD_GET(RAVE_SP_LEDS_CONNECTOR_ON,
						     status.power_led_status);
	return 0;
}

static u8 rave_sp_leds_legacy_colors_to_state(const u8 colors[])
{
	u8 state;

	state = colors[RAVE_SP_LEDS_COLOR_POWER] ? RAVE_SP_LEDS_POWER_ON : 0;

	if (colors[RAVE_SP_LEDS_COLOR_CONNECTOR])
		state |= RAVE_SP_LEDS_CONNECTOR_ON;

	return state;
}

static int rave_sp_leds_legacy_colors_set(struct rave_sp_leds *sp_led,
					  u8 colors[])
{
	u8 cmd[] = {
		[0]  = RAVE_SP_CMD_PWR_LED,
		[1]  = 0,
		[2]  = rave_sp_leds_legacy_colors_to_state(colors),
	};

	return rave_sp_exec(sp_led->sp, cmd, sizeof(cmd), NULL, 0);
}

static void rave_sp_leds_set(struct led *led, unsigned int value)
{
	struct rave_sp_leds *sp_led = to_rave_sp_led(led)->priv;
	enum rave_sp_leds_color c = rave_sp_leds_get_color(sp_led, led);
	u8 colors[RAVE_SP_LEDS_COLOR_NUM];
	int ret;

	ret = sp_led->type->colors_get(sp_led, colors);
	if (ret)
		return;

	colors[c] = value;

	sp_led->type->colors_set(sp_led, colors);
}

static int rave_sp_leds_probe(struct device_d *dev)
{
	struct rave_sp *sp = dev->parent->priv;
	struct device_node *np = dev->device_node;
	u8 colors[RAVE_SP_LEDS_COLOR_NUM] = {0};
	struct rave_sp_leds *sp_led;
	struct device_node *child;
	int ret;

	sp_led = xzalloc(sizeof(*sp_led));
	sp_led->sp = sp;
	sp_led->type = of_device_get_match_data(dev);
	dev->priv = sp_led;

	ret = of_property_read_u32(np, "led-sources", &sp_led->source);
	if (ret || sp_led->source >= RAVE_SP_LEDS_NUM) {
		dev_err(dev, "Invalid or missing 'led-sources'\n");
		return -EINVAL;
	}

	for_each_child_of_node(np, child) {
		struct led *led;
		u32 reg;

		ret = of_property_read_u32(child, "reg", &reg);
		if (ret || reg >= RAVE_SP_LEDS_COLOR_NUM) {
			dev_err(dev, "Failed to read 'reg'\n");
			continue;
		}

		sp_led->colors[reg].priv = sp_led;
		led = &sp_led->colors[reg].led;

		if (of_property_read_string(child, "label",
					    (const char **)&led->name)) {
			dev_err(dev, "Failed to read 'label'\n");
			continue;
		}

		led->set = rave_sp_leds_set;
		led->max_value = sp_led->type->max_brightness;

		ret = led_register(led);
		if (ret) {
			dev_err(dev, "Could not register %s\n", led->name);
			return ret;
		}

		led_of_parse_trigger(led, child);
	}
	/*
	 * RAVE SP firmware leaves one of LEDs in "blinking" state, so
	 * we make sure to turn all of them off as the last step of
	 * probing.
	 */
	ret = sp_led->type->colors_set(sp_led, colors);
	if (ret)
		dev_warn(dev, "Failed to turn off all LEDs\n");

	return 0;
}

const struct rave_sp_leds_type rave_sp_leds_type_new = {
	.colors_get = rave_sp_leds_new_colors_get,
	.colors_set = rave_sp_leds_new_colors_set,
	.max_brightness = LED_FULL,
};

const struct rave_sp_leds_type rave_sp_leds_type_legacy = {
	.colors_get = rave_sp_leds_legacy_colors_get,
	.colors_set = rave_sp_leds_legacy_colors_set,
	.max_brightness = LED_ON,
};

static const struct of_device_id rave_sp_leds_of_match[] = {
	{
		.compatible = "zii,rave-sp-leds",
		.data = &rave_sp_leds_type_new,
	},
	{
		.compatible = "zii,rave-sp-leds-legacy",
		.data = &rave_sp_leds_type_legacy,
	},
	{ /* sentinel */ }
};


static struct driver_d led_rave_sp_driver = {
	.name	= "led-rave-sp",
	.probe	= rave_sp_leds_probe,
	.of_compatible = DRV_OF_COMPAT(rave_sp_leds_of_match),
};
console_platform_driver(led_rave_sp_driver);
