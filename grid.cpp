/**************************************************************

   grid.cpp - Simple test grid

   ---------------------------------------------------------

   Switchres   Modeline generation engine for emulation

   License     GPL-2.0+
   Copyright   2010-2020 Chris Kennedy, Antonio Giner,
                         Alexandre Wodarczyk, Gil Delescluse

 **************************************************************/

#define SDL_MAIN_HANDLED
#define NUM_GRIDS 2

#include <SDL2/SDL.h> 

typedef struct grid_display
{
	int index;
	int width;
	int height;

	SDL_Window *window;
	SDL_Renderer *renderer;
} GRID_DISPLAY;

//============================================================
//  draw_grid
//============================================================
	
void draw_grid(int num_grid, int width, int height, SDL_Renderer *renderer)
{
	// Clean the surface
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
	SDL_RenderClear(renderer);

	// Draw outer rectangle
	SDL_Rect rect {0, 0, width, height};
	SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
	SDL_RenderDrawRect(renderer, &rect);

	switch (num_grid)
	{
		case 0:
			// cps2 grid
			for (int i = 0;  i < width / 16; i++)
			{
				for (int j = 0; j < height / 16; j++)
				{
					if (i == 0 || j == 0 || i == (width / 16) - 1 || j == (height / 16) - 1)
						SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
					else
						SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);

					rect = {i * 16, j * 16, 16, 16};
					SDL_RenderDrawRect(renderer, &rect);

					rect = {i * 16 + 7, j * 16 + 7, 2, 2};
					SDL_RenderDrawRect(renderer, &rect);
				}
			}
			break;

		case 1:
			// 16 x 12 squares
			for (int i = 0;  i < width / 16; i++)
			{
				int x_pos = floor(i * float(width) / 16);
				SDL_RenderDrawLine(renderer, x_pos, 0, x_pos, height - 1);
			}

			for (int i = 0;  i < height / 12; i++)
			{
				int y_pos = floor(i * float(height) / 12);
				SDL_RenderDrawLine(renderer, 0, y_pos, width - 1, y_pos);
			}
			break;
	}

	SDL_RenderPresent(renderer);
	SDL_RenderPresent(renderer);
}

//============================================================
//  main
//============================================================

int main(int argc, char **argv)
{ 
	SDL_Window* win_array[10] = {};
	GRID_DISPLAY display_array[10] = {};
	int display_total = 0;

	// Initialize SDL
	if (SDL_Init(SDL_INIT_VIDEO) != 0)
	{ 
		printf("error initializing SDL: %s\n", SDL_GetError());
		return 1;
	}

	// Get target displays
	if (argc > 1)
	{
		// Parse command line for display indexes
		int display_index = 0;
		int num_displays = SDL_GetNumVideoDisplays();

		for (int arg = 1; arg < argc; arg++)
		{
			sscanf(argv[arg], "%d", &display_index);

			if (display_index < 0 || display_index > num_displays - 1)
			{
				printf("error, bad display_index: %d\n", display_index);
				return 1;
			}

			display_array[display_total].index = display_index;
			display_total++;
		}
	}
	else
	{
		// No display specified, use default
		display_array[0].index = 0;
		display_total = 1;
	}

	// Create windows
	for (int disp = 0; disp < display_total; disp++)
	{
		// Get target display size
		SDL_DisplayMode dm;
		SDL_GetCurrentDisplayMode(display_array[disp].index, &dm);

		display_array[disp].width = dm.w;
		display_array[disp].height = dm.h;

		// Create window
		display_array[disp].window = SDL_CreateWindow("Switchres test grid", SDL_WINDOWPOS_CENTERED_DISPLAY(display_array[disp].index), SDL_WINDOWPOS_CENTERED, dm.w, dm.h, 0);

		// Create renderer
		display_array[disp].renderer = SDL_CreateRenderer(display_array[disp].window, -1, SDL_RENDERER_ACCELERATED);

		// Draw grid
		draw_grid(0, display_array[disp].width, display_array[disp].height, display_array[disp].renderer);
	}

	// Wait for escape key
	bool close = false;
	int  num_grid = 0;

	while (!close)
	{ 
		SDL_Event event;

		while (SDL_PollEvent(&event))
		{ 
			switch (event.type)
			{ 
				case SDL_QUIT:
					close = true;
					break; 

				case SDL_KEYDOWN: 
					switch (event.key.keysym.scancode)
					{
						case SDL_SCANCODE_ESCAPE:
							close = true;
							break; 

						case SDL_SCANCODE_TAB:
							num_grid ++;
							for (int disp = 0; disp < display_total; disp++)
								draw_grid(num_grid % NUM_GRIDS, display_array[disp].width, display_array[disp].height, display_array[disp].renderer);
							break; 

						default:
							break;
					}
			}
		}
	}

	// Destroy all windows
	for (int disp = 0; disp < display_total; disp++)
		SDL_DestroyWindow(display_array[disp].window);

	SDL_Quit();

	return 0; 
} 
