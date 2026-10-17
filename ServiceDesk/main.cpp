#include "OrderConverter.h"

int main()
{
	OrderConverter::setupConsole();

	OrderConverter converter;
	converter.textToJson("../input.txt", "../output.json");
	return 0;
}
