/**
 * \file src/oshu/main.cc
 *
 * \brief
 * Main entry-point for the game.
 *
 * Interpret command-line arguments and spawn everything.
 */

#include "config.h"

#include "core/log.h"
#include "game/base.h"
#include "game/osu.h"
#include "ui/shell.h"
#include "ui/osu.h"
#include "video/display.h"

extern "C" {
#include <libavutil/log.h>
}

#include <SDL2/SDL.h>

#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>

enum option_values {
	OPT_AUTOPLAY = 0x10000,
	OPT_HELP = 'h',
	OPT_PAUSE = 0x10001,
	OPT_VERBOSE = 'v',
	OPT_VERSION = 0x10002,
};

static struct option options[] = {
	{"autoplay", no_argument, 0, OPT_AUTOPLAY},
	{"help", no_argument, 0, OPT_HELP},
	{"pause", no_argument, 0, OPT_PAUSE},
	{"verbose", no_argument, 0, OPT_VERBOSE},
	{"version", no_argument, 0, OPT_VERSION},
	{0, 0, 0, 0},
};

static const char *flags = "vh";

static const char *usage =
	"Usage: oshu [OPTION]... BEATMAP.osu\n"
	"       oshu --help\n"
;

static const char *help =
	"Options:\n"
	"  -v, --verbose       Increase the verbosity.\n"
	"  -h, --help          Show this help message.\n"
	"  --version           Output version information.\n"
	"  --autoplay          Perform a perfect run.\n"
	"  --pause             Start the game paused.\n"
	"\n"
	"Check the man page oshu(1) for details.\n"
;

static const char *version =
	"oshu! " PROJECT_VERSION "\n"
	"Copyright (C) 2018 Frédéric Mangano-Tarumi\n"
	"License GPLv3+: GNU GPL version 3 or later <https://gnu.org/licenses/gpl.html>.\n"
	"This is free software: you are free to change and redistribute it.\n"
	"There is NO WARRANTY, to the extent permitted by law.\n"
;

static std::weak_ptr<oshu::shell> current_shell;

static void signal_handler(int signum)
{
	if (auto w = current_shell.lock())
		w->close();
}

int run(const char *beatmap_path, int autoplay, int pause)
{
	int rc = 0;

	if (SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO) < 0) {
		oshu_log_error("SDL initialization error: %s", SDL_GetError());
		return -1;
	}

	try {
		oshu::osu_game game(beatmap_path);
		game.autoplay = autoplay;
		if (pause)
			game.pause();

		oshu::display display;
		std::shared_ptr<oshu::shell> shell = std::make_shared<oshu::shell>(display, game);
		shell->game_view = std::make_unique<oshu::osu_ui>(&display, game);
		current_shell = shell;
		shell->open();
	} catch (std::exception &e) {
		oshu::critical_log() << e.what() << std::endl;
		rc = -1;
	}

	SDL_Quit();
	return rc;
}

int main(int argc, char **argv)
{
	int autoplay = 0;
	int pause = 0;

	for (;;) {
		int c = getopt_long(argc, argv, flags, options, NULL);
		if (c == -1)
			break;
		switch (c) {
		case OPT_VERBOSE:
			--oshu::log_priority;
			break;
		case OPT_HELP:
			puts(usage);
			fputs(help, stdout);
			return 0;
		case OPT_AUTOPLAY:
			autoplay = 1;
			break;
		case OPT_PAUSE:
			pause = 1;
			break;
		case OPT_VERSION:
			fputs(version, stdout);
			return 0;
		default:
			fputs(usage, stderr);
			return 2;
		}
	}

	if (argc - optind != 1) {
		fputs(usage, stderr);
		return 2;
	}

	SDL_LogSetAllPriority(SDL_LOG_PRIORITY_WARN);
	SDL_LogSetPriority(SDL_LOG_CATEGORY_APPLICATION, static_cast<SDL_LogPriority>(oshu::log_priority));
	av_log_set_level(oshu::log_priority <= oshu::log_level::debug ? AV_LOG_INFO : AV_LOG_ERROR);

	char *beatmap_path = realpath(argv[optind], NULL);
	if (beatmap_path == NULL) {
		oshu_log_error("cannot locate %s", argv[optind]);
		return 3;
	}

	char *beatmap_file = beatmap_path;
	char *slash = strrchr(beatmap_path, '/');
	if (slash) {
		*slash = '\0';
		oshu_log_debug("changing the current directory to %s", beatmap_path);
		if (chdir(beatmap_path) < 0) {
			oshu_log_error("error while changing directory: %s", strerror(errno));
			return 3;
		}
		beatmap_file = slash + 1;
	}

	signal(SIGTERM, signal_handler);
	signal(SIGINT, signal_handler);

	if (run(beatmap_file, autoplay, pause) < 0) {
		if (!isatty(fileno(stdout)))
			SDL_ShowSimpleMessageBox(
				SDL_MESSAGEBOX_ERROR,
				"oshu! fatal error",
				"oshu! encountered a fatal error. Start it from the command-line to get more details:\n"
				"$ oshu path/to/your/beatmap.osu",
				NULL
			);
		return 1;
	}

	free(beatmap_path);

	return 0;
}
