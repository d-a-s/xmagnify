#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xfixes.h>
#include <X11/keysym.h>

#define DEFAULT_ZOOM_LEVEL 2
#define DEFAULT_WINDOW_SIZE 600

Display *display;
Window zoom_window;
int screen;
int screen_width, screen_height;
int running = 1;
int zoom_level = DEFAULT_ZOOM_LEVEL;
int default_zoom_level;
float current_zoom = DEFAULT_ZOOM_LEVEL;
int window_size = DEFAULT_WINDOW_SIZE;
int window_x;
int window_y;
int show_crosshairs = 0;
int window_opacity = -1;
int always_on_top = 0;
int borderless = 0;

void print_usage(const char *program_name) {
	printf("Usage: %s [OPTIONS]\n", program_name);
	printf("Options:\n");
	printf("  -z, --zoom LEVEL    Zoom level (default: %d)\n", DEFAULT_ZOOM_LEVEL);
	printf("  -s, --size SIZE     Window size in pixels (default: %d)\n", DEFAULT_WINDOW_SIZE);
	printf("  -c, --crosshairs    Show crosshairs at cursor location\n");
	printf("  -o, --opacity PERC  Window opacity 0-100 (requires compositor)\n");
	printf("  -t, --top           Always on top\n");
	printf("  -b, --borderless    Hide window border\n");
	printf("  -h, --help          Show this help message\n");
	printf("  -q, --quit          Quit the application\n");
}

void parse_arguments(int argc, char *argv[]) {
	int opt;
	const char *short_options = "z:s:cho:tbq";
	struct option long_options[] = {
		{"zoom", required_argument, 0, 'z'},
		{"size", required_argument, 0, 's'},
		{"crosshairs", no_argument, 0, 'c'},
		{"opacity", required_argument, 0, 'o'},
		{"top", no_argument, 0, 't'},
		{"borderless", no_argument, 0, 'b'},
		{"help", no_argument, 0, 'h'},
		{"quit", no_argument, 0, 'q'},
		{0, 0, 0, 0}
	};

	while ((opt = getopt_long(argc, argv, short_options, long_options, NULL)) != -1) {
		switch (opt) {
			case 'z':
				zoom_level = atoi(optarg);
				default_zoom_level = zoom_level;
				current_zoom = zoom_level;
				if (zoom_level <= 0) {
					fprintf(stderr, "Error: Zoom level must be positive\n");
					exit(1);
				}
				break;
			case 's':
				window_size = atoi(optarg);
				if (window_size <= 0) {
					fprintf(stderr, "Error: Window size must be positive\n");
					exit(1);
				}
				break;
			case 'c':
				show_crosshairs = 1;
				break;
			case 'o':
				window_opacity = atoi(optarg);
				if (window_opacity < 0 || window_opacity > 100) {
					fprintf(stderr, "Error: Opacity must be 0-100\n");
					exit(1);
				}
				break;
			case 't':
				always_on_top = 1;
				break;
			case 'b':
				borderless = 1;
				break;
			case 'h':
				print_usage(argv[0]);
				exit(0);
			case 'q':
				exit(0);
			default:
				print_usage(argv[0]);
				exit(1);
		}
	}
}

void init_x11() {
	display = XOpenDisplay(NULL);
	if (!display) {
		fprintf(stderr, "Cannot open X display\n");
		exit(1);
	}
	screen = DefaultScreen(display);
	screen_width = DisplayWidth(display, screen);
	screen_height = DisplayHeight(display, screen);
}

typedef struct {
	unsigned long flags;
	unsigned long functions;
	unsigned long decorations;
	long input_mode;
	unsigned long status;
} MWMHints;

#define MWM_HINTS_FUNCTIONS   (1L << 0)
#define MWM_HINTS_DECORATIONS (1L << 1)
#define MWM_DECOR_NONE        (0L)

void set_motif_borderless() {
	Atom motif_hints = XInternAtom(display, "_MOTIF_WM_HINTS", False);
	MWMHints hints = {
		.flags = MWM_HINTS_DECORATIONS,
		.functions = 0,
		.decorations = MWM_DECOR_NONE,
		.input_mode = 0,
		.status = 0
	};
	XChangeProperty(
		display, zoom_window,
		motif_hints, motif_hints, 32,
		PropModeReplace,
		(unsigned char *)&hints, 5
	);
}

void create_zoom_window() {
	window_x = (screen_width - window_size) / 2;
	window_y = (screen_height - window_size) / 2;

	zoom_window = XCreateSimpleWindow(
		display,
		RootWindow(display, screen),
		window_x, window_y,
		window_size, window_size,
		1,
		BlackPixel(display, screen),
		WhitePixel(display, screen)
	);

	if (borderless) {
		set_motif_borderless();
	}

	XStoreName(display, zoom_window, "Xmagnify");
	XSelectInput(display, zoom_window, KeyPressMask);

	if (always_on_top) {
		Atom wm_state = XInternAtom(display, "_NET_WM_STATE", False);
		Atom wm_above = XInternAtom(display, "_NET_WM_STATE_ABOVE", False);
		XChangeProperty(
			display, zoom_window,
			wm_state, XA_ATOM, 32,
			PropModeReplace,
			(unsigned char *)&wm_above, 1
		);
	}

	if (window_opacity >= 0) {
		Atom opacity_atom = XInternAtom(display, "_NET_WM_WINDOW_OPACITY", False);
		unsigned int opacity = (unsigned int)((window_opacity / 100.0) * 0xFFFFFFFF);
		XChangeProperty(
			display, zoom_window,
			opacity_atom, XA_CARDINAL, 32,
			PropModeReplace,
			(unsigned char *)&opacity, 1
		);
	}

	XMapWindow(display, zoom_window);
}

void clamp_coordinates(int *x, int *y, int width, int height) {
	*x = (*x < 0) ? 0 : *x;
	*y = (*y < 0) ? 0 : *y;
	*x = (*x > screen_width - width) ? screen_width - width : *x;
	*y = (*y > screen_height - height) ? screen_height - height : *y;
}

void move_window(int dx, int dy) {
	window_x += dx;
	window_y += dy;
	clamp_coordinates(&window_x, &window_y, window_size, window_size);
	XMoveWindow(display, zoom_window, window_x, window_y);
}

void handle_keypress(XEvent *event) {
	KeySym keysym = XLookupKeysym(&event->xkey, 0);
	if (keysym == XK_Escape || keysym == XK_q || keysym == XK_Q) {
		running = 0;
	} else if (keysym == XK_equal || keysym == XK_plus) {
		current_zoom += 0.5;
	} else if (keysym == XK_minus) {
		if (current_zoom > 0.5) {
			current_zoom -= 0.5;
		}
	} else if (keysym == XK_0) {
		current_zoom = default_zoom_level;
	} else if (keysym == XK_Left) {
		move_window(-window_size, 0);
	} else if (keysym == XK_Right) {
		move_window(window_size, 0);
	} else if (keysym == XK_Up) {
		move_window(0, -window_size);
	} else if (keysym == XK_Down) {
		move_window(0, window_size);
	}
}

void update_zoom() {
	XFixesCursorImage *cursor = XFixesGetCursorImage(display);
	if (!cursor) {
		fprintf(stderr, "Failed to get cursor position\n");
		return;
	}

	int capture_width = window_size / current_zoom;
	int capture_height = window_size / current_zoom;
	int capture_x = cursor->x - capture_width/2;
	int capture_y = cursor->y - capture_height/2;

	clamp_coordinates(&capture_x, &capture_y, capture_width, capture_height);

	XImage *src_image = XGetImage(
		display,
		RootWindow(display, screen),
		capture_x, capture_y,
		capture_width, capture_height,
		AllPlanes,
		ZPixmap
	);

	if (!src_image) {
		fprintf(stderr, "XGetImage failed\n");
		XFree(cursor);
		return;
	}

	XImage *dest_image = XCreateImage(
		display,
		DefaultVisual(display, screen),
		DefaultDepth(display, screen),
		ZPixmap,
		0,
		malloc(window_size * window_size * 4),
		window_size, window_size,
		32,
		0
	);

	for (int y = 0; y < window_size; y++) {
		for (int x = 0; x < window_size; x++) {
		int src_x = x / current_zoom;
		int src_y = y / current_zoom;
			XPutPixel(dest_image, x, y, XGetPixel(src_image, src_x, src_y));
		}
	}

	GC gc = XCreateGC(display, zoom_window, 0, NULL);
	XPutImage(display, zoom_window, gc, dest_image, 0, 0, 0, 0, window_size, window_size);

	if (show_crosshairs) {
		XSetForeground(display, gc, 0xFF0000);
		XSetLineAttributes(display, gc, 1, LineSolid, CapButt, JoinMiter);
		int center = window_size / 2;
		XDrawLine(display, zoom_window, gc, center, 0, center, window_size);
		XDrawLine(display, zoom_window, gc, 0, center, window_size, center);
	}

	XFreeGC(display, gc);
	XDestroyImage(src_image);
	free(dest_image->data);
	dest_image->data = NULL;
	XDestroyImage(dest_image);
	XFree(cursor);
}

int main(int argc, char *argv[]) {
	parse_arguments(argc, argv);

	init_x11();

	int event_base, error_base;
	if (!XFixesQueryExtension(display, &event_base, &error_base)) {
		fprintf(stderr, "XFixes not available\n");
		XCloseDisplay(display);
		return 1;
	}

	create_zoom_window();

	while (running) {
		while (XPending(display)) {
			XEvent event;
			XNextEvent(display, &event);
			if (event.type == KeyPress) {
				handle_keypress(&event);
			}
		}

		update_zoom();
		XFlush(display);
		usleep(16000);
	}

	XCloseDisplay(display);
	return 0;
}
