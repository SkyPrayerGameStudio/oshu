/**
 * \file game/game.c
 * \ingroup game
 *
 * Game module implementation.
 */

#include "../config.h"

#include "game/game.h"
#include "graphics/draw.h"
#include "log.h"

#include <assert.h>
#include <stdio.h>
#include <unistd.h>

#include <SDL2/SDL_image.h>

/**
 * How long a frame should last in seconds. 17 is about 60 FPS.
 */
static const double frame_duration = .016666666;

int oshu_create_game(const char *beatmap_path, struct oshu_game *game)
{
	/* 1. Beatmap */
	if (oshu_load_beatmap(beatmap_path, &game->beatmap) < 0) {
		oshu_log_error("no beatmap, aborting");
		goto fail;
	}
	if (game->beatmap.mode == OSHU_OSU_MODE) {
		game->mode = &osu_mode;
	} else {
		oshu_log_error("unsupported game mode");
		goto fail;
	}
	assert (game->beatmap.hits != NULL);
	game->hit_cursor = game->beatmap.hits;

	/* 2. Audio */
	assert (game->beatmap.audio_filename != NULL);
	if (oshu_open_audio(game->beatmap.audio_filename, &game->audio) < 0) {
		oshu_log_error("no audio, aborting");
		goto fail;
	}
	oshu_open_sound_library(&game->library, &game->audio.device_spec);
	oshu_populate_library(&game->library, &game->beatmap);

	/* 3. Display */
	if (oshu_open_display(&game->display) < 0) {
		oshu_log_error("no display, aborting");
		goto fail;
	}
	char *title;
	if (asprintf(&title, "%s - oshu!", beatmap_path) >= 0) {
		SDL_SetWindowTitle(game->display.window, title);
		free(title);
	}
	if (game->beatmap.background_filename)
		oshu_load_texture(&game->display, game->beatmap.background_filename, &game->background);

	/* 4. Clock */
	if (game->beatmap.audio_lead_in > 0.) {
		game->clock.now = - game->beatmap.audio_lead_in;
	} else {
		double first_hit = game->beatmap.hits->next->time;
		if (first_hit < 1.)
			game->clock.now = first_hit - 1.;
	}

	/* 5. Post-initialization */
	if (game->mode->initialize(game) < 0)
		goto fail;

	return 0;

fail:
	oshu_destroy_game(game);
	return -1;
}

/**
 * Show the state of the game (paused/playing) and the current song position.
 *
 * Only do that for terminal outputs in order not to spam something if the
 * output is redirected.
 *
 * The state length must not decrease over time, otherwise you end up with
 * glitches. If you write `foo\rx`, you get `xoo`. This is the reason the
 * Paused string literal has an extra space.
 */
static void dump_state(struct oshu_game *game)
{
	if (!isatty(fileno(stdout)))
		return;
	int minutes = game->clock.now / 60.;
	double seconds = game->clock.now - minutes * 60.;
	const char *state = game->state & OSHU_PAUSED ? " Paused" : "Playing";
	double duration = game->audio.music.duration;
	int duration_minutes = duration / 60.;
	double duration_seconds = duration - duration_minutes * 60;
	printf(
		"%s: %d:%06.3f / %d:%06.3f\r",
		state, minutes, seconds,
		duration_minutes, duration_seconds
	);
	fflush(stdout);
}

static void pause_game(struct oshu_game *game)
{
	oshu_pause_audio(&game->audio);
	game->state |= OSHU_PAUSED;
	game->state &= ~OSHU_PLAYING;
	dump_state(game);
}

/**
 * Rewind the song by the specified offset in seconds.
 *
 * Rewind the beatmap too but leaving a 1-second break so that we won't seek
 * right before a note.
 */
static void rewind_music(struct oshu_game *game, double offset)
{
	oshu_seek_music(&game->audio, game->audio.music.current_timestamp - offset);
	game->clock.now = game->audio.music.current_timestamp;
	game->mode->relinquish(game);
	dump_state(game);

	assert (game->hit_cursor != NULL);
	while (game->hit_cursor->time > game->clock.now + 1.) {
		game->hit_cursor->state = OSHU_INITIAL_HIT;
		game->hit_cursor = game->hit_cursor->previous;
	}
}

static void forward_music(struct oshu_game *game, double offset)
{
	oshu_seek_music(&game->audio, game->audio.music.current_timestamp + offset);
	game->clock.now = game->audio.music.current_timestamp;
	game->mode->relinquish(game);

	if (!(game->state & OSHU_PAUSED))
		oshu_play_audio(&game->audio);

	dump_state(game);

	assert (game->hit_cursor != NULL);
	while (game->hit_cursor->time < game->clock.now + 1.) {
		game->hit_cursor->state = OSHU_SKIPPED_HIT;
		game->hit_cursor = game->hit_cursor->next;
	}
}

/**
 * Resume the game.
 *
 * If the music was playing, rewind it by 1 second to leave the player a little
 * break after resuming. This probably makes cheating possible but I couldn't
 * care less.
 *
 * Pausing on a slider will break it though.
 */
static void unpause_game(struct oshu_game *game)
{
	if (game->clock.now >= 0) {
		if (!(game->state & OSHU_AUTOPLAY))
			rewind_music(game, 1.);
		oshu_play_audio(&game->audio);
	}
	game->state &= ~OSHU_PAUSED;
	game->state |= OSHU_PLAYING;
}

enum oshu_key translate_key(SDL_Keysym *keysym)
{
	switch (keysym->scancode) {
	/* Bottom row, for standard and taiko modes. */
	case SDL_SCANCODE_Z: return OSHU_LEFT_MIDDLE;
	case SDL_SCANCODE_X: return OSHU_LEFT_INDEX;
	case SDL_SCANCODE_C: return OSHU_RIGHT_INDEX;
	case SDL_SCANCODE_V: return OSHU_RIGHT_MIDDLE;
	/* Middle row, for mania. */
	case SDL_SCANCODE_A: return OSHU_LEFT_PINKY;
	case SDL_SCANCODE_S: return OSHU_LEFT_RING;
	case SDL_SCANCODE_D: return OSHU_LEFT_MIDDLE;
	case SDL_SCANCODE_F: return OSHU_LEFT_INDEX;
	case SDL_SCANCODE_SPACE: return OSHU_THUMBS;
	case SDL_SCANCODE_J: return OSHU_RIGHT_INDEX;
	case SDL_SCANCODE_K: return OSHU_RIGHT_MIDDLE;
	case SDL_SCANCODE_L: return OSHU_RIGHT_RING;
	case SDL_SCANCODE_SEMICOLON: return OSHU_RIGHT_PINKY;
	default:             return OSHU_UNKNOWN_KEY;
	}
}

/**
 * React to an event got from SDL.
 */
static void handle_event(struct oshu_game *game, SDL_Event *event)
{
	switch (event->type) {
	case SDL_KEYDOWN:
		if (event->key.repeat)
			break;
		if (game->state & (OSHU_AUTOPLAY | OSHU_PAUSED)) {
			switch (event->key.keysym.sym) {
			case SDLK_q:
				game->state |= OSHU_STOPPING;
				break;
			case SDLK_ESCAPE:
				unpause_game(game);
				break;
			case SDLK_PAGEUP:
				rewind_music(game, 10.);
				break;
			case SDLK_PAGEDOWN:
				forward_music(game, 20.);
				break;
			}
		} else if ((game->state & OSHU_USERPLAY) && (game->state & OSHU_PLAYING)) {
			switch (event->key.keysym.sym) {
			case SDLK_ESCAPE:
				pause_game(game);
				break;
			case SDLK_PAGEUP:
				rewind_music(game, 10.);
				break;
			case SDLK_PAGEDOWN:
				forward_music(game, 20.);
				break;
			default:
				{
					enum oshu_key key = translate_key(&event->key.keysym);
					if (key != OSHU_UNKNOWN_KEY)
						game->mode->press(game, key);
				}
			}
		} else {
			/* probably the end screen, OSHU_FINISHED */
			switch (event->key.keysym.sym) {
			case SDLK_q:
				game->state |= OSHU_STOPPING;
				break;
			}
		}
		break;
	case SDL_KEYUP:
		if ((game->state & OSHU_USERPLAY) && (game->state & OSHU_PLAYING)) {
			enum oshu_key key = translate_key(&event->key.keysym);
			if (key != OSHU_UNKNOWN_KEY)
				game->mode->release(game, key);
		}
		break;
	case SDL_MOUSEBUTTONDOWN:
		if ((game->state & OSHU_USERPLAY) && (game->state & OSHU_PLAYING))
			game->mode->press(game, OSHU_LEFT_BUTTON);
		break;
	case SDL_MOUSEBUTTONUP:
		if ((game->state & OSHU_USERPLAY) && (game->state & OSHU_PLAYING))
			game->mode->release(game, OSHU_LEFT_BUTTON);
		break;
	case SDL_WINDOWEVENT:
		switch (event->window.event) {
		case SDL_WINDOWEVENT_MINIMIZED:
		case SDL_WINDOWEVENT_FOCUS_LOST:
			if ((game->state & OSHU_USERPLAY) && (game->state & OSHU_PLAYING) && game->hit_cursor->next)
				pause_game(game);
			break;
		case SDL_WINDOWEVENT_CLOSE:
			game->state |= OSHU_STOPPING;
			break;
		}
		break;
	}
}

/**
 * Print the score if the song was finished, then exit.
 */
static void end(struct oshu_game *game)
{
	/* Clear the status line. */
	printf("\r                                        \r");
	/* Compute the score. */
	int good = 0;
	int missed = 0;
	for (struct oshu_hit *hit = game->beatmap.hits; hit; hit = hit->next) {
		if (hit->state == OSHU_MISSED_HIT)
			missed++;
		else if (hit->state == OSHU_GOOD_HIT)
			good++;
	}
	double rate = (double) good / (good + missed);
	printf(
		"  \033[1mScore:\033[0m\n"
		"  \033[%dm%3d\033[0m good\n"
		"  \033[%dm%3d\033[0m miss\n"
		"\n",
		rate >= 0.9 ? 32 : 0, good,
		rate < 0.5  ? 31 : 0, missed
	);
}

static void draw(struct oshu_game *game)
{
	SDL_SetRenderDrawColor(game->display.renderer, 0, 0, 0, 255);
	SDL_RenderClear(game->display.renderer);
	game->mode->draw(game);
	SDL_RenderPresent(game->display.renderer);
}

/**
 * Update the game clock.
 *
 * It as roughly 2 modes:
 *
 * 1. When the audio has a lead-in time, rely on SDL's ticks to increase the
 *    clock.
 * 2. When the lead-in phase is over, use the audio clock. However, if we
 *    detect it hasn't change, probably because the codec frame is too big, then
 *    we make it progress with the SDL clock anyway.
 *
 * In both cases, we wanna ensure the *now* clock is always monotonous. If we
 * detect the new time is before the previous time, then we stop the time until
 * now catches up with before. That case does happen at least right after the
 * lead-in phase, because the audio starts when the *now* clock becomes
 * positive, while the audio clock will be null at that moment.
 */
static void update_clock(struct oshu_game *game)
{
	struct oshu_clock *clock = &game->clock;
	double system = SDL_GetTicks() / 1000.;
	double diff = system - clock->system;
	double prev_audio = clock->audio;
	clock->audio = game->audio.music.current_timestamp;
	clock->before = clock->now;
	clock->system = system;
	if (!(game->state & OSHU_PLAYING))
		; /* don't update the clock */
	else if (clock->before < 0) /* leading in */
		clock->now = clock->before + diff;
	else if (clock->audio == prev_audio) /* audio clock stuck */
		clock->now = clock->before + diff;
	else
		clock->now = clock->audio;
	/* force monotonicity */
	if (clock->now < clock->before)
		clock->now = clock->before;
}

static void welcome(struct oshu_game *game)
{
	struct oshu_beatmap *beatmap = &game->beatmap;
	struct oshu_metadata *meta = &beatmap->metadata;
	printf(
		"\n"
		"  \033[33m%s\033[0m // %s\n"
		"  \033[33m%s\033[0m // %s\n",
		meta->title_unicode, meta->title,
		meta->artist_unicode, meta->artist
	);
	if (meta->source)
		printf("  From %s\n", meta->source);

	printf("\n  \033[34m%s\033[0m\n", meta->version);
	if (meta->creator)
		printf("  By %s\n", meta->creator);

	int stars = beatmap->difficulty.overall_difficulty;
	double half_star = beatmap->difficulty.overall_difficulty - stars;
	printf("  ");
	for (int i = 0; i < stars; i++)
		printf("★ ");
	if (half_star >= .5)
		printf("☆ ");

	printf("\n\n");
}

static void check_end(struct oshu_game *game)
{
	if (game->state & OSHU_FINISHED)
		return;
	if (game->hit_cursor->next)
		return;
	if (game->clock.now > oshu_hit_end_time(game->hit_cursor->previous) + game->beatmap.difficulty.leniency) {
		game->state = OSHU_FINISHED | OSHU_PLAYING;
		end(game);
	}
}

int oshu_run_game(struct oshu_game *game)
{
	welcome(game);
	/* Reset the clock.
	 * Otherwise, when the startup is slow, the clock would jump. */
	game->clock.system = SDL_GetTicks() / 1000.;
	SDL_Event event;
	int missed_frames = 0;
	if ((game->state & OSHU_PLAYING) && game->clock.now >= 0)
		oshu_play_audio(&game->audio);
	while (!(game->state & OSHU_STOPPING)) {
		update_clock(game);
		if (game->clock.before < 0 && game->clock.now >= 0)
			oshu_play_audio(&game->audio);
		while (SDL_PollEvent(&event))
			handle_event(game, &event);
		if (game->state & OSHU_USERPLAY)
			game->mode->check(game);
		else if (game->state & OSHU_AUTOPLAY)
			game->mode->autoplay(game);
		check_end(game);
		draw(game);
		if ((game->state & OSHU_PLAYING) && !(game->state & OSHU_FINISHED))
			dump_state(game);
		double advance = frame_duration - (SDL_GetTicks() / 1000. - game->clock.system);
		if (advance > 0) {
			SDL_Delay(advance * 1000);
		} else {
			missed_frames++;
			if (missed_frames == 1000)
				oshu_log_warning("your computer is having a hard time keeping up 60 FPS");
		}
	}
	oshu_log_debug("%d missed frames", missed_frames);
	return 0;
}

void oshu_destroy_game(struct oshu_game *game)
{
	assert (game != NULL);
	if (game->mode)
		game->mode->destroy(game);
	oshu_destroy_beatmap(&game->beatmap);
	oshu_close_audio(&game->audio);
	oshu_close_sound_library(&game->library);
	oshu_destroy_texture(&game->background);
	oshu_close_display(&game->display);
}

struct oshu_hit* oshu_look_hit_back(struct oshu_game *game, double offset)
{
	struct oshu_hit *hit = game->hit_cursor;
	double target = game->clock.now - offset;
	/* seek backward */
	while (oshu_hit_end_time(hit) > target)
		hit = hit->previous;
	/* seek forward */
	while (oshu_hit_end_time(hit) < target)
		hit = hit->next;
	/* here we have the guarantee that hit->time >= target */
	return hit;
}

struct oshu_hit* oshu_look_hit_up(struct oshu_game *game, double offset)
{
	struct oshu_hit *hit = game->hit_cursor;
	double target = game->clock.now + offset;
	/* seek forward */
	while (hit->time < target)
		hit = hit->next;
	/* seek backward */
	while (hit->time > target)
		hit = hit->previous;
	/* here we have the guarantee that hit->time <= target */
	return hit;
}
