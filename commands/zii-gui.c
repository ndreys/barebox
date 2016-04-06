#include <common.h>
#include <command.h>
#include <errno.h>
#include <malloc.h>
#include <getopt.h>
#include <fb.h>
#include <gui/graphic_utils.h>

#define COLORS(c)	(c >> 24) & 0xff, (c >> 16) & 0xff, (c >> 8) & 0xff, c & 0xff

/*
 * gui_draw_box 0x12345678 0 0 100 100
 */
static int do_gui_draw_box(int argc, char *argv[])
{
	struct screen *sc;
	void *buf;
	//int ret = 0;
	int x1, y1, x2, y2;
	u32 color;
	char *fbdev = "/dev/fb0";

	if (argc != 6)
		return COMMAND_ERROR_USAGE;

	color = simple_strtol(argv[1], NULL, 16);
	x1 = simple_strtol(argv[2], NULL, 10);
	y1 = simple_strtol(argv[3], NULL, 10);
	x2 = simple_strtol(argv[4], NULL, 10);
	y2 = simple_strtol(argv[5], NULL, 10);

	sc = fb_open(fbdev);
	if (IS_ERR(sc)) {
		perror("fd_open");
		return PTR_ERR(sc);
	}
	buf = gui_screen_render_buffer(sc);

	gu_fill_frame(sc->info, buf,
		x1, y1, x2, y2,
		COLORS(color));

	gu_screen_blit(sc);

	fb_close(sc);

	return 0;
}

BAREBOX_CMD_HELP_START(gui_draw_box)
BAREBOX_CMD_HELP_TEXT("This command draws colored rectangle")
BAREBOX_CMD_HELP_TEXT("")
BAREBOX_CMD_HELP_OPT ("color", "RGBA (Reg Green Blue Alpha)")
BAREBOX_CMD_HELP_OPT ("x y", "left top corner position")
BAREBOX_CMD_HELP_OPT ("x y", "right bottom corner position")
BAREBOX_CMD_HELP_END

BAREBOX_CMD_START(gui_draw_box)
	.cmd		= do_gui_draw_box,
	BAREBOX_CMD_DESC("draw colored rectangle")
	BAREBOX_CMD_OPTS("color x y x y")
	BAREBOX_CMD_GROUP(CMD_GRP_CONSOLE)
	BAREBOX_CMD_HELP(cmd_gui_draw_box_help)
BAREBOX_CMD_END


#ifdef CONFIG_FONTS
/*
 * gui_put_string 0x12345678 0x87654321 0 0 test 42
 */
static int do_gui_put_string(int argc, char *argv[])
{
	struct screen *sc;
	void *buf;
	//int ret = 0;
	int x, y, h;
	u32 color;
	u32 bcolor;
	const struct font_desc *font;
	char *fbdev = "/dev/fb0";

	if (argc != 7)
		return COMMAND_ERROR_USAGE;

	color = simple_strtol(argv[1], NULL, 16);
	bcolor = simple_strtol(argv[2], NULL, 16);
	x = simple_strtol(argv[3], NULL, 10);
	y = simple_strtol(argv[4], NULL, 10);
	h = simple_strtol(argv[6], NULL, 10);
	font = find_font_height(h);
	if (!font)
		return -EINVAL;

	sc = fb_open(fbdev);
	if (IS_ERR(sc)) {
		perror("fd_open");
		return PTR_ERR(sc);
	}

	buf = gui_screen_render_buffer(sc);

	gu_put_string(sc->info, buf,
		font,
		x, y,
		argv[5],
		COLORS(color),
		COLORS(bcolor));

	gu_screen_blit(sc);

	fb_close(sc);

	return 0;
}

BAREBOX_CMD_HELP_START(gui_put_string)
BAREBOX_CMD_HELP_TEXT("draw string")
BAREBOX_CMD_HELP_TEXT("")
BAREBOX_CMD_HELP_OPT ("font color", "RGBA (Reg Green Blue Alpha)")
BAREBOX_CMD_HELP_OPT ("bg color", "RGBA (Reg Green Blue Alpha)")
BAREBOX_CMD_HELP_OPT ("x y", "left top corner position")
BAREBOX_CMD_HELP_OPT ("str", "test string")
BAREBOX_CMD_HELP_OPT ("h", "font size ("
#ifdef CONFIG_FONT_32x53
"53, "
#endif
#ifdef CONFIG_FONT_24x40
"40, "
#endif
#ifdef CONFIG_FONT_22x36
"36, "
#endif
#ifdef CONFIG_FONT_16x26
"26, "
#endif
")")

BAREBOX_CMD_HELP_END

BAREBOX_CMD_START(gui_put_string)
	.cmd		= do_gui_put_string,
	BAREBOX_CMD_DESC("draw string")
	BAREBOX_CMD_OPTS("fcolor bcolor x y str h")
	BAREBOX_CMD_GROUP(CMD_GRP_CONSOLE)
	BAREBOX_CMD_HELP(cmd_gui_put_string_help)
BAREBOX_CMD_END
#endif