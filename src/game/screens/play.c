/**
 * \file game/screens/play.c
 * \ingroup game_screens
 *
 * \brief
 * Implement the main game screen.
 */

#include "game/game.h"
#include "game/screens/screens.h"
#include "game/tty.h"

#include <SDL2/SDL.h>

static int on_event(struct oshu_game *game, union SDL_Event *event)
{
	switch (event->type) {
	case SDL_KEYDOWN:
		if (event->key.repeat)
			break;
		switch (event->key.keysym.sym) {
		case OSHU_PAUSE_KEY:
			oshu_pause_game(game);
			break;
		case OSHU_REWIND_KEY:
			oshu_rewind_game(game, 10.);
			break;
		case OSHU_FORWARD_KEY:
			oshu_forward_game(game, 20.);
			break;
		default:
			if (!game->autoplay) {
				enum oshu_finger key = oshu_translate_key(&event->key.keysym);
				if (key != OSHU_UNKNOWN_KEY)
					game->mode->press(game, key);
			}
		}
		break;
	case SDL_KEYUP:
		if (!game->autoplay) {
			enum oshu_finger key = oshu_translate_key(&event->key.keysym);
			if (key != OSHU_UNKNOWN_KEY)
				game->mode->release(game, key);
		}
		break;
	case SDL_MOUSEBUTTONDOWN:
		if (!game->autoplay)
			game->mode->press(game, OSHU_LEFT_BUTTON);
		break;
	case SDL_MOUSEBUTTONUP:
		if (!game->autoplay)
			game->mode->release(game, OSHU_LEFT_BUTTON);
		break;
	case SDL_WINDOWEVENT:
		switch (event->window.event) {
		case SDL_WINDOWEVENT_MINIMIZED:
		case SDL_WINDOWEVENT_FOCUS_LOST:
			if (!game->autoplay && game->hit_cursor->next)
				oshu_pause_game(game);
			break;
		case SDL_WINDOWEVENT_CLOSE:
			oshu_stop_game(game);
			break;
		}
		break;
	}
	return 0;
}

static void check_end(struct oshu_game *game)
{
	if (game->hit_cursor->next)
		return;
	const double delay = game->beatmap.difficulty.leniency + game->beatmap.difficulty.approach_time;
	if (game->clock.now > oshu_hit_end_time(game->hit_cursor->previous) + delay) {
		oshu_reset_view(&game->display);
		oshu_paint_score(game);
		oshu_congratulate(game);
		game->screen = &oshu_score_screen;
	}
}

static int update(struct oshu_game *game)
{
	if (game->clock.now >= 0)
		oshu_play_audio(&game->audio);
	if (game->autoplay)
		game->mode->autoplay(game);
	else
		game->mode->check(game);
	check_end(game);
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
static void draw_background(struct oshu_game *game)
{
	double break_start = oshu_hit_end_time(oshu_previous_hit(game));
	double break_end = oshu_next_hit(game)->time;
	double now = game->clock.now;
	double ratio = 0.;
	if (break_end - break_start > 6.)
		ratio = oshu_trapezium(break_start + 1, break_end - 1, 1, now);
	oshu_show_background(&game->ui.background, ratio);
}

static int draw(struct oshu_game *game)
{
	SDL_ShowCursor(SDL_DISABLE);
	draw_background(game);
	oshu_show_metadata(game);
	oshu_show_audio_progress_bar(&game->ui.audio_progress_bar);
	game->mode->draw(game);
	return 0;
}

struct oshu_game_screen oshu_play_screen = {
	.name = "Playing",
	.on_event = on_event,
	.update = update,
	.draw = draw,
};
