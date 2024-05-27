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

//chip8 struct
typedef struct {
    emulator_state_t state;
} chip8_t;

bool set_config(config_t *config, int argc, char **argv) {
    *config = (config_t) {
            .windowWidth = 64,
            .windowHeight = 32,
            .fgColor = 0xFFFFFFFF,
            .bgColor = 0x00000000,
            .scaleFactor = 20,
    };
}

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
bool init_chip8(chip8_t *chip8) {
    chip8->state = RUNNING;
    return true;
}

bool quit_sdl(const sdl_t sdl) {
    SDL_DestroyRenderer(sdl.renderer);
    SDL_DestroyWindow(sdl.window);
    SDL_Quit();
}

void clear_screen(const sdl_t sdl, const config_t config) {
    uint8_t r = (config.bgColor >> 24) & 0xFF;
    uint8_t g = (config.bgColor >> 16) & 0xFF;
    uint8_t b = (config.bgColor >> 8) & 0xFF;
    uint8_t a = (config.bgColor >> 0) & 0xFF;

    SDL_SetRenderDrawColor(sdl.renderer, r, g, b, a);
    SDL_RenderClear(sdl.renderer);
}

void update_screen(const sdl_t sdl) {
    SDL_RenderPresent(sdl.renderer);
}

void handle_input(chip8_t *chip8) {
    SDL_Event event;

    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_QUIT:
                chip8->state = QUIT;
                return;
            case SDL_KEYDOWN:
                break;
            case SDL_KEYUP:
                switch(event.key.keysym.sym) {
                    case SDLK_ESCAPE:
                        chip8->state = QUIT;
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

int main(int argc, char **argv) {
    sdl_t sdl = {0};
    config_t config = {0};
    chip8_t chip8 = {0};
    if (!init_chip8(&chip8))
        exit(EXIT_FAILURE);
    set_config(&config, argc, argv);
    if (!init_sdl(&sdl, config)) {
        exit(EXIT_FAILURE);
    }
    clear_screen(sdl, config);
    while (chip8.state != QUIT) {
        handle_input(&chip8);
        update_screen(sdl);
        SDL_Delay(16);
    }
    quit_sdl(sdl);
    exit(EXIT_SUCCESS);
}
