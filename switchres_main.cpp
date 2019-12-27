#include <stdio.h>
#include <iostream>
#include <fstream>
#include <string>
#include <algorithm>
#include "switchres.h"

using namespace std;

const string WHITESPACE = " \n\r\t\f\v";

// File parsing helpers

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

bool parse_config(switchres_manager &switchres)
{
	
	ifstream config_file("switchres.ini");

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
			//cout << key << " " << value << '\n';
			switch (s2i(key.c_str()))
			{
				case s2i("monitor"):
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

				default:
					cout << "Invalid option " << key << '\n';
					break;
			}
		}
	}
	config_file.close();
	return true;
}


int main(int argc, char **argv)
{

	switchres_manager switchres;

	parse_config(switchres);
	switchres.init();

}
