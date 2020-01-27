#include <stdio.h>
#include <iostream>
#include <fstream>
#include <string>
#include <algorithm>
#include <getopt.h>
#include "switchres.h"

using namespace std;

const string WHITESPACE = " \n\r\t\f\v";
int show_version();
int show_usage();

//============================================================
//  File parsing helpers
//============================================================

string ltrim(const string& s)
{
	size_t start = s.find_first_not_of(WHITESPACE);
	return (start == string::npos) ? "" : s.substr(start);
}

string rtrim(const string& s)
{
	size_t end = s.find_last_not_of(WHITESPACE);
	return (end == string::npos) ? "" : s.substr(0, end + 1);
}

string trim(const string& s)
{
	return rtrim(ltrim(s));
}

bool get_value(const string& line, string& key, string& value)
{
	size_t key_end = line.find_first_of(WHITESPACE);
	
	key = line.substr(0, key_end);
	value = ltrim(line.substr(key_end + 1));
	
	if (key.length() > 0 && value.length() > 0)
		return true;

	return false;
}

constexpr unsigned int s2i(const char* str, int h = 0)
{
    return !str[h] ? 5381 : (s2i(str, h+1)*33) ^ str[h];
}


//============================================================
//  parse_config
//============================================================

bool parse_config(switchres_manager &switchres, const char *file_name)
{	
	ifstream config_file(file_name);

	if (!config_file.is_open())
		return false;
	
	string line;
	while (getline(config_file, line))
	{		
		line = trim(line);
		if (line.length() == 0 || line.at(0) == '#')
			continue;

		string key, value;
		if(get_value(line, key, value))
		{
			switch (s2i(key.c_str()))
			{
				// Switchres options
				case s2i("monitor"):
					transform(value.begin(), value.end(), value.begin(), ::tolower);
					sprintf(switchres.cs.monitor, value.c_str());
					break;
				case s2i("orientation"):
					sprintf(switchres.cs.orientation, value.c_str());
					break;
				case s2i("crt_range0"):
					sprintf(switchres.cs.crt_range0, value.c_str());
					break;
				case s2i("crt_range1"):
					sprintf(switchres.cs.crt_range1, value.c_str());
					break;
				case s2i("crt_range2"):
					sprintf(switchres.cs.crt_range2, value.c_str());
					break;
				case s2i("crt_range3"):
					sprintf(switchres.cs.crt_range3, value.c_str());
					break;
				case s2i("crt_range4"):
					sprintf(switchres.cs.crt_range4, value.c_str());
					break;
				case s2i("crt_range5"):
					sprintf(switchres.cs.crt_range5, value.c_str());
					break;
				case s2i("crt_range6"):
					sprintf(switchres.cs.crt_range6, value.c_str());
					break;
				case s2i("crt_range7"):
					sprintf(switchres.cs.crt_range7, value.c_str());
					break;
				case s2i("crt_range8"):
					sprintf(switchres.cs.crt_range8, value.c_str());
					break;
				case s2i("crt_range9"):
					sprintf(switchres.cs.crt_range9, value.c_str());
					break;
				case s2i("lcd_range"):
					sprintf(switchres.cs.lcd_range, value.c_str());
					break;

				// Display options
				case s2i("screen"):
					sprintf(switchres.ds.screen, value.c_str());
					break;
				case s2i("lock_unsupported_modes"):
					switchres.ds.lock_unsupported_modes = atoi(value.c_str());
					break;
				case s2i("lock_system_modes"):
					switchres.ds.lock_system_modes = atoi(value.c_str());
					break;
				case s2i("refresh_dont_care"):
					switchres.ds.refresh_dont_care = atoi(value.c_str());
					break;

				// Modeline generation options
				case s2i("modeline_generation"):
					switchres.gs.modeline_generation = atoi(value.c_str());
					break;
				case s2i("interlace"):
					switchres.gs.interlace = atoi(value.c_str());
					break;
				case s2i("doublescan"):
					switchres.gs.doublescan = atoi(value.c_str());
					break;
				case s2i("dotclock_min"):
					float pclock_min;
					sscanf(value.c_str(), "%f", &pclock_min);
					switchres.gs.pclock_min = pclock_min * 1000000;
					break;
				case s2i("sync_refresh_tolerance"):
					sscanf(value.c_str(), "%f", &switchres.gs.refresh_tolerance);
					break;
				case s2i("super_width"):
					sscanf(value.c_str(), "%d", &switchres.gs.super_width);
					break;

				default:
					cout << "Invalid option " << key << '\n';
					break;
			}
		}
	}
	config_file.close();
	return true;
}


//============================================================
//  main
//============================================================

int main(int argc, char **argv)
{

	switchres_manager switchres;

	parse_config(switchres, "switchres.ini");

	int verbose_flag = false;
	bool version_flag = false;
	bool help_flag = false;
	bool resolution_flag = false;
	bool calculate_flag = false;
	int c;

	while (1)
	{
		static struct option long_options[] =
		{
			{"verbose",     no_argument,       &verbose_flag, '1'},
			{"version",     no_argument,       0, 'v'},
			{"help",        no_argument,       0, 'h'},
			{"calc",        no_argument,       0, 'c'},
			{"monitor",     required_argument, 0, 'm'},
			{"orientation", required_argument, 0, 'o'},
			{"resolution",  required_argument, 0, 'r'},
			{"screen",      required_argument, 0, 's'},
			{0, 0, 0, 0}
		};

		int option_index = 0;
		c = getopt_long(argc, argv, "vhcm:o:r:s:", long_options, &option_index);

		if (c == -1)
			break;

		switch (c)
		{
			case 'v':
				version_flag = true;
				break;

			case 'h':
				help_flag = true;
				break;

			case 'c':
				calculate_flag = true;
				break;

			case 'm':
				sprintf(switchres.cs.monitor, optarg);
				break;

			case 'o':
				sprintf(switchres.cs.orientation, optarg);
				break;

			case 'r':
				if ((sscanf(optarg, "%dx%d@%d", &switchres.gs.width, &switchres.gs.height, &switchres.gs.refresh) < 3))
				{
					printf ("SwitchRes: illegal -resolution value: %s\n", optarg);
					break;
				}
				resolution_flag = true;
				break;

			case 's':
				sprintf(switchres.ds.screen, optarg);
				break;

			default:
				break;
		}
	}

	if (version_flag)
	{
		show_version();
		return 0;
	}

	if (help_flag)
		goto usage;

	// Get user video mode information from command line
	if ((argc - optind) < 3)
	{
		printf ("Error: missing argument\n");
		goto usage;
	}
	else if ((argc - optind) > 3)
	{
		printf ("Error: too many arguments\n");
		goto usage;
	}
	else
	{
		switchres.game.width = atoi(argv[optind]);
		switchres.game.height = atoi(argv[optind + 1]);
		switchres.game.refresh = atof(argv[optind + 2]);
		sprintf(switchres.game.name, "user");
		resolution_flag = true;
	}

	switchres.init();
	
	if (!calculate_flag)
		switchres.display()->init(&switchres.ds);

	if (resolution_flag)
	{
		modeline *mode = switchres.get_video_mode();
		if (mode)
		{
			if (mode->type & MODE_UPDATED)
			{
				switchres.display()->update_mode(mode);
			}
			else if (mode->type & MODE_NEW)
			{
				switchres.display()->add_mode(mode);
			}
		}
	}

	return (0);

usage:
	show_usage();
	return 0;
}

//============================================================
//  show_version
//============================================================

int show_version()
{
	char version[]
	{
		"Switchres " SWITCHRES_VERSION "\n"
		"Modeline generation engine for emulation\n"
		"Copyright (C) 2010-2019 - Chris Kennedy, Antonio Giner\n"
		"License GPL-2.0+\n"
		"This is free software: you are free to change and redistribute it.\n"
		"There is NO WARRANTY, to the extent permitted by law.\n"
	};
	
	printf("%s", version);
	return 0;
}

//============================================================
//  show_usage
//============================================================

int show_usage()
{
	char usage[] =
	{
		"Usage: switchres <width> <height> <refresh> [options]\n"
		"Options:\n"
		"  -c, --calc                        Calculate modeline only\n"
		"  -m, --monitor <preset>            Monitor preset (generic_15, arcade_15, pal, ntsc, etc.)\n"
		"  -o, --orientation <orientation>   Monitor orientation (horizontal, vertical, rotate_r, rotate_l)\n"
		"  -r, --resolution <width>x<height>@<refresh>\n"
		"                                    Force a specific resolution\n"
		"  -s, --screen <OS_display_name>    Configure target screen\n"
	};

	printf("%s", usage);
	return 0;
}
