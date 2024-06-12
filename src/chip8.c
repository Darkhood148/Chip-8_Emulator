#include <stdbool.h>
#include "SDL2/SDL.h"

//sdl struct
typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
} sdl_t;

//config stuff
typedef struct {
    uint32_t windowWidth;
    uint32_t windowHeight;
    uint32_t fgColor;
    uint32_t bgColor;
    uint32_t scaleFactor;
} config_t;

//emulator states
typedef enum {
    QUIT,
    RUNNING,
    PAUSED,
} emulator_state_t;

//instruction struct
typedef struct {
    uint16_t opcode;
    uint16_t NNN; // 12 bit address/constant
    uint8_t NN;   //  8 bit constant
    uint8_t N;    //  4 bit constant
    uint8_t X;    //  4 bit register identifier
    uint8_t Y;    //  4 bit register identifier
} instruction_t;

//chip8 struct
typedef struct {
    emulator_state_t state;
    uint8_t ram[4096];
    bool display[64 * 32];
    uint16_t stack[12];
    uint16_t *stackPtr;
    uint8_t V[16]; //V0-VF registers
    uint16_t I; //Index Register
    uint16_t PC; //Program counter
    uint8_t delayTimer;
    uint8_t soundTimer;
    bool keypad[16];
    instruction_t inst;
    const char *romName;
} chip8_t;

//sets configurations for sdl/window
bool set_config(config_t *config, int argc, char **argv) {
    *config = (config_t) {
            .windowWidth = 64,
            .windowHeight = 32,
            .fgColor = 0xFFFFFFFF,
            .bgColor = 0x000000FF,
            .scaleFactor = 20,
    };
}

//inits SDL
bool init_sdl(sdl_t *sdl, const config_t config) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
        SDL_Log("Error Occurred: %s", SDL_GetError());
        return false;
    }
    sdl->window = SDL_CreateWindow("CHIP8-Emulator", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                   config.windowWidth * config.scaleFactor,
                                   config.windowHeight * config.scaleFactor, 0);
    if (!sdl->window) {
        SDL_Log("Could not create window: %s\n", SDL_GetError());
        return false;
    }
    sdl->renderer = SDL_CreateRenderer(sdl->window, -1, SDL_RENDERER_ACCELERATED);
    return true;
}

//Initialize Chip-8
bool init_chip8(chip8_t *chip8, const char *romName) {
    const uint32_t entryPoint = 0x200;
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

    memcpy(&chip8->ram[0], &font[0], sizeof(font));

    FILE *rom = fopen(romName, "rb");
    if (!rom) {
        SDL_Log("File: %s does not exist", romName);
        return false;
    }

    fseek(rom, 0, SEEK_END);
    const size_t romSize = ftell(rom);
    const size_t maxSize = sizeof(chip8->ram) - entryPoint;
    rewind(rom);

    if (romSize > maxSize) {
        SDL_Log("Rom file too big");
        return false;
    }

    if (fread(&chip8->ram[entryPoint], romSize, 1, rom) != 1) {
        SDL_Log("Could not read file into ram");
        return false;
    }

    fclose(rom);
    chip8->state = RUNNING;
    chip8->PC = entryPoint;
    chip8->romName = romName;
    chip8->stackPtr = &chip8->stack[0];
    return true;
}

#ifdef DEBUG
void print_debug_info(chip8_t *chip8) {
    printf("Adress: 0x%04X, Opcode: 0x%04X Desc: ", chip8->PC-2, chip8->inst.opcode);
    switch ((chip8->inst.opcode >> 12) & 0x000F) {
    case 0x0:
        //clear the screen
        if(chip8->inst.NN == 0xE0) {
            printf("Clears the screen\n");
        } else if (chip8->inst.NN == 0xEE) {
            printf("Returned from subroutine to address 0x%04X\n", *(chip8->stackPtr - 1));
        } else {
            printf("Unimplemented Opcode")
        }
        break;
    case 0x02:
        printf("Calls subroutine at NNN\n");
        break;
    case 0x06:
        printf("Sets value of register V%X to NN (0x%02X)\n", chip8->inst.X, chip8->inst.NN);
        break;
    case 0x0A:
        printf("Sets instruction register to NNN (0x%04X)\n", chip8->inst.NNN);
        break;
    case 0x0D:
        printf("Drawing");
        break;
    default:
        printf("Unimplemented Opcode\n");
        break;
    }
}
#endif

//Quit SDL
bool quit_sdl(const sdl_t sdl) {
    SDL_DestroyRenderer(sdl.renderer);
    SDL_DestroyWindow(sdl.window);
    SDL_Quit();
}

//Update backbuffer to a color
void clear_screen(const sdl_t sdl, const config_t config) {
    uint8_t r = (config.bgColor >> 24) & 0xFF;
    uint8_t g = (config.bgColor >> 16) & 0xFF;
    uint8_t b = (config.bgColor >> 8) & 0xFF;
    uint8_t a = (config.bgColor >> 0) & 0xFF;

    SDL_SetRenderDrawColor(sdl.renderer, r, g, b, a);
    SDL_RenderClear(sdl.renderer);
}

//bring backbuffer to screen
void update_screen(const sdl_t sdl) {
    SDL_RenderPresent(sdl.renderer);
}

//handle user events
void handle_input(chip8_t *chip8) {
    SDL_Event event;

    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_QUIT:
                chip8->state = QUIT;
                return;
            case SDL_KEYUP:
                break;
            case SDL_KEYDOWN:
                switch (event.key.keysym.sym) {
                    case SDLK_ESCAPE:
                        chip8->state = QUIT;
                        return;
                    case SDLK_SPACE:
                        if (chip8->state == RUNNING) {
                            chip8->state = PAUSED;
                            puts("====Paused====");
                        } else if (chip8->state == PAUSED) {
                            chip8->state = RUNNING;
                            puts("====Resumed====");
                        }
                        return;
                    default:
                        return;
                }
                break;
            default:
                break;
        }
    }
}

void emulate_instruction(chip8_t *chip8, config_t config) {
    chip8->inst.opcode = (chip8->ram[chip8->PC] << 8) | chip8->ram[chip8->PC + 1];
    chip8->PC += 2;

    chip8->inst.NNN = chip8->inst.opcode & 0x0FFF;
    chip8->inst.NN = chip8->inst.opcode & 0x00FF;
    chip8->inst.N = chip8->inst.opcode & 0x000F;
    chip8->inst.X = (chip8->inst.opcode >> 8) & 0x000F;
    chip8->inst.Y = (chip8->inst.opcode >> 4) & 0x000F;

#ifdef DEBUG
    print_debug_info(chip8);
#endif

    switch ((chip8->inst.opcode >> 12) & 0x000F) {
        case 0x0:
            if (chip8->inst.NN == 0XE0) {
                // 0x00E0 Clear the screen
                memset(&chip8->display[0], false, sizeof(chip8->display));
            } else if (chip8->inst.NN == 0xEE) {
                // 0x00EE Return from subroutine
                chip8->PC = *--chip8->stackPtr;
            }
            break;
        case 0x02:
            // 0x2NNN call subroutine at NNN
            *chip8->stackPtr++ = chip8->PC;
            chip8->PC = chip8->inst.NNN;
            break;
        case 0x06:
            // 0x6XNN sets value of register VX to NN
            chip8->V[chip8->inst.X] = chip8->inst.NN;
            break;
        case 0x0A:
            // 0xANNN Sets index register I to NNN
            chip8->I = chip8->inst.NNN;
            break;
        case 0x0D: {
            // 0xDXYN Draws N-height sprites at cords X,Y; Read from memory location I
            // Screen pixels are XOR'd with sprite bits
            // VF (Carry Flag) is set if any screen pixels are switched off
            uint8_t X_cord = chip8->V[chip8->inst.X] % config.windowWidth;
            uint8_t Y_Cord = chip8->V[chip8->inst.Y] % config.windowHeight;
            const uint8_t orig_X = X_cord;

            chip8->V[0xF] = 0;

            for (uint8_t i = 0; i < chip8->inst.N; i++) {

                const uint8_t sprite_data = chip8->ram[chip8->I + i];
                X_cord = orig_X;

                for (int8_t j = 7; j >= 0; j--) {
                    bool *pixel = &chip8->display[Y_Cord * config.windowWidth + X_cord];
                    const bool sprite_bit = (sprite_data && (1 << j));

                    if (sprite_bit && *pixel) {
                        chip8->V[0xF] = 1;
                    }

                    *pixel ^= sprite_bit;

                    if (++X_cord >= config.windowWidth)
                        break;
                }

                if (++Y_Cord >= config.windowHeight)
                    break;
            }
            break;
        }
        default:
            break;
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <rom-path>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    sdl_t sdl = {0};
    config_t config = {0};
    chip8_t chip8 = {0};
    const char *romName = argv[1];
    if (!init_chip8(&chip8, romName))
        exit(EXIT_FAILURE);
    set_config(&config, argc, argv);
    if (!init_sdl(&sdl, config)) {
        exit(EXIT_FAILURE);
    }
    clear_screen(sdl, config);
    while (chip8.state != QUIT) {
        handle_input(&chip8);
        if (chip8.state == PAUSED)
            continue;
        emulate_instruction(&chip8, config);
        update_screen(sdl);
        SDL_Delay(16);
    }
    quit_sdl(sdl);
    exit(EXIT_SUCCESS);
}
