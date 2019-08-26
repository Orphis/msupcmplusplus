#include <iostream>
#include <locale>
#include <string>

#include "AudioTrackListBuilder.h"
#include "sox_main.h"
#include "utf8.h"

#define VERSION_NUMBER 0.6

using namespace msu;

void usage()
{
	std::wcout << L"msupcm v" << VERSION_NUMBER << std::endl << std::endl;
	std::wcout << L"Usage:" << std::endl;
	std::wcout << L"msupcm [tracks.json]" << std::endl;
	std::wcout << L"msupcm -s [sox args]" << std::endl;
	std::wcout << L"msupcm -v" << std::endl;
	std::wcout << L"msupcm ?" << std::endl;
}

int main(int argc, char * argv[])
{
	int wargc;
	char** wargv;
	int exit_code;

#ifdef _WIN32
	lsx_init_console();
	lsx_init_commandline_arguments(&wargc, &wargv);
#elif __LINUX__
    wargv = argv;
    wargc = argc;

	std::setlocale(LC_ALL, "");
#endif

	switch (argc)
	{
	case 1:
		AudioTrackListBuilder(L"tracks.json").get().render();
		exit_code = 0;
		break;

	case 2:
		if (std::wstring(utf8_to_wstring.from_bytes(wargv[1])).compare(L"-v") == 0)
		{
			std::wcout << L"msupcm v" << VERSION_NUMBER << std::endl;
			exit_code = 0;
			break;
		}

		else if (std::wstring(utf8_to_wstring.from_bytes(wargv[1])).compare(L"?") == 0)
		{
			usage();
			exit_code = 0;
			break;
		}

		else if (std::wstring(utf8_to_wstring.from_bytes(wargv[1])).find_last_of(L".") != std::wstring::npos  &&
			std::wstring(utf8_to_wstring.from_bytes(wargv[1])).substr( \
				std::wstring(utf8_to_wstring.from_bytes(wargv[1])).find_last_of(L".")).compare(L".json") == 0)
		{
			AudioTrackListBuilder(utf8_to_wstring.from_bytes(wargv[1])).get().render();
		}

		else
		{
			usage();
			exit_code = 1;
			break;
		}

		break;

	default:
		if (std::string(argv[1]).compare("-s") == 0)
		{
#ifdef _WIN32
			wargv[1] = utf16_to_utf8(L"sox");
#else
			int len = strlen(utf8_to_wstring.to_bytes(L"sox").c_str());
			wargv[1] = (char*) malloc(len + 1);
			strncpy(wargv[1], utf8_to_wstring.to_bytes(L"sox").c_str(), len);
#endif
			--wargc;

			exit_code = soxmain(wargc, wargv + 1);
			break;
		}

		usage();
		exit_code = 1;
		break;
	}
	
#ifdef _WIN32
	lsx_uninit_console();
	lsx_free_commandline_arguments(&wargc, &wargv);
#endif

	return exit_code;
}
