/**
 * \file graphics/texture.c
 * \ingroup graphics_texture
 */

#include "graphics/display.h"
#include "graphics/texture.h"

#include <cairo/cairo.h>

void oshu_draw_texture(struct oshu_display *display, oshu_point p, struct oshu_texture *texture)
{
	oshu_point top_left = p - texture->origin;
	SDL_Rect dest = {
		.x = creal(top_left), .y = cimag(top_left),
		.w = creal(texture->size), .h = cimag(texture->size),
	};
	SDL_RenderCopy(display->renderer, texture->texture, NULL, &dest);
}

/**
 * \todo
 * Error handling.
 */
int oshu_start_painting(oshu_size size, oshu_point origin, struct oshu_painter *painter)
{
	painter->size = size;
	painter->origin = origin;
	painter->destination = SDL_CreateRGBSurface(
		0, creal(size), cimag(size), 32,
		0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
	SDL_LockSurface(painter->destination);
	painter->surface = cairo_image_surface_create_for_data(
		painter->destination->pixels, CAIRO_FORMAT_ARGB32,
		creal(size), cimag(size), painter->destination->pitch);
	painter->cr = cairo_create(painter->surface);
	cairo_translate(painter->cr, creal(origin), cimag(origin));
	return 0;
}

/**
 * \todo
 * Error handling.
 */
int oshu_finish_painting(struct oshu_painter *painter, struct oshu_display *display, struct oshu_texture *texture)
{
	cairo_surface_destroy(painter->surface);
	SDL_UnlockSurface(painter->destination);
	texture->size = painter->size;
	texture->origin = painter->origin;
	texture->texture = SDL_CreateTextureFromSurface(display->renderer, painter->destination);
	SDL_FreeSurface(painter->destination);
	return 0;
}

void oshu_destroy_texture(struct oshu_texture *texture)
{
	if (texture->texture) {
		SDL_DestroyTexture(texture->texture);
		texture->texture = NULL;
	}
}