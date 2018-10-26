#include <common.h>
#include <command.h>
#include <complete.h>
#include <getopt.h>
#include <fb.h>
#include <gui/graphic_utils.h>
#include <gui/2d-primitives.h>


#define FRAME_SIZE	8

struct placement {
	int x, y;
};

typedef struct placement placement_function_t (struct screen *, int, int);

struct placement
placement_center(struct screen *sc, int text_width, int text_height)
{
	struct placement p;

	p.x = (sc->info->xres - text_width) / 2;
	p.y = (sc->info->yres - text_height) / 2;

	return p;
}

struct placement
placement_upper_left(struct screen *sc, int text_width, int text_height)
{
	struct placement p;

	p.x = FRAME_SIZE;
	p.y = FRAME_SIZE;

	return p;
}

struct placement
placement_upper_right(struct screen *sc, int text_width, int text_height)
{
	struct placement p;

	p.x = sc->info->xres - 1 - text_width - FRAME_SIZE;
	p.y = FRAME_SIZE;

	return p;
}

struct placement
placement_lower_left(struct screen *sc, int text_width, int text_height)
{
	struct placement p;

	p.x = FRAME_SIZE;
	p.y = sc->info->yres - 1 - text_height - FRAME_SIZE;

	return p;
}

struct placement
placement_lower_right(struct screen *sc, int text_width, int text_height)
{
	struct placement p;

	p.x = sc->info->xres - 1 - text_width  - FRAME_SIZE;
	p.y = sc->info->yres - 1 - text_height - FRAME_SIZE;

	return p;
}

static int do_textbox(int argc, char *argv[])
{
	const struct font_desc *font;
	const char *fbdev = "/dev/fb0";
	const char *string = version_string;
	struct screen *sc;
	u8 fg[3], bg[3];
	int text_width;
	int text_height;

	placement_function_t *place;
	struct placement placement;
	const struct {
		const char *name;
		placement_function_t *func;
	} placements[] = {
		{ "center",      placement_center      },
		{ "upper-left",  placement_upper_left  },
		{ "lower-left",  placement_lower_left  },
		{ "upper-right", placement_upper_right },
		{ "lower-right", placement_lower_right  },
	};
	u32 color;
	size_t i;
	int opt;
	u8 *c;

	place = placement_center;

	memset(fg, 0xff, sizeof(fg)); /* default to white */
	memset(bg, 0x00, sizeof(bg)); /* default to black */

	while((opt = getopt(argc, argv, "d:f:b:p:")) > 0) {
		switch(opt) {
		case 'd':
			fbdev = optarg;
			break;
		case 'f':
			/* FALLTHROUGH */
		case 'b':
			c     = (opt == 'f') ? fg : bg;
			color = simple_strtoul(optarg, NULL, 16);

			c[0] = color >> 16;
			c[1] = color >>  8;
			c[2] = color >>  0;
			break;
		case 'p':
			place = NULL;
			for (i = 0; i < ARRAY_SIZE(placements); i++)
				if (!strcmp(optarg, placements[i].name))
					place = placements[i].func;

			if (!place) {
				pr_err("Unknown placement: %s\n",
				       optarg);
				return -EINVAL;
			}
			break;
		default:
			return COMMAND_ERROR_USAGE;
		}
	}

	if (optind < argc)
		string = argv[optind];

	font = find_font_enum(0);
	if (!font) {
		pr_err("Couldn't find any suitable fonts to use\n");
		return -ENOENT;
	}

	sc = fb_open(fbdev);
	if (IS_ERR(sc)) {
		perror("unable to open framebuffer device");
		return PTR_ERR(sc);
	}

	text_width  = strlen(string) * font->width;
	text_height = font->height;

	placement = place(sc, text_width, text_height);
	/*
	 * Draw a small frame around the version text
	 */
	gu_fill_rectangle(sc,
			  placement.x - FRAME_SIZE,
			  placement.y - FRAME_SIZE,
			  placement.x + text_width  + FRAME_SIZE,
			  placement.y + text_height + FRAME_SIZE,
			  bg[0], bg[1], bg[2], 0xff);
	/*
	 * Then render the version string on the screen
	 */
	gu_draw_text(sc, font,
		     placement.x, placement.y, string,
		     fg[0], fg[1], fg[2], 0xff, bg[0], bg[1], bg[2], 0xff);

	gu_screen_blit(sc);
	fb_close(sc);

	return 0;
}

BAREBOX_CMD_HELP_START(textbox)
BAREBOX_CMD_HELP_TEXT("This command puts a text box to a framebuffer")
BAREBOX_CMD_HELP_TEXT("")
BAREBOX_CMD_HELP_TEXT("Options:")
BAREBOX_CMD_HELP_OPT ("-d <fbdev>\t", "framebuffer device")
BAREBOX_CMD_HELP_OPT ("-f color\t", "foreground color, in hex RRGGBB format")
BAREBOX_CMD_HELP_OPT ("-b color\t", "background color, in hex RRGGBB format")
BAREBOX_CMD_HELP_OPT ("-p placement\t",
		      "version info placement (center, upper-left, "
		      "lower-left, upper-right, lower-right)")
BAREBOX_CMD_HELP_END

BAREBOX_CMD_START(textbox)
	.cmd		= do_textbox,
	BAREBOX_CMD_DESC("print barebox version")
	BAREBOX_CMD_OPTS("-dfbp")
	BAREBOX_CMD_GROUP(CMD_GRP_INFO)
	BAREBOX_CMD_COMPLETE(empty_complete)
	BAREBOX_CMD_HELP(cmd_textbox_help)
BAREBOX_CMD_END