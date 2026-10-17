#pragma once

#include <string>
#include <vector>

struct Order
{
	std::string number;     // номер заказа
	std::string type;       // Впервые / Гарантия / Повтор
	std::string address;    // адрес (может содержать запятые)
	std::string request;    // информация по заявке
	std::string client;     // имя клиента
	std::string age;        // возраст
	std::string date;       // дата
	std::string flyer;      // имя с листовки
	std::string reportType; // тип отписки: СД / закрытие
	std::string total;      // итого
	std::string expense;    // расход
	std::string net;        // чистыми
	std::string sd;         // СД
	std::string when;       // КОГДА
	std::string agree;      // СОГЛАС
	std::string comment;    // комментарий
};

// Разбирает текстовый журнал заявок и превращает его в JSON
class OrderConverter
{
public:
	// Переключает консоль на UTF-8
	static void setupConsole();

	// Читает inputPath, разбирает заявки и записывает JSON в outputPath
	bool textToJson(const std::string& inputPath, const std::string& outputPath);

private:
	// --- Утилиты строк ---
	static std::string trim(const std::string& s);
	static std::string escapeJson(const std::string& s);
	static std::string toLowerUtf8(const std::string& s); // нижний регистр с поддержкой кириллицы (UTF-8)
	static bool isWordByte(unsigned char b);              // байт, принадлежащий «слову»
	static size_t findWordCi(const std::string& text, const std::string& word, size_t from = 0);
	static std::vector<std::string> splitLines(const std::string& text);
	static std::vector<std::string> splitByCommas(const std::string& text);
	static std::string joinRange(const std::vector<std::string>& v, size_t from, size_t to);
	static bool hasTimePattern(const std::string& s);     // время вида H:MM / HH:MM

	// --- Разбор блоков ---
	static size_t matchKeyword(const std::string& low);   // длина метки в начале строки, иначе 0
	static bool lineStartsWithKeyword(const std::string& line);
	static size_t findTailStart(const std::string& block); // позиция начала «хвоста» с метками
	static std::vector<std::string> splitBlocks(const std::string& text); // текст -> блоки «Заказ ...»
	static std::string valueAfter(size_t len, const std::string& line);
	static std::string expandThousands(const std::string& value); // «7к» -> «7000»
	static bool isMoneyLabel(const std::string& label);
	static Order parseOrder(const std::string& block);

	// --- Вывод ---
	static std::string ordersToJson(const std::vector<Order>& orders);
};
