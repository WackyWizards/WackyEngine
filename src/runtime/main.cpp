#include <string>
#include <iostream>

#ifdef WACKY_EXPORT
#include <windows.h>
#endif

void RuntimeRun(const std::string& startWorldPath);

#ifdef WACKY_EXPORT

/**
 * Export build: WinMain entry point (no console window).
 */
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
	try
	{
		RuntimeRun("DefaultWorld.json");
	}
	catch (const std::exception& e)
	{
		/**
		 * Errors in GUI mode: show message box instead of console.
		 * (In practice, errors should be rare in release builds.)
		 */
		MessageBoxA(nullptr, e.what(), "[FATAL ERROR]", MB_OK | MB_ICONERROR);
		return 1;
	}
	catch (...)
	{
		MessageBoxA(nullptr, "Unknown error occurred", "[FATAL ERROR]", MB_OK | MB_ICONERROR);
		return 1;
	}
	return 0;
}

#else

/**
 * Editor build: standard main entry point (with console for debugging).
 */
int main()
{
	try
	{
		RuntimeRun("DefaultWorld.json");
	}
	catch (const std::exception& e)
	{
		std::cerr << "[FATAL] " << e.what() << "\n";
		std::cerr << "\nPress any key to exit...\n";
		std::cin.ignore();
		return 1;
	}
	catch (...)
	{
		std::cerr << "[FATAL] Unknown error occurred\n";
		std::cerr << "\nPress any key to exit...\n";
		std::cin.ignore();
		return 1;
	}
	return 0;
}

#endif