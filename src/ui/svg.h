// SVG icons rasterised once into textures (nanosvg). Icons are authored white and tinted at draw time.
#pragma once
#include <SDL.h>
#include <string>

// Loads `path` through SDL_RWops, rasterises it into a w x h RGBA texture. nullptr on failure.
SDL_Texture* svg_texture_from_file(SDL_Renderer* r, const char* path, int w, int h);
