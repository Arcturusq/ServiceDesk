#include "OrderConverter.h"
#include "TelegramBot.h"

#include <iostream>
#include <string>

namespace
{
	// Спрашивает значение в консоли, подсказывая значение по умолчанию
	std::string askValue(const std::string& prompt, const std::string& defaultValue)
	{
		std::cout << prompt << " [" << defaultValue << "]: " << std::flush;
		std::string value;
		std::getline(std::cin, value);
		return value.empty() ? defaultValue : value;
	}
}

int main()
{
	OrderConverter::setupConsole();

	std::cout << "1 - Скачать заявки из Telegram-чата и сохранить в текст\n"
		"2 - Превратить текст заявок в JSON\n"
		"Выбор [2]: " << std::flush;

	std::string choice;
	std::getline(std::cin, choice);

	if (choice == "1")
	{
		TelegramBot bot;
		if (!bot.loadLibrary())
			return 1;
		if (!bot.loadConfig(askValue("Файл конфигурации", "../telegram_config.txt")))
			return 1;

		TelegramBot::Config config = bot.config();
		config.chat = askValue("Чат (@username, название или id)", config.chat);
		config.outputFile = askValue("Файл для заявок", config.outputFile);
		bot.setConfig(config);

		return bot.downloadOrders() ? 0 : 1;
	}

	std::string input = askValue("Файл-источник", "../input.txt");
	std::string output = askValue("Файл JSON", "../output.json");

	OrderConverter converter;
	return converter.textToJson(input, output) ? 0 : 1;
}
