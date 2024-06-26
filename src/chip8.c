#include <stdbool.h>
#include "SDL2/SDL.h"
#include "stdint.h"
#include "time.h"

//sdl struct
typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_AudioSpec want, have;
    SDL_AudioDeviceID dev;
} sdl_t;

//CHIP8 Extension
typedef enum {
    CHIP8,
    SUPERCHIP,
    XOCHIP,
} extension_t;

//config stuff
typedef struct {
    uint32_t windowWidth;
    uint32_t windowHeight;
    uint32_t fgColor;
    uint32_t bgColor;
    uint32_t scaleFactor;
    bool pixelOutlines;
    uint32_t insts_per_second;
    uint32_t square_wave_freq;
    uint32_t audio_sample_rate;
    int16_t volume;
    float color_lerp_rate;
    extension_t extension;
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
    uint32_t pixel_color[64 * 32];
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
    bool draw;
} chip8_t;

uint32_t color_lerp(const uint32_t start_color, const uint32_t end_color, const float t) {
    const uint8_t s_r = (start_color >> 24) & 0xFF;
    const uint8_t s_g = (start_color >> 16) & 0xFF;
    const uint8_t s_b = (start_color >> 8) & 0xFF;
    const uint8_t s_a = (start_color >> 0) & 0xFF;

    const uint8_t e_r = (end_color >> 24) & 0xFF;
    const uint8_t e_g = (end_color >> 16) & 0xFF;
    const uint8_t e_b = (end_color >> 8) & 0xFF;
    const uint8_t e_a = (end_color >> 0) & 0xFF;

    const uint8_t ret_r = ((1 - t) * s_r) + (t * e_r);
    const uint8_t ret_g = ((1 - t) * s_g) + (t * e_g);
    const uint8_t ret_b = ((1 - t) * s_b) + (t * e_b);
    const uint8_t ret_a = ((1 - t) * s_a) + (t * e_a);

    return (ret_r << 24) | (ret_g << 16) | (ret_b << 8) | ret_a;
}

//sets configurations for sdl/window
bool set_config(config_t *config, int argc, char **argv) {
    *config = (config_t) {
            .windowWidth = 64,
            .windowHeight = 32,
            .fgColor = 0xFFFFFFFF,
            .bgColor = 0x000000FF,
            .scaleFactor = 20,
            .pixelOutlines = true,
            .insts_per_second = 500,
            .square_wave_freq = 440,
            .audio_sample_rate = 44100,
            .volume = 3000,
            .color_lerp_rate = 0.7f,
            .extension = CHIP8,
    };

    for (int i = 0; i < argc; i++) {
        if (strncmp(argv[i], "--scale-factor", strlen("--scale-factor")) == 0) {
            config->scaleFactor = (uint32_t) strtol(argv[i], NULL, 10);
        }
    }
}

void audio_callback(void *user_data, uint8_t *stream, int len) {
    config_t *config = (config_t *) user_data;
    int16_t *audio_data = (int16_t *) stream;
    static uint32_t running_sample_index = 0;
    const int32_t square_wave_period = config->audio_sample_rate / config->square_wave_freq;
    const int32_t half_square_wave_period = square_wave_period / 2;

    for (int i = 0; i < len / 2; i++) {
        audio_data[i] = ((running_sample_index++ / half_square_wave_period) % 2) ? config->volume : -config->volume;
    }
}

//inits SDL
bool init_sdl(sdl_t *sdl, config_t *config) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
        SDL_Log("Error Occurred: %s", SDL_GetError());
        return false;
    }
    sdl->window = SDL_CreateWindow("CHIP8-Emulator", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                   config->windowWidth * config->scaleFactor,
                                   config->windowHeight * config->scaleFactor, 0);
    if (!sdl->window) {
        SDL_Log("Could not create window: %s\n", SDL_GetError());
        return false;
    }
    sdl->renderer = SDL_CreateRenderer(sdl->window, -1, SDL_RENDERER_ACCELERATED);

    sdl->want = (SDL_AudioSpec) {
            .freq = 44100,
            .format = AUDIO_S16LSB,
            .channels = 1,
            .samples = 512,
            .callback = audio_callback,
            .userdata = config,
    };

    sdl->dev = SDL_OpenAudioDevice(NULL, 0, &sdl->want, &sdl->have, 0);

    if (!sdl->dev) {
        SDL_Log("Could not get Audio Device: %s\n", SDL_GetError());
        return false;
    }

    if (sdl->want.channels != sdl->have.channels || sdl->want.format != sdl->have.format) {
        SDL_Log("Could not get Audio Device with required specs: %s\n", SDL_GetError());
        return false;
    }

    return true;
}

//Initialize Chip-8
bool init_chip8(chip8_t *chip8, const config_t config, const char *romName) {
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

    memset(chip8, 0, sizeof(chip8_t));

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
    memset(&chip8->pixel_color[0], config.bgColor, sizeof(chip8->pixel_color));
    return true;
}

#ifdef DEBUG

void print_debug_info(chip8_t *chip8) {
    printf("Adress: 0x%04X, Opcode: 0x%04X Desc: ", chip8->PC - 2, chip8->inst.opcode);
    switch ((chip8->inst.opcode >> 12) & 0x000F) {
        case 0x0:
            //clear the screen
            if (chip8->inst.NN == 0xE0) {
                printf("Clears the screen\n");
            } else if (chip8->inst.NN == 0xEE) {
                printf("Returned from subroutine to address 0x%04X\n", *(chip8->stackPtr - 1));
            } else {
                printf("Unimplemented Opcode\n");
            }
            break;
        case 0x01:
            printf("Jumps to address: 0x%04X\n", chip8->inst.NNN);
            break;
        case 0x02:
            printf("Calls subroutine at NNN\n");
            break;
        case 0x03:
            printf("Checks if value of V%X (0x%02X) is equal to NN (0x%02X). If yes, skips next instruction (0x%04X)\n",
                   chip8->inst.X, chip8->V[chip8->inst.X], chip8->inst.NN, chip8->PC);
            break;
        case 0x04:
            printf("Checks if value of V%X (0x%02X) is not equal to NN (0x%02X). If yes, skips next instruction (0x%04X)\n",
                   chip8->inst.X, chip8->V[chip8->inst.X], chip8->inst.NN, chip8->PC);
            break;
        case 0x05:
            printf("Checks if value of V%X (0x%02X) is equal to V%X (0x%02X). If yes skips next instruction (0x%04X)\n",
                   chip8->inst.X, chip8->V[chip8->inst.X], chip8->inst.Y, chip8->V[chip8->inst.Y], chip8->PC);
            break;
        case 0x06:
            printf("Sets value of register V%X to NN (0x%02X)\n", chip8->inst.X, chip8->inst.NN);
            break;
        case 0x07:
            printf("Adds NN (0x%02X) to V%X\n", chip8->inst.NN, chip8->inst.X);
            break;
        case 0x08:
            switch (chip8->inst.N) {
                case 0x0:
                    // 0x8XY0 sets VX = VY
                    printf("Sets V%X (0x%02X) to V%X (0x%02X)\n", chip8->inst.X, chip8->V[chip8->inst.X], chip8->inst.Y,
                           chip8->V[chip8->inst.Y]);
                    break;
                case 0x1:
                    // 0x8XY1 performs VX |= VY
                    printf("Performs V%X (0x%02X) |= V%X (0x%02X)\n", chip8->inst.X, chip8->V[chip8->inst.X],
                           chip8->inst.Y, chip8->V[chip8->inst.Y]);
                    break;
                case 0x2:
                    // 0x8XY2 performs VX &= VY
                    printf("Performs V%X (0x%02X) &= V%X (0x%02X)\n", chip8->inst.X, chip8->V[chip8->inst.X],
                           chip8->inst.Y, chip8->V[chip8->inst.Y]);
                    break;
                case 0x3:
                    // 0x8XY3 performs VX ^= VY
                    printf("Performs V%X (0x%02X) ^= V%X (0x%02X)\n", chip8->inst.X, chip8->V[chip8->inst.X],
                           chip8->inst.Y, chip8->V[chip8->inst.Y]);
                    break;
                case 0x4:
                    // 0x8XY4 sets VX += VY sets VF = 1 if there is an overflow otherwise sets VF = 0
                    printf("Performs V%X (0x%02X) += V%X (0x%02X) and sets VF as per overflow occuring/not occuring\n",
                           chip8->inst.X, chip8->V[chip8->inst.X], chip8->inst.Y, chip8->V[chip8->inst.Y]);
                    break;
                case 0x5:
                    // 0x8XY5 performs VX -= VY sets VF = 0 if there is an underflow otherwise sets VF = 1
                    printf("Performs V%X (0x%02X) += V%X (0x%02X) and sets VF as per underflow occuring/not occuring\n",
                           chip8->inst.X, chip8->V[chip8->inst.X], chip8->inst.Y, chip8->V[chip8->inst.Y]);
                    break;
                case 0x6:
                    // 0x8XY6 performs VX >>= VY and sets VF to the LSB of VX prior to shift
                    printf("Performs V%X (0x%02X) >>= 1 and sets VF to the LSB of V%X prior to shift\n", chip8->inst.X,
                           chip8->V[chip8->inst.X], chip8->inst.X);
                    break;
                case 0x7:
                    // 0x8XY7 performs VX = VY - VX and sets VF = 0 when there is an underflow else set VF = 0
                    printf("Performs V%X (0x%02X) = V%X (0x%02X) - V%X and sets VF as per underflow occuring/not occuring\n",
                           chip8->inst.X, chip8->V[chip8->inst.X], chip8->inst.Y, chip8->V[chip8->inst.Y],
                           chip8->inst.X);
                    break;
                case 0xE:
                    // 0x8XYE performs VX <<= VY and sets VF to the MSB of VX prior to shift
                    printf("Performs V%X (0x%02X) <<= 1 and sets VF to the MSB of V%X prior to shift\n", chip8->inst.X,
                           chip8->V[chip8->inst.X], chip8->inst.X);
                    break;
            }
            break;
        case 0x09:
            // 0x9XY0 Skips the next instruction if VX != VY
            printf("Skips the next instruction if V%X (0x%02X) != V%X (0x%02X)\n", chip8->inst.X,
                   chip8->V[chip8->inst.X], chip8->inst.Y, chip8->V[chip8->inst.Y]);
            break;
        case 0x0A:
            printf("Sets instruction register to NNN (0x%04X)\n", chip8->inst.NNN);
            break;
        case 0x0B:
            // 0xBNNN sets PC to V0 + NNN
            printf("Sets the PC to V0 (0x%02X) + NNN (0x%04X)", chip8->V[0x0], chip8->inst.NNN);
            break;
        case 0x0C:
            printf("Sets V%X (0x%02X) equal to rand() & NN (0x%02X)\n", chip8->inst.X, chip8->V[chip8->inst.X],
                   chip8->inst.NN);
            break;
        case 0x0D:
            printf("Drawing\n");
            break;
        case 0x0E:
            if (chip8->inst.NN == 0x9E) {
                // 0xEX9E skips next instruction if key stored in VX is pressed
                printf("Skips next instruction if key stored in V%X (0x%02X) is pressed\n", chip8->inst.X,
                       chip8->V[chip8->inst.X]);
            } else if (chip8->inst.NN == 0xA1) {
                // 0xEXA1 skips next instruction if key stored in VX is not pressed
                printf("Skips next instruction if key stored in V%X (0x%02X) is not pressed\n", chip8->inst.X,
                       chip8->V[chip8->inst.X]);
            } else {
                printf("Unimplemented Opcode\n");
            }
            break;
        case 0x0F:
            switch (chip8->inst.NN) {
                case 0x07:
                    printf("Sets V%X (0x%02X) = delayTimer (%d)\n", chip8->inst.X, chip8->V[chip8->inst.X],
                           chip8->delayTimer);
                    // 0xFX0A await a key press, then store it in VX
                case 0x0A:
                    printf("Awaiting a key press to store in V%X\n", chip8->inst.X);
                    break;
                    // 0xFX15 sets delayTimer = VX
                case 0x15:
                    printf("Sets delayTimer (%d) = V%X (0x%02X)\n", chip8->delayTimer, chip8->inst.X,
                           chip8->V[chip8->inst.X]);
                    break;
                    // 0xFX18 sets soundTimer = VX
                case 0x18:
                    printf("Sets soundTimer (%d) = V%X (0x%02X)\n", chip8->soundTimer, chip8->inst.X,
                           chip8->V[chip8->inst.X]);
                    break;
                    // 0xFX1E performs I += VX
                case 0x1E:
                    printf("Performs I (0x%04X) += V%X (0x%02X)\n", chip8->I, chip8->inst.X, chip8->V[chip8->inst.X]);
                    break;
                    // 0xFX29 sets I = location of font in VX
                case 0x29:
                    printf("Sets I = address of font in V%X (0x%02X)\n", chip8->inst.X, chip8->V[chip8->inst.X]);
                    break;
                case 0x33:
                    printf("Stores BCD format of V%X (0x%02X) at I (0x%04X)\n", chip8->inst.X, chip8->V[chip8->inst.X],
                           chip8->I);
                    break;
                case 0x55:
                    printf("Performs regdump of V0-V%X at address I (0x%04X)\n", chip8->inst.X, chip8->I);
                    break;
                case 0x65:
                    printf("Performs regload of V0-V%X at address I (0x%04X)\n", chip8->inst.X, chip8->I);
                    break;

            }
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
    SDL_CloseAudioDevice(sdl.dev);
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
void update_screen(const sdl_t sdl, const config_t config, chip8_t *chip8) {
    SDL_Rect rect = {.x = 0, .y = 0, .w = config.scaleFactor, .h = config.scaleFactor};

    const uint8_t bg_r = (config.bgColor >> 24) & 0xFF;
    const uint8_t bg_g = (config.bgColor >> 16) & 0xFF;
    const uint8_t bg_b = (config.bgColor >> 8) & 0xFF;
    const uint8_t bg_a = (config.bgColor >> 0) & 0xFF;

    for (uint32_t i = 0; i < sizeof(chip8->display); i++) {
        rect.x = (i % config.windowWidth) * config.scaleFactor;
        rect.y = (i / config.windowWidth) * config.scaleFactor;
//        printf("Bruh %d %d %d\n", rect.x, rect.y, chip8->display[i]);

        if (chip8->display[i]) {
            if (chip8->pixel_color[i] != config.fgColor) {
                chip8->pixel_color[i] = color_lerp(chip8->pixel_color[i], config.fgColor, config.color_lerp_rate);
            }
            const uint8_t r = (chip8->pixel_color[i] >> 24) & 0xFF;
            const uint8_t g = (chip8->pixel_color[i] >> 16) & 0xFF;
            const uint8_t b = (chip8->pixel_color[i] >> 8) & 0xFF;
            const uint8_t a = (chip8->pixel_color[i] >> 0) & 0xFF;

            SDL_SetRenderDrawColor(sdl.renderer, r, g, b, a);
            SDL_RenderFillRect(sdl.renderer, &rect);

            if (config.pixelOutlines) {
                SDL_SetRenderDrawColor(sdl.renderer, bg_r, bg_g, bg_b, bg_a);
                SDL_RenderDrawRect(sdl.renderer, &rect);
            }
        } else {
            SDL_SetRenderDrawColor(sdl.renderer, bg_r, bg_g, bg_b, bg_a);
            SDL_RenderFillRect(sdl.renderer, &rect);
        }
    }
    SDL_RenderPresent(sdl.renderer);
}

//handle user events
void handle_input(chip8_t *chip8, config_t *config) {
    SDL_Event event;

    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_QUIT:
                chip8->state = QUIT;
                return;
            case SDL_KEYUP:
                switch (event.key.keysym.sym) {
                    case SDLK_1:
                        chip8->keypad[0x1] = false;
                        break;
                    case SDLK_2:
                        chip8->keypad[0x2] = false;
                        break;
                    case SDLK_3:
                        chip8->keypad[0x3] = false;
                        break;
                    case SDLK_4:
                        chip8->keypad[0xC] = false;
                        break;
                    case SDLK_q:
                        chip8->keypad[0x4] = false;
                        break;
                    case SDLK_w:
                        chip8->keypad[0x5] = false;
                        break;
                    case SDLK_e:
                        chip8->keypad[0x6] = false;
                        break;
                    case SDLK_r:
                        chip8->keypad[0xD] = false;
                        break;
                    case SDLK_a:
                        chip8->keypad[0x7] = false;
                        break;
                    case SDLK_s:
                        chip8->keypad[0x8] = false;
                        break;
                    case SDLK_d:
                        chip8->keypad[0x9] = false;
                        break;
                    case SDLK_f:
                        chip8->keypad[0xE] = false;
                        break;
                    case SDLK_z:
                        chip8->keypad[0xA] = false;
                        break;
                    case SDLK_x:
                        chip8->keypad[0x0] = false;
                        break;
                    case SDLK_c:
                        chip8->keypad[0xB] = false;
                        break;
                    case SDLK_v:
                        chip8->keypad[0xF] = false;
                        break;
                    default:
                        break;
                }
                break;
            case SDL_KEYDOWN:
                switch (event.key.keysym.sym) {
                    case SDLK_ESCAPE:
                        chip8->state = QUIT;
                        break;
                    case SDLK_SPACE:
                        if (chip8->state == RUNNING) {
                            chip8->state = PAUSED;
                            puts("====Paused====");
                        } else if (chip8->state == PAUSED) {
                            chip8->state = RUNNING;
                            puts("====Resumed====");
                        }
                        return;
                    case SDLK_EQUALS:
                        init_chip8(chip8, *config, chip8->romName);
                        break;
                    case SDLK_j:
                        if (config->color_lerp_rate > 0)
                            config->color_lerp_rate -= 0.1f;
                        break;
                    case SDLK_k:
                        if (config->color_lerp_rate < 1)
                            config->color_lerp_rate += 0.1f;
                        break;
                    case SDLK_o:
                        if (config->volume > 0)
                            config->volume -= 500;
                        break;
                    case SDLK_p:
                        if (config->volume < INT16_MAX)
                            config->volume += 500;
                        break;
                    case SDLK_1:
                        chip8->keypad[0x1] = true;
                        break;
                    case SDLK_2:
                        chip8->keypad[0x2] = true;
                        break;
                    case SDLK_3:
                        chip8->keypad[0x3] = true;
                        break;
                    case SDLK_4:
                        chip8->keypad[0xC] = true;
                        break;
                    case SDLK_q:
                        chip8->keypad[0x4] = true;
                        break;
                    case SDLK_w:
                        chip8->keypad[0x5] = true;
                        break;
                    case SDLK_e:
                        chip8->keypad[0x6] = true;
                        break;
                    case SDLK_r:
                        chip8->keypad[0xD] = true;
                        break;
                    case SDLK_a:
                        chip8->keypad[0x7] = true;
                        break;
                    case SDLK_s:
                        chip8->keypad[0x8] = true;
                        break;
                    case SDLK_d:
                        chip8->keypad[0x9] = true;
                        break;
                    case SDLK_f:
                        chip8->keypad[0xE] = true;
                        break;
                    case SDLK_z:
                        chip8->keypad[0xA] = true;
                        break;
                    case SDLK_x:
                        chip8->keypad[0x0] = true;
                        break;
                    case SDLK_c:
                        chip8->keypad[0xB] = true;
                        break;
                    case SDLK_v:
                        chip8->keypad[0xF] = true;
                        break;
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
        case 0x01:
            chip8->PC = chip8->inst.NNN;
            break;
        case 0x02:
            // 0x2NNN call subroutine at NNN
            *chip8->stackPtr++ = chip8->PC;
            chip8->PC = chip8->inst.NNN;
            break;
        case 0x03:
            // checks if Vx == NN. If yes, skips next instruction
            if (chip8->V[chip8->inst.X] == chip8->inst.NN) {
                chip8->PC += 2;
            }
            break;
        case 0x04:
            // checks if Vx != NN. If yes, skips next instruction
            if (chip8->V[chip8->inst.X] != chip8->inst.NN) {
                chip8->PC += 2;
            }
            break;
        case 0x05:
            // 0x5XY0 checks if Vx == Vy. If yes, skips next instruction
            if (chip8->V[chip8->inst.X] == chip8->V[chip8->inst.Y]) {
                chip8->PC += 2;
            }
            break;
        case 0x06:
            // 0x6XNN sets value of register VX to NN
            chip8->V[chip8->inst.X] = chip8->inst.NN;
            break;
        case 0x07:
            // 0x7XNN adds VX to NN
            chip8->V[chip8->inst.X] += chip8->inst.NN;
            break;
        case 0x08:
            switch (chip8->inst.N) {
                case 0x0:
                    // 0x8XY0 sets VX = VY
                    chip8->V[chip8->inst.X] = chip8->V[chip8->inst.Y];
                    break;
                case 0x1:
                    // 0x8XY1 performs VX |= VY
                    chip8->V[chip8->inst.X] |= chip8->V[chip8->inst.Y];
                    if (config.extension == CHIP8)
                        chip8->V[0xF] = 0;
                    break;
                case 0x2:
                    // 0x8XY2 performs VX &= VY
                    chip8->V[chip8->inst.X] &= chip8->V[chip8->inst.Y];
                    if (config.extension == CHIP8)
                        chip8->V[0xF] = 0;
                    break;
                case 0x3:
                    // 0x8XY3 performs VX |= VY
                    chip8->V[chip8->inst.X] ^= chip8->V[chip8->inst.Y];
                    if (config.extension == CHIP8)
                        chip8->V[0xF] = 0;
                    break;
                case 0x4: {
                    // 0x8XY4 sets VX += VY sets VF = 1 if there is an overflow otherwise sets VF = 0
                    const bool carry = (uint16_t) chip8->V[chip8->inst.X] + chip8->V[chip8->inst.Y] > 0xFF;
                    chip8->V[chip8->inst.X] += chip8->V[chip8->inst.Y];
                    chip8->V[0xF] = carry;
                }
                    break;
                case 0x5: {
                    // 0x8XY5 performs VX -= VY sets VF = 0 if there is an underflow otherwise sets VF = 1
                    const bool carry = chip8->V[chip8->inst.X] >= chip8->V[chip8->inst.Y];
                    chip8->V[chip8->inst.X] -= chip8->V[chip8->inst.Y];
                    chip8->V[0xF] = carry;
                }
                    break;
                case 0x6: {
                    // 0x8XY6 performs VX >>= 1 and sets VF to the LSB of VX prior to shift
                    bool carry;
                    if (config.extension == CHIP8) {
                        carry = chip8->V[chip8->inst.Y] & 0x1;
                        chip8->V[chip8->inst.X] = chip8->V[chip8->inst.Y] >> 1;
                    } else {
                        carry = chip8->V[chip8->inst.X] & 0x1;
                        chip8->V[chip8->inst.X] >>= 1;
                    }
                    chip8->V[0xF] = carry;
                }
                    break;
                case 0x7: {
                    // 0x8XY7 performs VX = VY - VX and sets VF = 0 when there is an underflow else set VF = 0
                    const bool carry = chip8->V[chip8->inst.X] <= chip8->V[chip8->inst.Y];
                    chip8->V[chip8->inst.X] = chip8->V[chip8->inst.Y] - chip8->V[chip8->inst.X];
                    chip8->V[0xF] = carry;
                }
                    break;
                case 0xE: {
                    // 0x8XYE performs VX <<= VY and sets VF to the MSB of VX prior to shift
                    bool carry;
                    if (config.extension == CHIP8) {
                        carry = (chip8->V[chip8->inst.Y] & 0x80) >> 7;
                        chip8->V[chip8->inst.X] = chip8->V[chip8->inst.Y] << 1;
                    } else {
                        carry = (chip8->V[chip8->inst.X] & 0x80) >> 0x7;
                        chip8->V[chip8->inst.X] <<= 1;
                    }
                    chip8->V[0xF] = carry;
                }
                    break;
            }
            break;
        case 0x09:
            // 0x9XY0 Skips the next instruction if VX != VY
            if (chip8->V[chip8->inst.X] != chip8->V[chip8->inst.Y]) {
                chip8->PC += 2;
            }
            break;
        case 0x0A:
            // 0xANNN Sets index register I to NNN
            chip8->I = chip8->inst.NNN;
            break;
        case 0x0B:
            // 0xBNNN sets PC to V0 + NNN
            chip8->PC = chip8->V[0x0] + chip8->inst.NNN;
            break;
        case 0x0C:
            // 0xCXNN sets VX = rand() & NN
            chip8->V[chip8->inst.X] = (rand() % 256) & chip8->inst.NN;
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
                    const bool sprite_bit = (sprite_data & (1 << j));

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
            chip8->draw = true;
            break;
        }
        case 0x0E:
            if (chip8->inst.NN == 0x9E) {
                // 0xEX9E skips next instruction if key stored in VX is pressed
                if (chip8->keypad[chip8->V[chip8->inst.X]]) {
                    chip8->PC += 2;
                }
            } else if (chip8->inst.NN == 0xA1) {
                // 0xEXA1 skips next instruction if key stored in VX is not pressed
                if (!chip8->keypad[chip8->V[chip8->inst.X]]) {
                    chip8->PC += 2;
                }
            }
            break;
        case 0x0F:
            switch (chip8->inst.NN) {
                // 0FX07 sets VX = delayTimer
                case 0x07:
                    chip8->V[chip8->inst.X] = chip8->delayTimer;
                    break;
                    // 0xFX0A await a key press, then store it in VX
                case 0x0A: {
                    static bool any_key_pressed = false;
                    static uint8_t key = 0xFF;
                    for (uint8_t i = 0; key == 0xFF && i < sizeof(chip8->keypad); i++) {
                        if (chip8->keypad[i]) {
                            key = i;
                            any_key_pressed = true;
                        }
                    }
                    if (!any_key_pressed)
                        chip8->PC -= 2;
                    else {
                        if (chip8->keypad[key])
                            chip8->PC -= 2;
                        else {
                            chip8->V[chip8->inst.X] = key;
                            key = 0xFF;
                            any_key_pressed = false;
                        }
                    }
                    break;
                }
                    // 0xFX15 sets delayTimer = VX
                case 0x15:
                    chip8->delayTimer = chip8->V[chip8->inst.X];
                    break;
                    // 0xFX18 sets soundTimer = VX
                case 0x18:
                    chip8->soundTimer = chip8->V[chip8->inst.X];
                    break;
                    // 0xFX1E performs I += VX
                case 0x1E:
                    chip8->I += chip8->V[chip8->inst.X];
                    break;
                    // 0xFX29 sets I = location of font in VX
                case 0x29:
                    chip8->I = chip8->V[chip8->inst.X] * 5;
                    break;
                    // 0xFX33 sets bcd value of VX at I
                    // hundreds place at I, tens place at I+1, ones place at I+2
                case 0x33: {
                    uint8_t bcd = chip8->V[chip8->inst.X];
                    chip8->ram[chip8->I + 2] = bcd % 10;
                    bcd /= 10;
                    chip8->ram[chip8->I + 1] = bcd % 10;
                    bcd /= 10;
                    chip8->ram[chip8->I] = bcd % 10;
                    break;
                }
                case 0x55:
                    for (uint8_t i = 0; i <= chip8->inst.X; i++) {
                        if (config.extension == CHIP8)
                            chip8->ram[chip8->I++] = chip8->V[i];
                        else
                            chip8->ram[chip8->I + i] = chip8->V[i];
                    }
                    break;
                case 0x65:
                    for (uint8_t i = 0; i <= chip8->inst.X; i++) {
                        if (config.extension == CHIP8)
                            chip8->V[i] = chip8->ram[chip8->I++];
                        else
                            chip8->V[i] = chip8->ram[chip8->I + i];
                    }
                    break;
            }
            break;
        default:
            break;
    }
}

void update_timers(const sdl_t sdl, chip8_t *chip8) {
    if (chip8->delayTimer > 0)
        chip8->delayTimer--;
    if (chip8->soundTimer > 0) {
        chip8->soundTimer--;
        SDL_PauseAudioDevice(sdl.dev, 0);
    } else {
        SDL_PauseAudioDevice(sdl.dev, 1);
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
    if (!init_chip8(&chip8, config, romName))
        exit(EXIT_FAILURE);
    set_config(&config, argc, argv);
    if (!init_sdl(&sdl, &config)) {
        exit(EXIT_FAILURE);
    }
    clear_screen(sdl, config);
    srand(time(NULL));
    while (chip8.state != QUIT) {
        handle_input(&chip8, &config);
        if (chip8.state == PAUSED)
            continue;
        const uint64_t start = SDL_GetPerformanceCounter();
        for (uint32_t i = 0; i < config.insts_per_second / 60; i++)
            emulate_instruction(&chip8, config);
        const uint64_t end = SDL_GetPerformanceCounter();
        double time_elapsed = (double) ((end - start) * 1000) / SDL_GetPerformanceFrequency();
        SDL_Delay(16.67f > time_elapsed ? 16.67f - time_elapsed : 0);
        if (chip8.draw) {
            update_screen(sdl, config, &chip8);
            chip8.draw = false;
        }
        update_timers(sdl, &chip8);
    }
    quit_sdl(sdl);
    exit(EXIT_SUCCESS);
}
