/**
 * \file lib/ui/screens/play.cc
 * \ingroup ui_screens
 *
 * \brief
 * Implement the main game screen.
 */

#include "./screens.h"

#include "game/base.h"
#include "game/tty.h"
#include "ui/widget.h"
#include "ui/shell.h"
#include "video/display.h"
#include "video/transitions.h"

#include <SDL2/SDL.h>

static int on_event(oshu::shell &w, union SDL_Event *event)
{
	oshu::game_base *game = &w.game;
	switch (event->type) {
	case SDL_KEYDOWN:
		if (event->key.repeat)
			break;
		switch (event->key.keysym.sym) {
		case oshu::PAUSE_KEY:
			game->pause();
			w.screen = &oshu::pause_screen;
			break;
		case oshu::REWIND_KEY:
			game->rewind(10.);
			break;
		case oshu::FORWARD_KEY:
			game->forward(20.);
			break;
		default:
			if (!game->autoplay) {
				enum oshu::finger key = oshu::translate_key(&event->key.keysym);
				if (key != oshu::UNKNOWN_KEY)
					game->press(key);
			}
		}
		break;
	case SDL_KEYUP:
		if (!game->autoplay) {
			enum oshu::finger key = oshu::translate_key(&event->key.keysym);
			if (key != oshu::UNKNOWN_KEY)
				game->release(key);
		}
		break;
	case SDL_MOUSEBUTTONDOWN:
		if (!game->autoplay)
			game->press(oshu::LEFT_BUTTON);
		break;
	case SDL_MOUSEBUTTONUP:
		if (!game->autoplay)
			game->release(oshu::LEFT_BUTTON);
		break;
	case SDL_WINDOWEVENT:
		switch (event->window.event) {
		case SDL_WINDOWEVENT_MINIMIZED:
		case SDL_WINDOWEVENT_FOCUS_LOST:
			if (!game->autoplay && game->hit_cursor->next) {
				game->pause();
				w.screen = &oshu::pause_screen;
			}
			break;
		case SDL_WINDOWEVENT_CLOSE:
			w.close();
			break;
		}
		break;
	}
	return 0;
}

/**
 * Once the last note of the beatmap is past the game cursor, end the game.
 *
 * This is where the score is computed, and displayed on the console.
 *
 * The game switches to the score screen, from which the only exit is *death*.
 */
static void check_end(oshu::shell &w)
{
	oshu::game_base *game = &w.game;
	if (game->hit_cursor->next)
		return;
	const double delay = game->beatmap.difficulty.leniency + game->beatmap.difficulty.approach_time;
	if (game->clock.now > oshu::hit_end_time(game->hit_cursor->previous) + delay) {
		oshu::reset_view(&w.display);
		oshu::create_score_frame(&w.display, &game->beatmap, &w.score);
		oshu::congratulate(game);
		w.screen = &oshu::score_screen;
	}
}

static int update(oshu::shell &w)
{
	oshu::game_base *game = &w.game;
	if (game->paused) {
		w.screen = &oshu::pause_screen;
		return 0;
	}
	if (game->clock.now >= 0)
		oshu::play_audio(&game->audio);
	if (game->autoplay)
		game->check_autoplay();
	else
		game->check();
	check_end(w);
	return 0;
}

/**
 * Draw the background, adjusting the brightness.
 *
 * Most of the time, the background will be displayed at 25% of its luminosity,
 * so that hit objects are clear.
 *
 * During breaks, the background is shown at full luminosity. The variation
 * show in the following graph, where *S* is the end time of the previous note
 * and the start of the break, and E the time of the next note and the end of
 * the break.
 *
 * ```
 * 100% ┼      ______________
 *      │     /              \
 *      │    /                \
 *      │___/                  \___
 *  25% │
 *      └──────┼────────────┼─────┼─> t
 *      S     S+2s         E-2s   E
 * ```
 *
 * A break must have a duration of at least 6 seconds, ensuring the animation
 * is never cut in between, or that the background stays lit for less than 2
 * seconds.
 *
 */
static void draw_background(oshu::shell &w)
{
	oshu::game_base *game = &w.game;
	double break_start = oshu::hit_end_time(oshu::previous_hit(game));
	double break_end = oshu::next_hit(game)->time;
	double now = game->clock.now;
	double ratio = 0.;
	if (break_end - break_start > 6.)
		ratio = oshu::trapezium(break_start + 1, break_end - 1, 1, now);
	oshu::show_background(&w.background, ratio);
}

static int draw(oshu::shell &w)
{
	oshu::game_base *game = &w.game;
	if (w.display.features & oshu::FANCY_CURSOR)
		SDL_ShowCursor(SDL_DISABLE);
	draw_background(w);
	oshu::show_metadata_frame(&w.metadata, oshu::fade_out(5, 6, game->clock.system));
	oshu::show_audio_progress_bar(&w.audio_progress_bar);
	if (w.game_view)
		w.game_view->draw();
	return 0;
}

/**
 * The standard in-play game screen.
 *
 * This is the main screen of the game. The beatmap is displayed and the user
 * interacts with it by clicking and stroking keys.
 *
 * This screen relies heavily on the game mode.
 *
 */
oshu::game_screen oshu::play_screen = {
	.name = "Playing",
	.on_event = on_event,
	.update = update,
	.draw = draw,
};
