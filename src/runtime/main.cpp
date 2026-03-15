#include <string>

void RuntimeRun(const std::string& startWorldPath);

int main()
{
	RuntimeRun("DefaultWorld.json");
	return 0;
}