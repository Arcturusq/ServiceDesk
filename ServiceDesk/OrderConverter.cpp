#include "OrderConverter.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <cctype>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#endif

// Все метки хвоста (важен порядок: сначала более длинные)
namespace
{
	const char* const KEYWORDS[] = {
		"итого чистыми", "комментарий", "чистыми", "расход", "итого",
		"ком", "когда", "соглас", "сд"
	};
}

void OrderConverter::setupConsole()
{
#ifdef _WIN32
	SetConsoleOutputCP(CP_UTF8);
	SetConsoleCP(CP_UTF8);
#endif
}

std::string OrderConverter::trim(const std::string& s)
{
	size_t start = s.find_first_not_of(" \t\r\n");
	if (start == std::string::npos)
		return "";
	size_t end = s.find_last_not_of(" \t\r\n");
	return s.substr(start, end - start + 1);
}

std::string OrderConverter::escapeJson(const std::string& s)
{
	std::string result;
	for (char c : s)
	{
		switch (c)
		{
		case '"':  result += "\\\""; break;
		case '\\': result += "\\\\"; break;
		case '\n': result += "\\n";  break;
		case '\r': result += "\\r";  break;
		case '\t': result += "\\t";  break;
		default:   result += c;      break;
		}
	}
	return result;
}

// Нижний регистр с поддержкой кириллицы (UTF-8)
std::string OrderConverter::toLowerUtf8(const std::string& s)
{
	std::string out;
	out.reserve(s.size());
	size_t i = 0;
	while (i < s.size())
	{
		unsigned char c = (unsigned char)s[i];
		if (c < 0x80)
		{
			out += (char)std::tolower(c);
			++i;
		}
		else if ((c & 0xE0) == 0xC0 && i + 1 < s.size())
		{
			unsigned cp = ((c & 0x1Fu) << 6) | ((unsigned char)s[i + 1] & 0x3F);
			if (cp >= 0x410 && cp <= 0x42F) cp += 0x20; // А..Я -> а..я
			else if (cp == 0x401) cp = 0x451;           // Ё -> ё
			out += (char)(0xC0 | (cp >> 6));
			out += (char)(0x80 | (cp & 0x3F));
			i += 2;
		}
		else
		{
			out += (char)c;
			++i;
		}
	}
	return out;
}

// Байт, принадлежащий «слову» (латиница/цифры или кириллица >= 0x80)
bool OrderConverter::isWordByte(unsigned char b)
{
	return std::isalnum(b) || b >= 0x80;
}

// Позиция целого слова (без учёта регистра); иначе npos
size_t OrderConverter::findWordCi(const std::string& text, const std::string& word, size_t from)
{
	std::string lowText = toLowerUtf8(text);
	std::string lowWord = toLowerUtf8(word);
	size_t pos = lowText.find(lowWord, from);
	while (pos != std::string::npos)
	{
		bool leftOK = (pos == 0) || !isWordByte((unsigned char)lowText[pos - 1]);
		size_t end = pos + lowWord.size();
		bool rightOK = (end >= lowText.size()) || !isWordByte((unsigned char)lowText[end]);
		if (leftOK && rightOK)
			return pos;
		pos = lowText.find(lowWord, end);
	}
	return std::string::npos;
}

std::vector<std::string> OrderConverter::splitLines(const std::string& text)
{
	std::vector<std::string> lines;
	std::stringstream ss(text);
	std::string line;
	while (std::getline(ss, line))
		lines.push_back(line);
	return lines;
}

std::vector<std::string> OrderConverter::splitByCommas(const std::string& text)
{
	std::vector<std::string> values;
	std::stringstream ss(text);
	std::string item;
	while (std::getline(ss, item, ','))
	{
		item = trim(item);
		if (!item.empty())
			values.push_back(item);
	}
	return values;
}

std::string OrderConverter::joinRange(const std::vector<std::string>& v, size_t from, size_t to)
{
	std::string result;
	for (size_t i = from; i <= to && i < v.size(); ++i)
	{
		if (!result.empty())
			result += ", ";
		result += v[i];
	}
	return result;
}

// Ищем в строке время вида H:MM / HH:MM
bool OrderConverter::hasTimePattern(const std::string& s)
{
	for (size_t i = 0; i + 3 < s.size(); ++i)
	{
		if (std::isdigit((unsigned char)s[i]) && s[i + 1] == ':' &&
			std::isdigit((unsigned char)s[i + 2]) && std::isdigit((unsigned char)s[i + 3]))
			return true;
	}
	return false;
}

// Если строка (уже в нижнем регистре) начинается с метки — возвращает её длину, иначе 0
size_t OrderConverter::matchKeyword(const std::string& low)
{
	for (const char* kw : KEYWORDS)
	{
		size_t n = strlen(kw);
		if (low.size() < n || low.compare(0, n, kw) != 0)
			continue;
		if (low.size() > n && isWordByte((unsigned char)low[n]))
			continue; // это начало другого слова (например «компьютер»)
		return n;
	}
	return 0;
}

bool OrderConverter::lineStartsWithKeyword(const std::string& line)
{
	return matchKeyword(toLowerUtf8(trim(line))) != 0;
}

// Позиция начала «хвоста»: первая строка, начинающаяся с метки
size_t OrderConverter::findTailStart(const std::string& block)
{
	size_t offset = 0;
	for (const std::string& raw : splitLines(block))
	{
		if (lineStartsWithKeyword(raw))
			return offset;
		offset += raw.size() + 1; // + '\n'
		if (offset > block.size())
			break;
	}
	return std::string::npos;
}

// Строка-начало блока: «Заказ ...» (текстовый журнал) или «ЗАЯВКА ...» (из Telegram)
bool OrderConverter::isBlockStart(const std::string& low)
{
	for (const char* word : { "заказ", "заявка" })
	{
		size_t n = strlen(word);
		if (low.compare(0, n, word) == 0 &&
			(low.size() == n || !isWordByte((unsigned char)low[n])))
			return true;
	}
	return false;
}

// Разбивает текст на блоки: новый блок начинается со строки «Заказ ...» / «ЗАЯВКА ...»
std::vector<std::string> OrderConverter::splitBlocks(const std::string& text)
{
	std::vector<std::string> blocks;
	std::string current;
	bool currentStarted = false;
	for (const std::string& rawLine : splitLines(text))
	{
		std::string line = trim(rawLine);
		std::string low = toLowerUtf8(line);
		bool isNew = isBlockStart(low);
		if (isNew && currentStarted)
		{
			blocks.push_back(current);
			current.clear();
		}
		if (isNew)
			currentStarted = true;
		current += line;
		current += "\n";
	}
	if (!trim(current).empty())
		blocks.push_back(current);
	return blocks;
}

// Убирает ведущий разделитель («:», пробелы) из значения метки
std::string OrderConverter::valueAfter(size_t len, const std::string& line)
{
	size_t i = len;
	while (i < line.size() && (line[i] == ':' || line[i] == ' ' || line[i] == '\t'))
		++i;
	return trim(line.substr(i));
}

// «7к», «5к», «11К», «11k», «5к предоплата» -> «7000», «5000», «11000», «5000 предоплата»
// Обычный текст и слова вроде «7копеек» не трогаем
std::string OrderConverter::expandThousands(const std::string& value)
{
	std::string out;
	out.reserve(value.size());
	size_t i = 0;
	while (i < value.size())
	{
		// начало числа — цифра, перед которой нет цифры
		if (std::isdigit((unsigned char)value[i]) &&
			(i == 0 || !std::isdigit((unsigned char)value[i - 1])))
		{
			size_t j = i;
			while (j < value.size() && std::isdigit((unsigned char)value[j]))
				++j;

			size_t k = j; // пропускаем пробелы между числом и «к»
			while (k < value.size() && (value[k] == ' ' || value[k] == '\t'))
				++k;

			size_t afterK = std::string::npos; // где заканчивается «к»
			if (k < value.size() && (value[k] == 'k' || value[k] == 'K'))
				afterK = k + 1;
			else if (value.compare(k, 2, "к") == 0 || value.compare(k, 2, "К") == 0)
				afterK = k + 2;

			// после «к» не должно быть букв/цифр (чтобы «7копеек» осталось как есть)
			bool isThousands = afterK != std::string::npos &&
				(afterK >= value.size() || !isWordByte((unsigned char)value[afterK]));

			if (isThousands)
			{
				long long number = std::stoll(value.substr(i, j - i));
				out += std::to_string(number * 1000);
				i = afterK;
				continue;
			}

			out += value.substr(i, j - i);
			i = j;
			continue;
		}
		out += value[i];
		++i;
	}
	return out;
}

// Денежные поля: «к» разворачиваем в тысячи
bool OrderConverter::isMoneyLabel(const std::string& label)
{
	return label == "итого" || label == "итого чистыми" || label == "чистыми" ||
		label == "расход" || label == "сд" || label == "соглас";
}

// Разбирает один блок заявки
Order OrderConverter::parseOrder(const std::string& block)
{
	Order o;

	// 1. Данные заявки — всё до первой строки-метки (или до слова «ИТОГО»)
	size_t tailStart = findTailStart(block);
	size_t wordPos = findWordCi(block, "итого");
	size_t splitPos = tailStart;
	if (splitPos == std::string::npos || (wordPos != std::string::npos && wordPos < splitPos))
		splitPos = wordPos;

	std::string header = (splitPos == std::string::npos) ? block : block.substr(0, splitPos);
	std::string tail = (splitPos == std::string::npos) ? "" : block.substr(splitPos);

	// 2. Данные заявки разбиваем по запятым
	std::vector<std::string> fields = splitByCommas(header);
	if (!fields.empty())
	{
		// Номер заказа: первая цифровая последовательность в первом поле
		size_t start = fields[0].find_first_of("0123456789");
		if (start != std::string::npos)
		{
			size_t end = fields[0].find_first_not_of("0123456789", start);
			o.number = fields[0].substr(start, end - start);
		}
	}
	if (fields.size() > 1)
		o.type = fields[1];

	// Ищем поле с датой (содержит время) — от него отсчитываем остальные поля
	size_t d = std::string::npos;
	for (size_t i = 2; i < fields.size(); ++i)
	{
		if (hasTimePattern(fields[i]))
		{
			d = i;
			break;
		}
	}
	if (d != std::string::npos && d >= 5)
	{
		// [Заказ], [тип], адрес... (город, улица, дом), [заявка...], [клиент], [возраст], [дата], [листовка?]
		// Граница адреса и заявки — поле с номером дома (начинается с цифры)
		size_t hi = std::string::npos;
		for (size_t i = 2; i + 1 <= d - 3; ++i)
		{
			if (!fields[i].empty() && std::isdigit((unsigned char)fields[i][0]))
			{
				hi = i;
				break;
			}
		}
		if (hi == std::string::npos)
			hi = d - 4; // запасной вариант: адрес ровно одно поле
		o.address = joinRange(fields, 2, hi);
		o.request = joinRange(fields, hi + 1, d - 3);
		o.client = fields[d - 2];
		o.age = fields[d - 1];
		o.date = fields[d];
		if (d + 1 < fields.size())
			o.flyer = fields[d + 1];
	}
	else if (fields.size() > 2)
	{
		// Не нашли дату — складываем всё подряд, чтобы ничего не потерять
		o.request = joinRange(fields, 2, fields.size() - 1);
	}

	// 3. Хвост: метки ИТОГО / РАСХОД / ЧИСТЫМИ / КОМ / СД / КОГДА / СОГЛАС
	bool inComment = false;
	bool hasSd = false, hasWhen = false, hasAgree = false;
	bool hasTotals = false;
	for (const std::string& rawLine : splitLines(tail))
	{
		std::string line = trim(rawLine);
		if (line.empty())
			continue;

		if (inComment)
		{
			if (!o.comment.empty())
				o.comment += "\n";
			o.comment += line;
			continue;
		}

		std::string low = toLowerUtf8(line);
		size_t kw = matchKeyword(low);
		if (kw == 0)
		{
			// Неизвестная строка — дописываем в комментарий, чтобы не потерять
			if (!o.comment.empty())
				o.comment += "\n";
			o.comment += line;
			continue;
		}

		std::string value = valueAfter(kw, line);
		std::string label = low.substr(0, kw);
		if (isMoneyLabel(label))
			value = expandThousands(value);

		if (label == "итого чистыми" || label == "чистыми") { o.net = value; hasTotals = true; }
		else if (label == "расход") { o.expense = value; hasTotals = true; }
		else if (label == "итого") { o.total = value; hasTotals = true; }
		else if (label == "комментарий" || label == "ком")
		{
			if (!value.empty())
			{
				if (!o.comment.empty())
					o.comment += "\n";
				o.comment += value;
			}
			inComment = true;
		}
		else if (label == "сд") { o.sd = value; hasSd = true; }
		else if (label == "когда") { o.when = value; hasWhen = true; }
		else if (label == "соглас") { o.agree = value; hasAgree = true; }
	}

	// 4. Тип отписки: СД (сд/когда/соглас) или закрытие (итого/расход/чистыми)
	if (hasSd || hasWhen || hasAgree)
		o.reportType = "СД";
	else if (hasTotals)
		o.reportType = "закрытие";

	return o;
}

std::string OrderConverter::ordersToJson(const std::vector<Order>& orders)
{
	std::ostringstream out;
	out << "[\n";
	for (size_t i = 0; i < orders.size(); ++i)
	{
		const Order& o = orders[i];
		out << "  {\n";
		out << "    \"заказ\": \"" << escapeJson(o.number) << "\",\n";
		out << "    \"тип\": \"" << escapeJson(o.type) << "\",\n";
		out << "    \"адрес\": \"" << escapeJson(o.address) << "\",\n";
		out << "    \"заявка\": \"" << escapeJson(o.request) << "\",\n";
		out << "    \"клиент\": \"" << escapeJson(o.client) << "\",\n";
		out << "    \"возраст\": \"" << escapeJson(o.age) << "\",\n";
		out << "    \"дата\": \"" << escapeJson(o.date) << "\",\n";
		out << "    \"листовка\": \"" << escapeJson(o.flyer) << "\",\n";
		out << "    \"отписка\": \"" << escapeJson(o.reportType) << "\",\n";
		out << "    \"итого\": \"" << escapeJson(o.total) << "\",\n";
		out << "    \"расход\": \"" << escapeJson(o.expense) << "\",\n";
		out << "    \"чистыми\": \"" << escapeJson(o.net) << "\",\n";
		out << "    \"сд\": \"" << escapeJson(o.sd) << "\",\n";
		out << "    \"когда\": \"" << escapeJson(o.when) << "\",\n";
		out << "    \"соглас\": \"" << escapeJson(o.agree) << "\",\n";
		out << "    \"комментарий\": \"" << escapeJson(o.comment) << "\"\n";
		out << "  }" << (i + 1 < orders.size() ? "," : "") << "\n";
	}
	out << "]\n";
	return out.str();
}

bool OrderConverter::textToJson(const std::string& inputPath, const std::string& outputPath)
{
	std::ifstream inFile(inputPath, std::ios::binary);
	if (!inFile.is_open())
	{
		std::cerr << "Не удалось открыть файл: " << inputPath << std::endl;
		return false;
	}
	std::stringstream buffer;
	buffer << inFile.rdbuf();
	inFile.close();

	std::string text = buffer.str();
	// Убираем UTF-8 BOM, если есть
	if (text.size() >= 3 && (unsigned char)text[0] == 0xEF &&
		(unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF)
		text.erase(0, 3);

	std::vector<std::string> blocks = splitBlocks(text);
	std::vector<Order> orders;
	for (const std::string& block : blocks)
		orders.push_back(parseOrder(block));

	std::ofstream outFile(outputPath, std::ios::binary);
	if (!outFile.is_open())
	{
		std::cerr << "Не удалось создать файл: " << outputPath << std::endl;
		return false;
	}
	outFile << ordersToJson(orders);
	outFile.close();

	std::cout << "Заявок найдено: " << orders.size() << " -> " << outputPath << std::endl;
	return true;
}
