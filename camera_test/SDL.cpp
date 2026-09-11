#include <stdio.h>
#include <SDL2/SDL.h>

int main()
{
    if (SDL_Init(SDL_INIT_VIDEO) < 0)   
{
    printf("SDL error:,%s\n",SDL_GetError);
    return -1;
}
printf("SDL init successfully\n");
SDL_Window *window;
window = SDL_CreateWindow(
        "RK",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        1280,
        720,
        SDL_WINDOW_SHOWN
    );

if (window == NULL)
    {
        printf("SDL_CreateWindow failed: %s\n",
               SDL_GetError());

        SDL_Quit();

        return 1;
    }

SDL_Renderer *renderer;
renderer = SDL_CreateRenderer(
    window,
    -1,
    SDL_RENDERER_SOFTWARE
);
if (renderer == NULL)
{
    printf("SDL_CreateRenderer failed: %s\n",
           SDL_GetError());

    SDL_DestroyWindow(window);

    SDL_Quit();

    return 1;
}
SDL_SetRenderDrawColor(
    renderer,
    0,
    0,
    0,
    255
);

SDL_Texture *texture;
texture = SDL_CreateTexture(
    renderer,
    SDL_PIXELFORMAT_NV12,
    SDL_TEXTUREACCESS_STREAMING,
    1280,
    720
);

if (texture == NULL)
{
    printf("SDL_CreateTexture failed: %s\n",
           SDL_GetError());

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 1;
}



SDL_Delay(5000);


    SDL_DestroyWindow(window);

    SDL_Quit();

    return 0;
}










