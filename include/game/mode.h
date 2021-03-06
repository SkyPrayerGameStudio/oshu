/**
 * \file include/game/mode.h
 * \ingroup game
 */

#pragma once

#include "game/controls.h"

namespace oshu {

/**
 * Define the contract for a game mode.
 *
 * It is defined as a game state with a set of pure virtual methods to
 * implement.
 *
 * Game modes inherit from this class and implement all these methods.
 *
 * \ingroup game
 */
struct game_mode {

	virtual ~game_mode() = default;

	/**
	 * Called at every game iteration, unless the game is paused.
	 *
	 * The job of this function is to check the game clock and see if notes
	 * were missed, or other things of the same kind.
	 *
	 * There's no guarantee this callback is called at regular intervals.
	 *
	 * For autoplay, use #check_autoplay instead.
	 */
	virtual int check() = 0;

	/**
	 * Called pretty much like #check, except it's for autoplay mode.
	 */
	virtual int check_autoplay() = 0;

	/**
	 * Handle a key press keyboard event, or mouse button press event.
	 *
	 * Key repeats are filtered out by the parent module, along with any
	 * key used by the game module itself, like escape or space to pause, q
	 * to quit, &c. Same goes for mouse buttons.
	 *
	 * If you need the mouse position, use #oshu::get_mouse to have it in
	 * game coordinates.
	 *
	 * This callback isn't called when the game is paused or on autoplay.
	 *
	 * \sa release
	 */
	virtual int press(enum oshu::finger key) = 0;

	/**
	 * See #press.
	 */
	virtual int release(enum oshu::finger key) = 0;

	/**
	 * Release any held object, like sliders or hold notes.
	 *
	 * This function is called whenever the user seeks somewhere in the
	 * song.
	 */
	virtual int relinquish() = 0;

};

}
