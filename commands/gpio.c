/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <common.h>
#include <command.h>
#include <errno.h>
#include <gpio.h>

static int find_gpio(const char *arg)
{
	int gpio;

	gpio = gpio_find_by_label(arg);
	if (gpio_is_valid(gpio))
		return gpio;

	return simple_strtoul(arg, NULL, 0);
}

static int do_gpio_get_value(int argc, char *argv[])
{
	int gpio, value;

	if (argc < 2)
		return COMMAND_ERROR_USAGE;

	gpio = find_gpio(argv[1]);

	value = gpio_get_value(gpio);
	if (value < 0)
		return 1;

	return value;
}

BAREBOX_CMD_HELP_START(gpio_generic)
BAREBOX_CMD_HELP_TEXT("GPIO can be specified by number or label")
BAREBOX_CMD_HELP_END

BAREBOX_CMD_START(gpio_get_value)
	.cmd		= do_gpio_get_value,
	BAREBOX_CMD_DESC("return value of a GPIO pin")
	BAREBOX_CMD_OPTS("GPIO")
	BAREBOX_CMD_GROUP(CMD_GRP_HWMANIP)
	BAREBOX_CMD_HELP(cmd_gpio_generic_help)
BAREBOX_CMD_END

static int do_gpio_set_value(int argc, char *argv[])
{
	int gpio, value;

	if (argc < 3)
		return COMMAND_ERROR_USAGE;

	gpio = find_gpio(argv[1]);
	value = simple_strtoul(argv[2], NULL, 0);

	gpio_set_value(gpio, value);

	return 0;
}

BAREBOX_CMD_START(gpio_set_value)
	.cmd		= do_gpio_set_value,
	BAREBOX_CMD_DESC("set a GPIO's output value")
	BAREBOX_CMD_OPTS("GPIO VALUE")
	BAREBOX_CMD_GROUP(CMD_GRP_HWMANIP)
	BAREBOX_CMD_HELP(cmd_gpio_generic_help)
BAREBOX_CMD_END

static int do_gpio_direction_input(int argc, char *argv[])
{
	int gpio, ret;

	if (argc < 2)
		return COMMAND_ERROR_USAGE;

	gpio = find_gpio(argv[1]);

	ret = gpio_direction_input(gpio);
	if (ret)
		return 1;

	return 0;
}

BAREBOX_CMD_START(gpio_direction_input)
	.cmd		= do_gpio_direction_input,
	BAREBOX_CMD_DESC("set direction of a GPIO pin to input")
	BAREBOX_CMD_OPTS("GPIO")
	BAREBOX_CMD_GROUP(CMD_GRP_HWMANIP)
	BAREBOX_CMD_HELP(cmd_gpio_generic_help)
BAREBOX_CMD_END

static int do_gpio_direction_output(int argc, char *argv[])
{
	int gpio, value, ret;

	if (argc < 3)
		return COMMAND_ERROR_USAGE;

	gpio = find_gpio(argv[1]);
	value = simple_strtoul(argv[2], NULL, 0);

	ret = gpio_direction_output(gpio, value);
	if (ret)
		return 1;

	return 0;
}

BAREBOX_CMD_START(gpio_direction_output)
	.cmd		= do_gpio_direction_output,
	BAREBOX_CMD_DESC("set direction of a GPIO pin to output")
	BAREBOX_CMD_OPTS("GPIO VALUE")
	BAREBOX_CMD_GROUP(CMD_GRP_HWMANIP)
	BAREBOX_CMD_HELP(cmd_gpio_generic_help)
BAREBOX_CMD_END
