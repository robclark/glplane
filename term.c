/*
 *
 */

#include <termios.h>
#include <unistd.h>

#include "term.h"

static struct termios saved_termios;

void term_init(void)
{
	struct termios t;

	tcgetattr(0, &t);

	saved_termios = t;

	t.c_lflag &= ~(ICANON | ECHO | ISIG);
	t.c_cc[VMIN] = 1;
	t.c_cc[VTIME] = 0;

	tcsetattr(0, TCSANOW, &t);
}

void term_deinit(void)
{
	tcsetattr(0, TCSANOW, &saved_termios);
}
