#include <SDL.h>
#include <stdlib.h>
#include <complex>
#include <cmath>
#include <array>
#include "conf.hpp"

#define ESCAPE_RADIUS_SQ 4

SDL_Event event;
bool mouseMoved = false;
void poll() {
    SDL_PollEvent(&event);
    switch(event.type) {
        case SDL_QUIT:
        case SDL_KEYDOWN:
            exit(0);
    }
}

SDL_PixelFormat* format;
void setPixel(SDL_Surface *screen, int x, int y, uint8_t* colors)
{
    int width = screen->pitch;
    uint32_t *pixels = (uint32_t*) screen->pixels + y * (width / 4) + x;

    if(colors)
        *pixels = SDL_MapRGB(format, colors[0], colors[1], colors[2] );
    else
        *pixels = SDL_MapRGB(format, 0, 0, 0);
}

SDL_Surface* surface;
void render(SDL_Surface* screen, uint8_t* colors) {
    register int x, y;
    register int box_x, box_y;
    register int i;
    for (i = 0; i < info.num_boxes + info.num_boxes_down + 1; i++)
    {
        for (box_x = i, box_y = 0; box_x >= 0; box_x--, box_y++) {
            register int initial_x = box_x * info.box_width;
            register int initial_y = box_y * info.box_height;
            for (y = initial_y; y - initial_y < info.box_height; y++) {
                if(y >= info.height) break;
                for (x = initial_x; x < info.width && x - initial_x < info.box_width; x++) {
                    setPixel(surface, x, y, colors ? colors + (sizeof(uint8_t) * (y*info.width*3 + x*3)) : NULL);
                }
            }
        }
        SDL_BlitSurface(surface, NULL, screen, NULL);
        SDL_Flip(screen);
        poll();
        SDL_Delay(info.delay);
    }
}

// method to (slowly) generate fractals other than the mandelbrot set
template<typename T, std::size_t N>
void mandelGeneralIterations(std::array<int, N>& iters, struct window<T>* window, T m_x, unsigned y, T power) {
    std::array<std::complex<T>, N> m;
    #pragma omp simd
    for(unsigned i=0;i<N;++i) {
        m[i] = std::complex<T>(m_x, info.aspect * (window->start_y + window->width_x * (y*N + i)/info.height));
    }
    std::array<unsigned, N> escaped{};
    std::array<std::complex<T>, N> zs;

    register int iter;
    for(iter=0;iter<window->max_iters;++iter) {
        register unsigned quit = 1;
        for(unsigned k = 0; k < N; ++k) { quit &= escaped[k]; }
        if(quit) break;

        #pragma omp simd
        for(unsigned k = 0; k < N; ++k) {
            if(!escaped[k])
            {
                if(std::abs(zs[k]) > 2) {
                    escaped[k] = 1;
                    iters[k] = iter;
                } else {
                    zs[k] = std::pow(zs[k], power) + m[k];
                }
            }
        }
    }
}

template<typename T, std::size_t N>
void mandelSquareIterations(std::array<int, N>& iters, const struct window<T>& window, T m_x, unsigned y) {
    std::array<T, N> mi; // imaginary parts of coordinates
    std::array<unsigned, N> escaped{};
    std::array<T, N> zr{};
    std::array<T, N> zrq{}; // zr squared to prevent recalculations
    std::array<T, N> zi{};
    std::array<T, N> ziq{}; // zi squared to prevent recalculations
    std::array<T, N> tzr; // temporary place to hold the real z values to do the swap

    // we need these arrays to take advantage of SIMD

    for(unsigned k=0;k<N;++k)
        mi[k]  = info.aspect * (window.start_y + window.width_x * (y*N + N - k)/info.height);

    register int iter;
    for(iter = 0; iter < window.max_iters; ++iter) {
        register unsigned quit = 1;
        #pragma omp simd
        for(unsigned k = 0; k < N; ++k) { quit &= escaped[k]; }
        if(quit) break;

        #pragma omp simd
        for(unsigned k = 0; k < N; ++k) {
            zrq[k] = zr[k] * zr[k];
            ziq[k] = zi[k] * zi[k];
            escaped[k] |= (zrq[k] + ziq[k]) > ESCAPE_RADIUS_SQ;
            iters[k] += escaped[k] ? 0 : 1;
            tzr[k] = zrq[k] - ziq[k] + m_x;
            zi[k] = 2.0 * zr[k] * zi[k] + mi[k];
            zr[k] = tzr[k];
        }
    }
}

void gaussianBlur(uint8_t* colors) {
    uint8_t* colors_new = (uint8_t*)malloc(sizeof(uint8_t) * 3 * info.width * info.height);
    #pragma omp parallel
    for(int x = 1; x < info.width-1; ++x) {
        for(int y = 1; y < info.height-1; ++y) {
            for(unsigned i = 0; i < 3; ++i) {
                unsigned place = (y * info.width + x)*3 + i;
                colors_new[place] =
                    1./16 * colors[(place - (info.width - 1)*3)] +
                    1./16 * colors[(place - (info.width + 1)*3)] +
                    1./16 * colors[(place + (info.width - 1)*3)] +
                    1./16 * colors[(place + (info.width + 1)*3)] +
                    1./8  * colors[(place - (info.width) * 3)] +
                    1./8  * colors[(place + (info.width) * 3)] +
                    1./8  * colors[(place - 3)] +
                    1./8  * colors[(place + 3)] +
                    1./4  * colors[place];
            }
        }
    }
    memcpy(colors, colors_new, 3 * info.width * info.height);
    free(colors_new);
}

template<typename T, std::size_t N>
void genColors(uint8_t* colors, const struct window<T>& window) {
    register int x;
    for(x = 0; x < info.width; x++) {
        float m_x = window.start_x + window.width_x * x / info.width;
        poll();
        #pragma omp parallel for
        for(unsigned y = 0; y < (info.height+N-1)/N; y++) { // the condition is: y < ceil(info.height/N); this is why we need the extra space in colors[];
            std::array<int, N> iters{};

            mandelSquareIterations<T, N>(iters, window, m_x, info.height/N - y);

            for(unsigned i = 0; i < N; ++i) {
                if(iters[i] == window.max_iters) {
                    memset(colors + sizeof(uint8_t) *( ( y * N + i ) * info.width * 3 + x * 3), 0x0, 3);
                } else {
                    float l = (float)iters[i]/conf::loop_every;
                    float p = l - std::floor(l);
                    int f = (int)std::floor(l) % conf::color_map_length;
                    int c = (int)std::ceil(l) % conf::color_map_length;
                    register int k;
                    for(k=0;k<3;++k)
                        colors[(y * N + i) * info.width * 3 + x * 3 + k] = (uint8_t)(conf::color_map[f][k] * (1-p) + conf::color_map[c][k] * p);
                }
            }
        }
    }
    // gaussianBlur(colors);
}

constexpr int resolution = 10;
void wait(int sec) {
    register int i;
    register int j;
    for(i = 0; i < sec; ++i) {
        for(j = 0; j < resolution; ++j) {
            poll();
            SDL_Delay(1000/resolution);
        }
    }
}

SDL_Surface* scr;
uint8_t* colors;

void exit() {
    free(colors);
    SDL_FreeSurface(scr);
    SDL_Quit();
}

int main()
{
    std::atexit(exit);

    SDL_Init(SDL_INIT_VIDEO);

    SDL_ShowCursor(0);

    genInfo();

    SDL_WM_SetCaption("FSS", NULL);

    scr = SDL_SetVideoMode(info.width, info.height, 32, SDL_FULLSCREEN | SDL_HWSURFACE | SDL_DOUBLEBUF);

    format = scr->format;
    surface = SDL_CreateRGBSurface(SDL_HWSURFACE, info.width, info.height, format->BitsPerPixel,
            format->Rmask, format->Gmask, format->Bmask, format->Amask);

    size_t colors_length = sizeof(uint8_t) * info.width * (info.height + conf::SIMD_SIZE) * 3; // We add the SIMD size to have a buffer to write unused data
    colors = (uint8_t*)malloc(colors_length); // freed in exit()

    for(;;) {
        for(uint8_t w=0;w<conf::float_windows.size();++w) {
            genColors<float, conf::SIMD_SIZE>(colors, conf::float_windows[w]);
            render(scr, colors);
            wait(info.wait);
        }
        for(uint8_t w=0;w<conf::double_windows.size();++w) {
            genColors<double, conf::SIMD_SIZE>(colors, conf::double_windows[w]);
            render(scr, colors);
            wait(info.wait);
        }
    }

    return 0;

}
