#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>
#include <wayland-server.h>
#include <wayland-util.h>
#include "idle-client-protocol.h"

static struct org_kde_kwin_idle *idle_manager = NULL;
static struct wl_seat *seat = NULL;

struct rawe_state {
	struct wl_display *display;
	struct wl_event_loop *event_loop;
	time_t last_idle, last_resumed;
} state;

struct rawe_timeout_cmd {
	char *cmd;
	int timeout;
	struct wl_event_source *timer;
};

struct rawe_parsed_args {
	int timeout_idle, cmd_count;
};

static enum log_level {
	LOG_DEBUG,
	LOG_INFO,
	LOG_ERROR,
} log_level = LOG_INFO;

static void rawe_log(enum log_level level, const char *fmt, ...)
{
	if (level < log_level) {
		return;
	}

	va_list args;
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
	fputc('\n', stderr);
}

static void rawe_terminate(int exit_code)
{
	if (state.display) {
		wl_display_disconnect(state.display);
	}
	if (state.event_loop) {
		wl_event_loop_destroy(state.event_loop);
	}
	exit(exit_code);
}

static void handle_global(void *data, struct wl_registry *registry,
		uint32_t name, const char *interface, uint32_t version)
{
	if (strcmp(interface, org_kde_kwin_idle_interface.name) == 0) {
		idle_manager = wl_registry_bind(
				registry, name, &org_kde_kwin_idle_interface, 1);
	} else if (strcmp(interface, wl_seat_interface.name) == 0) {
		seat = wl_registry_bind(registry, name, &wl_seat_interface, 1);
	}
}

static void handle_global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
	// Do nothing.
}

static const struct wl_registry_listener registry_listener = {
	.global = handle_global,
	.global_remove = handle_global_remove,
};

static void handle_idle(void *data, struct org_kde_kwin_idle_timeout *timer)
{
	rawe_log(LOG_DEBUG, "Switch to inactive state");
	state.last_idle = time(NULL);
}

static void handle_resumed(void *data, struct org_kde_kwin_idle_timeout *timer)
{
	rawe_log(LOG_DEBUG, "Switch to active state");
	state.last_resumed = time(NULL);
}

static const struct org_kde_kwin_idle_timeout_listener idle_timer_listener = {
	.idle = handle_idle,
	.resumed = handle_resumed,
};

static int dispatch_events(int fd, uint32_t mask, void *data)
{
	if (mask & (WL_EVENT_HANGUP | WL_EVENT_ERROR)) {
		rawe_log(LOG_ERROR, "Received mask %d", mask);
		rawe_terminate(EXIT_FAILURE);
	}

	if (mask & WL_EVENT_READABLE) {
		int count = wl_display_dispatch(state.display);
		if (count < 0) {
			rawe_log(LOG_ERROR, "Event dispatch failed");
			rawe_terminate(EXIT_FAILURE);
		}
		return count;
	}

	return 0;
}

static void cmd_exec(char *line)
{
	pid_t pid = fork();
	if (pid == 0) {
		char *const cmd[] = { "sh", "-c", line, NULL };
		execvp(cmd[0], cmd);
		exit(1);
	}

	if (pid < 0) {
		rawe_log(LOG_ERROR, "Fork failed");
	} else {
		rawe_log(LOG_DEBUG, "Spawned process '%s'", line);
		waitpid(pid, NULL, 0);
	}
}

static int handle_timeout(void *data)
{
	struct rawe_timeout_cmd *cmd = data;
	if (state.last_idle > state.last_resumed) {
		rawe_log(LOG_DEBUG, "System is idle, skip timeout command");
		goto reschedule;
	}

	time_t now = time(NULL);
	double d = cmd->timeout / 1000 - difftime(now, state.last_resumed);

	if (d > 1.0) {
		rawe_log(LOG_DEBUG, "Delay command '%s' by %.0lfs", cmd->cmd, d);
		wl_event_source_timer_update(cmd->timer, d * 1000);
		return 0;
	}

	cmd_exec(cmd->cmd);

reschedule:
	wl_event_source_timer_update(cmd->timer, cmd->timeout);
	return 0;
}

static int handle_signal(int sig, void *data)
{
	// sig is either SIGINT or SIGTERM
	rawe_terminate(EXIT_SUCCESS);
	return 0;
}

static int parse_timeout(const char *value)
{
	char *endptr;
	unsigned long ms = strtoul(value, &endptr, 10);
	if (*endptr != '\0' || ms > INT_MAX) {
		rawe_log(LOG_ERROR,
				"Invalid timeout parameter '%s': expected an integer between 0 and %d",
				value, INT_MAX);
		rawe_terminate(EXIT_FAILURE);
	}

	return ms;
}

static int parse_timeout_cmd(int argc, char *argv[])
{
	if (argc < 3) {
		rawe_log(LOG_ERROR, "Too few arguments for the timeout command");
		rawe_terminate(EXIT_FAILURE);
	}

	struct rawe_timeout_cmd *cmd = malloc(sizeof(struct rawe_timeout_cmd));
	cmd->timeout = parse_timeout(argv[1]);
	cmd->cmd = strdup(argv[2]);
	cmd->timer = wl_event_loop_add_timer(state.event_loop, handle_timeout, cmd);
	wl_event_source_timer_update(cmd->timer, cmd->timeout);

	rawe_log(LOG_DEBUG,
			"Register timeout command '%s' to run every %dms",
			cmd->cmd, cmd->timeout);
	return 3;
}

static struct rawe_parsed_args parse_args(int argc, char *argv[])
{
	struct option long_options[] = {
		{ "help", no_argument, NULL, 'h' },
		{ "idle", required_argument, NULL, 'i' },
		{ "debug", no_argument, NULL, 'd' },
	};

	struct rawe_parsed_args result = {
		.timeout_idle = 60 * 1000,
		.cmd_count = 0,
	};

	int c;
	while ((c = getopt_long(argc, argv, "i:dh", long_options, NULL)) != -1) {
		switch (c) {
		case 'i':
			result.timeout_idle = parse_timeout(optarg);
			break;
		case 'd':
			log_level = LOG_DEBUG;
			break;
		case 'h':
			printf("Usage: %s [OPTIONS] [COMMANDS]\n\n", argv[0]);
			printf("  -h, --help\tPrint this help message and quit.\n");
			printf("  -i, --idle\tSet the timeout after which the command timers should be reset.\n");
			printf("  -d, --debug\tEnable debug output.\n\n");
			printf("Commands:\n\n");
			printf("  timeout <timeout in ms> <command to execute>.\n\n");
			rawe_terminate(EXIT_SUCCESS);
		default:
			rawe_log(LOG_ERROR, "Unsupported flag: '%c'", c);
			rawe_terminate(EXIT_FAILURE);
		}
	}

	int i = optind;
	while (i < argc) {
		if (!strcmp(argv[i], "timeout")) {
			i += parse_timeout_cmd(argc - i, &argv[i]);
			result.cmd_count++;
		} else {
			rawe_log(LOG_ERROR, "Unsupported command: '%s'", argv[i]);
			rawe_terminate(EXIT_FAILURE);
		}
	}

	return result;
}

int main(int argc, char *argv[])
{
	state.event_loop = wl_event_loop_create();
	state.last_idle = state.last_resumed = time(NULL);
	struct rawe_parsed_args args = parse_args(argc, argv);

	state.display = wl_display_connect(NULL);
	if (state.display == NULL) {
		rawe_log(LOG_ERROR, "Unable to connect to the compositor");
		rawe_terminate(EXIT_FAILURE);
	}

	struct wl_registry *registry = wl_display_get_registry(state.display);
	wl_registry_add_listener(registry, &registry_listener, NULL);
	wl_display_roundtrip(state.display);

	if (idle_manager == NULL) {
		rawe_log(LOG_ERROR, "The compositor does not support idle protocol");
		rawe_terminate(EXIT_FAILURE);
	}

	if (seat == NULL) {
		rawe_log(LOG_ERROR, "Seat error");
		rawe_terminate(EXIT_FAILURE);
	}

	struct org_kde_kwin_idle_timeout *idle_timer =
		org_kde_kwin_idle_get_idle_timeout(idle_manager, seat, args.timeout_idle);
	org_kde_kwin_idle_timeout_add_listener(idle_timer, &idle_timer_listener, NULL);
	wl_display_roundtrip(state.display);

	wl_event_loop_add_fd(state.event_loop,
			wl_display_get_fd(state.display), WL_EVENT_READABLE,
			dispatch_events, NULL);

	if (args.cmd_count < 1) {
		rawe_log(LOG_INFO, "No timeout command to perform");
		rawe_terminate(EXIT_SUCCESS);
	}

	wl_event_loop_add_signal(state.event_loop, SIGINT, handle_signal, NULL);
	wl_event_loop_add_signal(state.event_loop, SIGTERM, handle_signal, NULL);

	while (wl_event_loop_dispatch(state.event_loop, -1) != 1) {
		// Do nothing.
	}

	return EXIT_SUCCESS;
}
