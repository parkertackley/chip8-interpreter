#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include "SDL.h"

typedef struct {
	SDL_Window *window;
	SDL_Renderer *renderer;
} sdl_t;

typedef struct {
	uint32_t window_width;	// SDL window width
	uint32_t window_height;	// SDL window height
	uint32_t fg_color;		// foreground color RGBA8888
	uint32_t bg_color;		// background color RGBA8888
	uint32_t scale_factor;	// amount to scale a CHIP8 pixel by
} config_t;
	
typedef enum {
	QUIT,
	RUNNING,
	PAUSED,
} emulator_state_t;

// Chip8 machine object
typedef struct {
	emulator_state_t state;
} chip8_t;

// Initialize SDL2
bool init_sdl( sdl_t *sdl, const config_t config){
	if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
		SDL_Log("Unable to initialize SDL: %s\n", SDL_GetError());
		return false;
	}

	sdl->window = SDL_CreateWindow("Chip-8 Emulator", SDL_WINDOWPOS_CENTERED, 
								SDL_WINDOWPOS_CENTERED,
		       					config.window_width * config.scale_factor, 
								config.window_height * config.scale_factor, 
								0);

	if(!sdl->window) {
		SDL_Log("Could not create SDL window %s\n", SDL_GetError());
		return false;
	}

	sdl->renderer = SDL_CreateRenderer(sdl->window, -1, SDL_RENDERER_ACCELERATED);
	if(!sdl->renderer) {
		SDL_Log("Could not create SDL renderer %s\n", SDL_GetError());
		return false;
	}

	return true;
}

// Setup initial emulatr config
bool set_config_from_args(config_t *config, const int argc, char **argv) {
	// Set defaults
	*config = (config_t) {
		.window_width = 64,
		.window_height = 32,
		.fg_color = 0xFFFFFFFF,	// white
		.bg_color = 0xFFFF00FF,	// yellow
		.scale_factor = 20,		// default res  will be 1280x640
	};

	// Override defaults
	for(int i = 1; i < argc; ++i) {
		(void)argv[i]; // prevents compile error
	}

	return true;
}

// Init CHIP8 machine
bool init_chip8(chip8_t *chip8) {
	chip8->state = RUNNING;
	return true;
}

// Final cleanup
void final_cleanup(const sdl_t sdl) {
	SDL_DestroyRenderer(sdl.renderer);
	SDL_DestroyWindow(sdl.window);
	SDL_Quit();	// shutdown SDL subsystems
}

// Clear screen / SDL window to backgorund color
void clear_screen(const sdl_t sdl, const config_t config) {
	const uint8_t r = (config.bg_color >> 24) & 0xFF;
	const uint8_t g = (config.bg_color >> 16) & 0xFF;
	const uint8_t b = (config.bg_color >> 8) & 0xFF;
	const uint8_t a = (config.bg_color >> 0) & 0xFF;

	SDL_SetRenderDrawColor(sdl.renderer, r, g, b, a);
	SDL_RenderClear(sdl.renderer);
}

// update window with changes
void update_screen(const sdl_t sdl) {
	SDL_RenderPresent(sdl.renderer);
	// SDL_RenderClear(sdl.renderer);
}

void handle_input(chip8_t *chip8) {
	SDL_Event event;

	while(SDL_PollEvent(&event)) {
		switch(event.type) {
			case SDL_QUIT:
				// exit window
				chip8->state = QUIT;
				return;

			case SDL_KEYDOWN:
				switch(event.key.keysym.sym){
					case SDLK_ESCAPE:
						chip8->state = QUIT;
						return;
					default:
						break;
				}
				break;

			case SDL_KEYUP:
				break;

			default:
				break;
		}
	}
}

int main(int argc, char **argv) {

	// Init emulator config/options
	config_t config = {0};
	if(!set_config_from_args(&config, argc, argv)) exit(EXIT_FAILURE);

	// Init SDL
	sdl_t sdl = {0};
	if(!init_sdl(&sdl, config)) exit(EXIT_FAILURE);

	// Init CHIP8 machine
	chip8_t chip8 = {0};
	if(!init_chip8(&chip8)) exit(EXIT_FAILURE);

	// Init screen clear to background color
	clear_screen(sdl, config);

	// Main emulator loop
	while(chip8.state != QUIT){
		clear_screen(sdl, config);
		//handle user input
		handle_input(&chip8);
		// if(chip8.state == PAUSED) continue;

		// emulate chip8 instructions

		// Delay for approx 60hz
		SDL_PumpEvents();
		SDL_Delay(16);
		// Update window with changes
		update_screen(sdl);
	}

	// Final cleanup
	final_cleanup(sdl);

	exit(EXIT_SUCCESS);

}

