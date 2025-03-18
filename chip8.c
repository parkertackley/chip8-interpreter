#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#include "SDL.h"

typedef struct {
	SDL_Window *window;
	SDL_Renderer *renderer;
} sdl_t;

// CHIP8 Instruction format
typedef struct {
	uint16_t opcode;
	uint16_t NNN;		// 12 bit address/constant
	uint8_t NN;			// 8 bit constant
	uint8_t N;			// 4 bit constant
	uint8_t X;			// 4 bit register identifier
	uint8_t Y;			// 4 bit register identifier
} instruction_t;

typedef struct {
	uint32_t window_width;	// SDL window width
	uint32_t window_height;	// SDL window height
	uint32_t fg_color;		// foreground color RGBA8888
	uint32_t bg_color;		// background color RGBA8888
	uint32_t scale_factor;	// amount to scale a CHIP8 pixel by
	bool pixel_outlines;	// Draw pixel outlines yes/no
} config_t;
	
typedef enum {
	QUIT,
	RUNNING,
	PAUSED,
} emulator_state_t;

// Chip8 machine object
typedef struct {
	emulator_state_t state;
	uint8_t ram[4096];
	bool display[64*32];	// Emulate original CHIP8 resolution
	uint16_t stack[12];		// Subroutine stack
	uint16_t *stack_pointer;
	uint8_t V[16];			// Data Registers V0-VF
	uint16_t I;				// Index Register
	uint16_t PC;			// Program counter
	uint8_t delay_timer;	// Decrements at 60hz when > 0
	uint8_t sound_timer;	// Decrements at 60hz and plays tone when > 0
	bool keypad[16];		// Hexadeciaml keypad 0x0-0xF
	const char *rom_name;	// Currently running ROM
	instruction_t inst;		// currently executing instruction
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
		.bg_color = 0x000000FF,	// yellow
		.scale_factor = 20,		// default res  will be 1280x640
		.pixel_outlines = true,	// draw pixel outlines by default
	};

	// Override defaults
	for(int i = 1; i < argc; ++i) {
		(void)argv[i]; // prevents compile error
	}

	return true;
}

// Init CHIP8 machine
bool init_chip8(chip8_t *chip8, const char rom_name[]) {
	const uint32_t entry_point = 0x200;
	const uint8_t font[] = {
		0xF0, 0x90, 0x90, 0x90, 0xF0, // 0
		0x20, 0x60, 0x20, 0x20, 0x70, // 1
		0xF0, 0x10, 0xF0, 0x80, 0xF0, // 2
		0xF0, 0x10, 0xF0, 0x10, 0xF0, // 3
		0x90, 0x90, 0xF0, 0x10, 0x10, // 4
		0xF0, 0x80, 0xF0, 0x10, 0xF0, // 5
		0xF0, 0x80, 0xF0, 0x90, 0xF0, // 6
		0xF0, 0x10, 0x20, 0x40, 0x40, // 7
		0xF0, 0x90, 0xF0, 0x90, 0xF0, // 8
		0xF0, 0x90, 0xF0, 0x10, 0xF0, // 9
		0xF0, 0x90, 0xF0, 0x90, 0x90, // A
		0xE0, 0x90, 0xE0, 0x90, 0xE0, // B
		0xF0, 0x80, 0x80, 0x80, 0xF0, // C
		0xE0, 0x90, 0x90, 0x90, 0xE0, // D
		0xF0, 0x80, 0xF0, 0x80, 0xF0, // E
		0xF0, 0x80, 0xF0, 0x80, 0x80  // F
	};

	// Load font
	memcpy(&chip8->ram[0], font, sizeof(font));

	// Open ROM file
	FILE *rom = fopen(rom_name, "rb");
	if(!rom) {
		SDL_Log("Rom file %s is invalid or does not exist\n", rom_name);
		return false;
	}

	// Get and check ROM size
	fseek(rom, 0, SEEK_END);
	const size_t rom_size = ftell(rom);
	const size_t max_size = sizeof(chip8->ram) - entry_point;
	rewind(rom);

	if(rom_size > max_size) {
		SDL_Log("Rom file %s is too big! Rom size: %zu, Max size allowed: %zu\n", rom_name, rom_size, max_size);
		return false;
	}

	if(!fread(&chip8->ram[entry_point], rom_size, 1, rom)) {
		SDL_Log("Could not read Rom file %s into CHIP8 memory\n", rom_name);
		return false;
	}
	fclose(rom);

	// Set CHIP8 defaults
	chip8->state = RUNNING;
	chip8->PC = entry_point;
	chip8->rom_name = rom_name;
	chip8->stack_pointer = &chip8->stack[0];

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
void update_screen(const sdl_t sdl, const config_t config, const chip8_t chip8) {
	SDL_Rect rect = {.x = 0, .y = 0, .w = config.scale_factor, .h = config.scale_factor};
	// Grab color values to draw
	const uint8_t fg_r = (config.fg_color >> 24) & 0xFF;
	const uint8_t fg_g = (config.fg_color >> 16) & 0xFF;
	const uint8_t fg_b = (config.fg_color >> 8) & 0xFF;
	const uint8_t fg_a = (config.fg_color >> 0) & 0xFF;

	const uint8_t bg_r = (config.bg_color >> 24) & 0xFF;
	const uint8_t bg_g = (config.bg_color >> 16) & 0xFF;
	const uint8_t bg_b = (config.bg_color >> 8) & 0xFF;
	const uint8_t bg_a = (config.bg_color >> 0) & 0xFF;

	// Loop through display, draw a rectangle per pixel to the SDL window
	for(uint32_t i = 0; i < sizeof chip8.display; i++) {
		// Translate 1D index i value to 2D X/Y coordinates
		rect.x = (i % config.window_width) * config.scale_factor;
		rect.y = (i / config.window_width) * config.scale_factor;

		if(chip8.display[i]) {
			// Pixel is on, draw forground color
			SDL_SetRenderDrawColor(sdl.renderer, fg_r, fg_g, fg_b, fg_a);
			SDL_RenderFillRect(sdl.renderer, &rect);

			// If user requests drawing pixel outlines
			if(config.pixel_outlines) {
				SDL_SetRenderDrawColor(sdl.renderer, bg_r, bg_g, bg_b, bg_a);
				SDL_RenderDrawRect(sdl.renderer, &rect);
			}

		} else {
			// Pixel is off, draw background color
			SDL_SetRenderDrawColor(sdl.renderer, bg_r, bg_g, bg_b, bg_a);
			SDL_RenderFillRect(sdl.renderer, &rect);
		}
	}

	SDL_RenderPresent(sdl.renderer);

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
					case SDLK_SPACE:
						// Space bar
						if(chip8->state == RUNNING)
							chip8->state = PAUSED;	// pause
						else {
							chip8->state = RUNNING;	// resume
							puts("==== PAUSED ====");
						}
						return;

					// Map qwerty keys to chip8 keypad
					case SDLK_1: chip8->keypad[0x1] = true; break;
					case SDLK_2: chip8->keypad[0x2] = true; break;
					case SDLK_3: chip8->keypad[0x3] = true; break;
					case SDLK_4: chip8->keypad[0xC] = true; break;

					case SDLK_q: chip8->keypad[0x4] = true; break;
					case SDLK_w: chip8->keypad[0x5] = true; break;
					case SDLK_e: chip8->keypad[0x6] = true; break;
					case SDLK_r: chip8->keypad[0xD] = true; break;
					
					case SDLK_a: chip8->keypad[0x7] = true; break;
					case SDLK_s: chip8->keypad[0x8] = true; break;
					case SDLK_d: chip8->keypad[0x9] = true; break;
					case SDLK_f: chip8->keypad[0xE] = true; break;
					
					case SDLK_z: chip8->keypad[0xA] = true; break;
					case SDLK_x: chip8->keypad[0x0] = true; break;
					case SDLK_c: chip8->keypad[0xB] = true; break;
					case SDLK_v: chip8->keypad[0xF] = true; break;

					default: break;
				}
				break;

			case SDL_KEYUP:
				switch(event.key.keysym.sym){
					case SDLK_1: chip8->keypad[0x1] = false; break;
					case SDLK_2: chip8->keypad[0x2] = false; break;
					case SDLK_3: chip8->keypad[0x3] = false; break;
					case SDLK_4: chip8->keypad[0xC] = false; break;
					
					case SDLK_q: chip8->keypad[0x4] = false; break;
					case SDLK_w: chip8->keypad[0x5] = false; break;
					case SDLK_e: chip8->keypad[0x6] = false; break;
					case SDLK_r: chip8->keypad[0xD] = false; break;

					case SDLK_a: chip8->keypad[0x7] = false; break;
					case SDLK_s: chip8->keypad[0x8] = false; break;
					case SDLK_d: chip8->keypad[0x9] = false; break;
					case SDLK_f: chip8->keypad[0xE] = false; break;

					case SDLK_z: chip8->keypad[0xA] = false; break;
					case SDLK_x: chip8->keypad[0x0] = false; break;
					case SDLK_c: chip8->keypad[0xB] = false; break;
					case SDLK_v: chip8->keypad[0xF] = false; break;

					default: break;
				}
			default:
				break;
		}
	}
}

#ifdef DEBUG
void print_debug_info(chip8_t *chip8) {
	printf("Address: 0x%04X, Opcode: 0x%04X Desc: ", chip8->PC-2, chip8->inst.opcode);
	// Emulate opcode
	switch((chip8->inst.opcode >> 12) & 0x0F) {
		case 0x00:
			if(chip8->inst.NNN == 0xE0) {
				// 0x00E0: Clear the screen
				printf("Clear screen\n");
			} else if(chip8->inst.NNN == 0xEE) {
				// 0x00EE: Return from subroutine
				printf("Return from subroutine to address0x%04X\n", 
						*(chip8->stack_pointer - 1));
			} else {
				printf("Uninplemented Opcode.\n");
			}
			break;
		case 0x01:
			// 0x1NNN: Jump to address NNN
			printf("Jump to address NNN (0x%04X)\n",
					chip8->inst.NNN);
			break;
		case 0x02:
			// 0x02NNN: Call subroutine at NNN
			printf("Stack pointer moved to (0x%04X)\n",
					chip8->PC);
			break;

		case 0x03:
			// 0x03XNN: Skip next instruction if VX == NN
			printf("Check if V%X (0x%02X) == NN (0x%02X), skip next instruction if true\n",
					chip8->inst.X, chip8->V[chip8->inst.X], chip8->inst.NN);
			break;

		case 0x04:
			// 0x4XNN: Skip next instruction if VX != NN
			printf("Check if V%X (0x%02X) != NN (0x%02X), skip next instruction if true\n",
					chip8->inst.X, chip8->V[chip8->inst.X], chip8->inst.NN);
			break;

		case 0x05:
			// 0x5XY0: Skip next instruction if VX == VY
			printf("Check if V%X (0x%02X) == V%X (0x%02X), skip next instruction if true\n",
					chip8->inst.X, chip8->V[chip8->inst.X],
					chip8->inst.Y, chip8->V[chip8->inst.Y]);
			break;

		case 0x06:
			// 0x6XNN: Set register VX to NN
			printf("Set register V%X to NN (0x%02X)\n",
					chip8->inst.X, chip8->inst.NN);
			break;

		case 0x07:
			// 0x7XNN: Set register VX += NN
			printf("Set register V%X (0x%02X) += NN (0x%02X). Result: 0x%02X\n", 
				chip8->inst.X, chip8->V[chip8->inst.X], 
				chip8->inst.NN, 
				chip8->V[chip8->inst.X] + chip8->inst.NN);
			break;

		case 0x08:
			switch(chip8->inst.N) {
				case 0:
					// 0x8XY0: Set register VX = VY
					printf("Set register V%X = V%X (0x%02x)\n",
							chip8->inst.X, chip8->inst.Y, chip8->V[chip8->inst.Y]);
					break;

				case 1:
					// 0x8XY1: Set register VX |= VY
					printf("Set register V%X (0x%02X) |= V%X (0x%02x). Result: 0x%02X\n",
							chip8->inst.X, chip8->V[chip8->inst.X],
							chip8->inst.Y, chip8->V[chip8->inst.Y],
							chip8->V[chip8->inst.X] | chip8->V[chip8->inst.X]);
					break;

				case 2:
					// 0x8XY2: Set register VX &= VY
					printf("Set register V%X (0x%02X) &= V%X (0x%02x). Result: 0x%02X\n",
							chip8->inst.X, chip8->V[chip8->inst.X],
							chip8->inst.Y, chip8->V[chip8->inst.Y],
							chip8->V[chip8->inst.X] & chip8->V[chip8->inst.X]);
					break;

				case 3:
					// 0x8XY3: Set register VX ^= VY
					printf("Set register V%X (0x%02X) ^= V%X (0x%02x). Result: 0x%02X\n",
							chip8->inst.X, chip8->V[chip8->inst.X],
							chip8->inst.Y, chip8->V[chip8->inst.Y],
							chip8->V[chip8->inst.X] ^ chip8->V[chip8->inst.X]);
					break;
				
				case 4:
					// 0x8XY4: Set register VX += VY set VF to 1 if carry, otherwise 0
					printf("Set register V%X (0x%02X) += V%X (0x%02x), VF = 1 if carry; Result: 0x%02X, VF = %X\n",
							chip8->inst.X, chip8->V[chip8->inst.X],
							chip8->inst.Y, chip8->V[chip8->inst.Y],
							chip8->V[chip8->inst.X] + chip8->V[chip8->inst.X],
							((uint16_t)(chip8->V[chip8->inst.X] + chip8->V[chip8->inst.X]) > 255));
					break;
				
				case 5:
					// 0x8XY5: Set register VX -= VY set VF to 1 if VX >= VY (undercarry)
					printf("Set register V%X (0x%02X) -= V%X (0x%02x), VF = 1 if carry; Result: 0x%02X, VF = %X\n",
							chip8->inst.X, chip8->V[chip8->inst.X],
							chip8->inst.Y, chip8->V[chip8->inst.Y],
							chip8->V[chip8->inst.X] - chip8->V[chip8->inst.X],
							(chip8->V[chip8->inst.Y] <= chip8->V[chip8->inst.X]));
					break;

				case 6:
					// 0x8XY6: Set register VX >>= 1, store LSB of VX prior to shift in VF
					printf("Set register V%X (0x%02X) >>= 1, VF = shfited off bit (%X); Result: 0x%02X\n",
							chip8->inst.X, chip8->V[chip8->inst.X],
							chip8->V[chip8->inst.X] & 1,
							chip8->V[chip8->inst.X] >> 1);
					break; 
				
				case 7:
					// 0x8XY7: Set register VX = VY - VX. VF = 0 when underflow, 1 otherwise
					printf("Set register V%X = V%X (0x%02x) - V%X (0x%02X), VF = 1 if no borrow; Result: 0x%02X, VF = %X\n",
							chip8->inst.X, chip8->inst.Y, chip8->V[chip8->inst.Y],
							chip8->inst.X, chip8->V[chip8->inst.X],
							chip8->V[chip8->inst.Y] - chip8->V[chip8->inst.X],
							(chip8->V[chip8->inst.X] <= chip8->V[chip8->inst.Y]));
					break;

				case 0xE:
					// 0x8XY8: Set register VX <<= 1, store LSB of VX prior to shift in VF
					printf("Set register V%X (0x%02X) <<= 1, VF = shfited off bit (%X); Result: 0x%02X\n",
							chip8->inst.X, chip8->V[chip8->inst.X],
							(chip8->V[chip8->inst.X] & 0x80) >> 7,
							chip8->V[chip8->inst.X] << 1);
					break;

				default:
					break;
			}
			break;

		case 0x09:
			// 0x9XY0: Skip next instruction if VX != VY

			printf("Check if V%X (0x%02X) != V%X (0x%02X), skip next instruction if true\n",
					chip8->inst.X, chip8->V[chip8->inst.X],
					chip8->inst.Y, chip8->V[chip8->inst.Y]);
			break;

		case 0x0A:
			// 0xANNN: Set index register I to NNN
			printf("Set I to NNN (0x%04X)\n", 
					chip8->inst.NNN);
			break;

		case 0x0B:
			// 0xBNNN: Set PC to (jump to) address NNN + V0
			printf("Set PC = NNN (0x%04X) + V0 (0x%02X).\n",
					chip8->inst.NNN, chip8->V[0x0]);
			break;

		case 0x0C:
			// 0xCXNN: Set VX = rand(0-255) & NN
			printf("Set V%X = rand() %% 256 & NN (0x%02X).\n",
					chip8->inst.X, chip8->inst.NN);
			break;

		case 0x0D:
			printf("Draw N (%u) height sprite at coords V%X (0x%02X), V%X (0x%02X) from memory location I (0x%04X). Set VF = 1 if any pixels are turned off.\n",
				chip8->inst.N, chip8->inst.X, 
				chip8->V[chip8->inst.X], chip8->inst.Y,
				chip8->V[chip8->inst.Y], chip8->I);
			break;

		case 0x0E:
			if(chip8->inst.NN == 0x9E) {
				// 0xEX9E: Skip next instruction if key in VX is pressed
				printf("Skip next isntruction if key in V%X (0x%02X) is pressed; keypad value: %d\n",
						chip8->inst.X, chip8->V[chip8->inst.X], chip8->keypad[chip8->V[chip8->inst.X]]);
				
			} else if(chip8->inst.NN == 0xA1) {
				//0xEXA1: Skip next instruction if key in VX is not pressed
				printf("Skip next isntruction if key in V%X (0x%02X) is not pressed; keypad value: %d\n",
						chip8->inst.X, chip8->V[chip8->inst.X], chip8->keypad[chip8->V[chip8->inst.X]]);
			}
			break;

		case 0x0F:
			switch(chip8->inst.NN) {
				case 0x0A:
					// 0xFX0A: VX = get_key(); Wait for a key press and store in VX
					printf("Await until a key is pressed; Store key in V%X\n.",
							chip8->inst.X);
					break;

				case 0x1E:
					// 0xFX1E: Add VX to I (I += VX), VF not affected
					printf("I (0x%04X) += V%X (0x%02X); Result: (I): 0x%04X.\n",
							chip8->I, chip8->inst.X, chip8->V[chip8->inst.X],
							chip8->I + chip8->V[chip8->inst.X]);
					break;

				case 0x07:
					// 0xFX07: Set VX = value of the delay timer
					printf("Set V%X = delay timer value (0x%02X).\n",
							chip8->inst.X, chip8->delay_timer);
					break;
				
				case 0x15:
					// 0xFX15: Set delay timer = VX
					printf("Set delay timer value = V%X (0x%02X).\n",
							chip8->delay_timer, chip8->inst.X);
					break;

				case 0x18:
					// 0xFX18: Set sound timer = VX
					printf("Set sound timer value = V%X (0x%02X).\n",
							chip8->inst.X, chip8->V[chip8->inst.X]);
					break;

				case 0x29:
					// 0xFX29: Set regist I to the location of the sprite in memory for the character [0x0-0xF] represented by a [4x5] font
					prinft("Set I to sprite location in memory for character in V%X (0x%02X). Result * 5 = \n",
							chip8->inst.X, chip8->V[chip8->inst.X] * 5);
					break;

				default:
					break;
			}
			break;

		default:
			printf("Uninplemented Opcode.\n");
			break;	// Unimplmented or invalid opcode
	}
}
#endif

// Emulate 1 CHIP8 instruction
void emulate_instruction(chip8_t *chip8, const config_t config) {
	// Get next opcode from ram
	chip8->inst.opcode = (chip8->ram[chip8->PC] << 8) | chip8->ram[chip8->PC+1];
	chip8->PC += 2;	// Pre-inc program counter for next opcode

	// Fill out current instruction format
	chip8->inst.NNN = chip8->inst.opcode & 0x0FFF;
	chip8->inst.NN = chip8->inst.opcode & 0x0FF;
	chip8->inst.N = chip8->inst.opcode & 0x0F;
	chip8->inst.X = (chip8->inst.opcode >> 8) & 0x0F;
	chip8->inst.Y = (chip8->inst.opcode >> 4) & 0x0F;

#ifdef DEBUG
	print_debug_info(chip8);
#endif

	// Emulate opcode
	switch((chip8->inst.opcode >> 12) & 0x0F) {
		case 0x00:
			if(chip8->inst.NNN == 0xE0) {
				// 0x00E0: Clear the screen
				memset(&chip8->display[0], false, sizeof(chip8->display));
			} else if(chip8->inst.NN == 0xEE) {
				// 0x00EE: Return from subroutine
				chip8->PC = *--chip8->stack_pointer;
			} else {
				// Uninplemented Opcode
			}
			break;

		case 0x01:
			// 0x1NNN: Jump to address NNN
			chip8->PC = chip8->inst.NNN;
			break;

		case 0x02:
			// 0x2NNN: Call subroutine at NNN
			*chip8->stack_pointer++ = chip8->PC;
			chip8->PC = chip8->inst.NNN;
			break;

		case 0x03:
			// 0x3XNN: Skip next instruction if VX == NN
			if(chip8->V[chip8->inst.X] == chip8->inst.NN) chip8->PC += 2;
			break;

		case 0x04:
			// 0x4XNN: Skip next instruction if VX != NN
			if(chip8->V[chip8->inst.X] != chip8->inst.NN) chip8->PC += 2;
			break;

		case 0x05:
			// 0x5XY0: Skip next instruction if VX == VY
			if(chip8->V[chip8->inst.X] == chip8->V[chip8->inst.Y]) chip8->PC += 2;
			break;

		case 0x06:
			// 0x6XNN: Set register VX to NN
			chip8->V[chip8->inst.X] = chip8->inst.NN;
			break;
		
		case 0x07:
			// 0x7XNN: Set register VX += NN
			chip8->V[chip8->inst.X] += chip8->inst.NN;
			break;

		case 0x08:
			switch(chip8->inst.N) {
				case 0:
					// 0x8XY0: Set register VX = VY
					chip8->V[chip8->inst.X] = chip8->V[chip8->inst.Y];
					break;

				case 1:
					// 0x8XY1: Set register VX |= VY
					chip8->V[chip8->inst.X] |= chip8->V[chip8->inst.Y];
					break;

				case 2:
					// 0x8XY2: Set register VX &= VY
					chip8->V[chip8->inst.X] &= chip8->V[chip8->inst.Y];
					break;

				case 3:
					// 0x8XY3: Set register VX ^= VY
					chip8->V[chip8->inst.X] ^= chip8->V[chip8->inst.Y];
					break;
				
				case 4:
					// 0x8XY4: Set register VX += VY set VF to 1 if carry, otherwise 0
					if((uint16_t)(chip8->V[chip8->inst.X] + chip8->V[chip8->inst.Y]) > 255) 
						chip8->V[0xF] = 1;

					(chip8->V[chip8->inst.X] += chip8->V[chip8->inst.Y]);
					break;
				
				case 5:
					// 0x8XY5: Set register VX -= VY set VF to 1 if VX >= VY (undercarry)
					if(chip8->V[chip8->inst.X] >= chip8->V[chip8->inst.Y])
						chip8->V[0xF] = 1;

					chip8->V[chip8->inst.X] -= chip8->V[chip8->inst.Y];
					break;

				case 6:
					// 0x8XY6: Set register VX >>= 1, store LSB of VX prior to shift in VF
					chip8->V[0xF] = chip8->V[chip8->inst.X] & 1;
					chip8->V[chip8->inst.X] >>= 1; 
					break;
				
				case 7:
					// 0x8XY7: Set register VX = VY - VX. VF = 0 when underflow, 1 otherwise
					if(chip8->V[chip8->inst.X] <= chip8->V[chip8->inst.Y])
						chip8->V[0xF] = 1;

					chip8->V[chip8->inst.X] = chip8->V[chip8->inst.Y] - chip8->V[chip8->inst.X];
					break;

				case 0xE:
					// 0x8XY8: Set register VX <<= 1, store LSB of VX prior to shift in VF
					chip8->V[0xF] = (chip8->V[chip8->inst.X] & 0x80) >> 7;
					chip8->V[chip8->inst.X] <<= 1; 
					break;

				default:
					break;
			}
			break;

		case 0x09:
			// 0x9XY0: Skip next instruction if VX != VY
			if(chip8->V[chip8->inst.X] != chip8->V[chip8->inst.Y])
				chip8->PC += 2;
			break;

		case 0x0A:
			// 0xANNN: Set index register I to NNN
			chip8->I = chip8->inst.NNN;
			break;

		case 0x0B:
			// 0xBNNN: Set PC to (jump to) address NNN + V0
			chip8->PC = chip8->inst.NNN + chip8->V[0x0];
			break;

		case 0x0C:
			// 0xCXNN: Set VX = rand(0-255) & NN
			chip8->V[chip8->inst.X] = ((rand() % 256) & chip8->inst.NN);
			break;

		case 0x0D: {
			// 0xDXYN: Draw N height sprite at coords X,Y; Read from memory location I;
			//	Screen pixels are XOR'd with sprite bits,
			//	VF (Carry flag) is set it any screen pixels are set off; This is usefull for collision detection
			uint8_t X_coord = chip8->V[chip8->inst.X] % config.window_width;
			uint8_t Y_coord = chip8->V[chip8->inst.Y] % config.window_height;
			const uint8_t orig_X = X_coord; // Orig X value

			chip8->V[0xF] = 0;	// Init carry flag to 0

			for(uint8_t i = 0; i < chip8->inst.N; i++) {
				// Get next byte/row of sprite data
				const uint8_t sprite_data = chip8->ram[chip8->I + i];
				X_coord = orig_X;	// Reset X for next row to draw

				for(int8_t j = 7; j >= 0; j--) {
					// If sprite pizel/bit is on and display pizel is on, set the carry flag
					bool *pixel = &chip8->display[Y_coord * config.window_width + X_coord];
					const bool sprite_bit = (sprite_data & (1 << j));

					if(sprite_bit && *pixel) {
						chip8->V[0xF] = 1;
					}

					// XOR display pixel with sprite pixel/bit to set it on or off
					*pixel ^= sprite_bit;

					// Stop drawing row if hit right edge of screen
					if(++X_coord >= config.window_width) break;
				}
				// Stop drawing entire sprite if hit bottom edge of screen
				if(++Y_coord >= config.window_height) break;
			}
			break;
			}	

		case 0x0E:
			if(chip8->inst.NN == 0x9E) {
				// 0xEX9E: Skip next instruction if key in VX is pressed
				if(chip8->keypad[chip8->V[chip8->inst.X]])
					chip8->PC += 2;
				
			} else if(chip8->inst.NN == 0xA1) {
				//0xEXA1: Skip next instruction if key in VX is not pressed
				if(!chip8->keypad[chip8->V[chip8->inst.X]])
					chip8->PC += 2;
			}
			break;

		case 0x0F:
			switch(chip8->inst.NN) {
				case 0x0A:
					// 0xFX0A: VX = get_key(); Wait for a key press and store in VX
					bool any_key_pressed = false;
					for(uint8_t i = 0; i < sizeof chip8->keypad; i++) {
						if(chip8->keypad[i]) {
							chip8->V[chip8->inst.X] = i;	// i = key (offset into keypad array)
							any_key_pressed = true;
							break;
						}
					} 

					if(!any_key_pressed) chip8->PC -= 2;	// keep getting current opcode and running instruction if no key is pressed

					break;

				case 0x1E:
					// 0xFX1E: Add VX to I (I += VX), VF not affected
					chip8->I += chip8->V[chip8->inst.X];
					break;

				case 0x07:
					// 0xFX07: Set VX = value of the delay timer
					chip8->V[chip8->inst.X] = chip8->delay_timer;
					break;
				
				case 0x15:
					// 0xFX15: Set delay timer = VX
					chip8->delay_timer = chip8->V[chip8->inst.X];
					break;

				case 0x18:
					// 0xFX18: Set sound timer = VX
					chip8->sound_timer = chip8->V[chip8->inst.X];
					break;

				case 0x29:
					// 0xFX29: Set regist I to the location of the sprite in memory for the character [0x0-0xF] represented by a [4x5] font
					chip8->I = chip8->V[chip8->inst.X] * 5;
					break;

				default:
					break;
			}
			break;

		default:
			break;	// Unimplmented or invalid opcode
	}
}

int main(int argc, char **argv) {
	// Default usage message for args
	if(argc < 2) {
		fprintf(stderr, "Usage: %s <rom_name>\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	// Init emulator config/options
	config_t config = {0};
	if(!set_config_from_args(&config, argc, argv)) exit(EXIT_FAILURE);

	// Init SDL
	sdl_t sdl = {0};
	if(!init_sdl(&sdl, config)) exit(EXIT_FAILURE);

	// Init CHIP8 machine
	chip8_t chip8 = {0};
	const char *rom_name = argv[1];
	if(!init_chip8(&chip8, rom_name)) exit(EXIT_FAILURE);

	// Init screen clear to background color
	clear_screen(sdl, config);

	// Seed random number generator
	srand(time(NULL));

	// Main emulator loop
	while(chip8.state != QUIT){
		//handle user input
		handle_input(&chip8);
		if(chip8.state == PAUSED) continue;

		// emulate chip8 instructions
		emulate_instruction(&chip8, config);

		// Delay for approx 60hz
		SDL_Delay(16);
		// Update window with changes
		update_screen(sdl, config, chip8);
	}

	// Final cleanup
	final_cleanup(sdl);

	exit(EXIT_SUCCESS);

}
