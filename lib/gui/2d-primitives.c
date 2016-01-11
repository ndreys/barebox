#include <common.h>
#include <fb.h>
#include <gui/graphic_utils.h>
#include <linux/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <fs.h>
#include <malloc.h>

static void __illuminate(struct fb_info *info,
			 int x, int y,
			 u8 r, u8 g, u8 b, u8 a)
{
	void *pixel;

	pixel  = fb_get_screen_base(info);
	pixel += y * info->line_length + x * (info->bits_per_pixel >> 3);

	gu_set_rgba_pixel(info, pixel, r, g, b, a);
}

static void illuminate(struct fb_info *info,
		       bool invert,
		       int x, int y,
		       u8 r, u8 g, u8 b, u8 a)
{
	if (invert)
		__illuminate(info, y, x,
			     r, g, b, a);
	else
		__illuminate(info, x, y,
			     r, g, b, a);

}


static void draw_simple_line(struct screen *sc,
			     int x1, int y1,
			     int x2, int y2,
			     u8 r, u8 g, u8 b, u8 a)
{
	int x;
	bool invert = false;

	BUG_ON(x1 != x2 &&
	       y1 != y2);

	if (x1 == x2) {
		swap(x1, y1);
		swap(x2, y2);
		invert = true;
	}

	if (x1 > x2) {
		swap(x1, x2);
		swap(y1, y2);
	}

	for (x = x1; x < x2 - 1; x++)
		illuminate(sc->info,
			   invert,
			   x, y1,
			   r, g, b, a);
}

/**
 * gl_draw_line - draw a 2D line between (x1, y1) and (x2,y2)
 *
 * @info: fb_info object
 * @x1, @y1: first point defining the line
 * @x2, @y2: second point defining the line
 * @r, @g, @b, @a: line's color
 *
 * gl_draw_line() implements integer version of Bresenham's algoritm
 * as can be found here:
 *
 * http://www.idav.ucdavis.edu/education/GraphicsNotes/Bresenhams-Algorithm.pdf
 */
void gu_draw_line(struct screen *sc,
		  int x1, int y1,
		  int x2, int y2,
		  u8 r, u8 g, u8 b, u8 a)
{
	int dx;
	int dy;
	int i, j, eps;
	bool invert = false;

	BUG_ON(x1 < 0 || y1 < 0 ||
	       x2 < 0 || y2 < 0);

	if (x1 == x2 || y1 == y2) {
		draw_simple_line(sc,
				 x1, y1,
				 x2, y2,
				 r, g, b, a);
		return;
	}

	dx = abs(x2 - x1);
	dy = abs(y2 - y1);

	/*
	 * First thing we need to determine "Driving Axis", as can be
	 * seen below if Y-axis projection of the line is bigger than
	 * X-axis projection we swap axes and pretend the X is Y and
	 * vice versa
	 */
	if (dy > dx) {
		swap(x1, y1);
		swap(x2, y2);
		swap(dx, dy);
		invert = true;
	}

	/*
	 * Second, we need to make sure that we will be traversing
	 * driving axis in the direction of increment so we swap point
	 * 1 with point 2 if x1 is greater than x2
	 */
	if (x1 > x2) {
		swap(x1, x2);
		swap(y1, y2);
	}

	j   = y1;
	eps = dy - dx;

	for (i = x1; i <= x2 - 1; i++) {
		illuminate(sc->info,
			   invert,
			   j, i,
			   r, g, b, a);

		if (eps >= 0) {
			j += 1;
			eps -= dx;
		}

		eps += dy;
	}
}
