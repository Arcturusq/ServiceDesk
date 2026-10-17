#include "TelegramBot.h"

#include <cctype>
#include <climits>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

// ---------------------------------------------------------------------------
// Минимальный JSON-разборщик (ответы TDLib приходят как JSON-строки)
// ---------------------------------------------------------------------------
namespace jsonlite
{
	struct Value
	{
		enum class Type { Null, Bool, Number, String, Object, Array };

		Type type = Type::Null;
		bool boolean = false;
		double number = 0;
		std::string str;
		std::vector<std::pair<std::string, Value>> members; // объект
		std::vector<Value> items;                           // массив

		const Value* find(const std::string& key) const
		{
			for (const auto& m : members)
				if (m.first == key)
					return &m.second;
			return nullptr;
		}

		std::string asStr(const std::string& def = "") const
		{
			return type == Type::String ? str : def;
		}

		long long asLong(long long def = 0) const
		{
			return type == Type::Number ? (long long)number : def;
		}
	};

	class Parser
	{
	public:
		Parser(const std::string& text) : s_(text) {}

		bool parse(Value& out)
		{
			skipWs();
			if (!parseValue(out))
				return false;
			skipWs();
			return i_ == s_.size();
		}

	private:
		const std::string& s_;
		size_t i_ = 0;

		void skipWs()
		{
			while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\t' ||
				s_[i_] == '\r' || s_[i_] == '\n'))
				++i_;
		}

		bool parseValue(Value& out)
		{
			skipWs();
			if (i_ >= s_.size())
				return false;
			char c = s_[i_];
			if (c == '{') return parseObject(out);
			if (c == '[') return parseArray(out);
			if (c == '"') { out.type = Value::Type::String; return parseString(out.str); }
			if (s_.compare(i_, 4, "true") == 0) { i_ += 4; out.type = Value::Type::Bool; out.boolean = true; return true; }
			if (s_.compare(i_, 5, "false") == 0) { i_ += 5; out.type = Value::Type::Bool; out.boolean = false; return true; }
			if (s_.compare(i_, 4, "null") == 0) { i_ += 4; out.type = Value::Type::Null; return true; }
			return parseNumber(out);
		}

		bool parseObject(Value& out)
		{
			out.type = Value::Type::Object;
			++i_; // '{'
			skipWs();
			if (i_ < s_.size() && s_[i_] == '}') { ++i_; return true; }
			while (i_ < s_.size())
			{
				skipWs();
				std::string key;
				if (!parseString(key))
					return false;
				skipWs();
				if (i_ >= s_.size() || s_[i_] != ':')
					return false;
				++i_;
				Value value;
				if (!parseValue(value))
					return false;
				out.members.emplace_back(std::move(key), std::move(value));
				skipWs();
				if (i_ < s_.size() && s_[i_] == ',') { ++i_; continue; }
				if (i_ < s_.size() && s_[i_] == '}') { ++i_; return true; }
				return false;
			}
			return false;
		}

		bool parseArray(Value& out)
		{
			out.type = Value::Type::Array;
			++i_; // '['
			skipWs();
			if (i_ < s_.size() && s_[i_] == ']') { ++i_; return true; }
			while (i_ < s_.size())
			{
				Value value;
				if (!parseValue(value))
					return false;
				out.items.push_back(std::move(value));
				skipWs();
				if (i_ < s_.size() && s_[i_] == ',') { ++i_; continue; }
				if (i_ < s_.size() && s_[i_] == ']') { ++i_; return true; }
				return false;
			}
			return false;
		}

		bool parseString(std::string& out)
		{
			if (i_ >= s_.size() || s_[i_] != '"')
				return false;
			++i_;
			out.clear();
			while (i_ < s_.size())
			{
				char c = s_[i_++];
				if (c == '"')
					return true;
				if (c != '\\')
				{
					out += c;
					continue;
				}
				if (i_ >= s_.size())
					return false;
				char e = s_[i_++];
				switch (e)
				{
				case '"':  out += '"';  break;
				case '\\': out += '\\'; break;
				case '/':  out += '/';  break;
				case 'b':  out += '\b'; break;
				case 'f':  out += '\f'; break;
				case 'n':  out += '\n'; break;
				case 'r':  out += '\r'; break;
				case 't':  out += '\t'; break;
				case 'u':
				{
					unsigned cp = 0;
					if (!parseHex4(cp))
						return false;
					// суррогатная пара
					if (cp >= 0xD800 && cp <= 0xDBFF && i_ + 1 < s_.size() &&
						s_[i_] == '\\' && s_[i_ + 1] == 'u')
					{
						size_t save = i_;
						i_ += 2;
						unsigned low = 0;
						if (parseHex4(low) && low >= 0xDC00 && low <= 0xDFFF)
							cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
						else
							i_ = save;
					}
					appendUtf8(out, cp);
					break;
				}
				default: return false;
				}
			}
			return false;
		}

		bool parseHex4(unsigned& cp)
		{
			if (i_ + 4 > s_.size())
				return false;
			cp = 0;
			for (int n = 0; n < 4; ++n)
			{
				char c = s_[i_++];
				unsigned d;
				if (c >= '0' && c <= '9') d = (unsigned)(c - '0');
				else if (c >= 'a' && c <= 'f') d = (unsigned)(c - 'a' + 10);
				else if (c >= 'A' && c <= 'F') d = (unsigned)(c - 'A' + 10);
				else return false;
				cp = (cp << 4) | d;
			}
			return true;
		}

		static void appendUtf8(std::string& out, unsigned cp)
		{
			if (cp < 0x80)
				out += (char)cp;
			else if (cp < 0x800)
			{
				out += (char)(0xC0 | (cp >> 6));
				out += (char)(0x80 | (cp & 0x3F));
			}
			else if (cp < 0x10000)
			{
				out += (char)(0xE0 | (cp >> 12));
				out += (char)(0x80 | ((cp >> 6) & 0x3F));
				out += (char)(0x80 | (cp & 0x3F));
			}
			else
			{
				out += (char)(0xF0 | (cp >> 18));
				out += (char)(0x80 | ((cp >> 12) & 0x3F));
				out += (char)(0x80 | ((cp >> 6) & 0x3F));
				out += (char)(0x80 | (cp & 0x3F));
			}
		}

		bool parseNumber(Value& out)
		{
			size_t start = i_;
			while (i_ < s_.size() && (std::isdigit((unsigned char)s_[i_]) ||
				s_[i_] == '-' || s_[i_] == '+' || s_[i_] == '.' ||
				s_[i_] == 'e' || s_[i_] == 'E'))
				++i_;
			if (i_ == start)
				return false;
			out.type = Value::Type::Number;
			out.number = std::atof(s_.substr(start, i_ - start).c_str());
			return true;
		}
	};

	bool parse(const std::string& text, Value& out)
	{
		Parser parser(text);
		return parser.parse(out);
	}
}

// ---------------------------------------------------------------------------
// Локальные утилиты
// ---------------------------------------------------------------------------
namespace
{
	// Нижний регистр с поддержкой кириллицы (UTF-8)
	std::string toLowerUtf8(const std::string& s)
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
				if (cp >= 0x410 && cp <= 0x42F) cp += 0x20;
				else if (cp == 0x401) cp = 0x451;
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

	bool isWordByte(unsigned char b)
	{
		return std::isalnum(b) || b >= 0x80;
	}

	// Позиция целого слова (без учёта регистра); иначе npos
	size_t findWordCi(const std::string& text, const std::string& word)
	{
		std::string lowText = toLowerUtf8(text);
		std::string lowWord = toLowerUtf8(word);
		size_t pos = lowText.find(lowWord);
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

	std::string trim(const std::string& s)
	{
		size_t start = s.find_first_not_of(" \t\r\n");
		if (start == std::string::npos)
			return "";
		size_t end = s.find_last_not_of(" \t\r\n");
		return s.substr(start, end - start + 1);
	}

	std::string escapeJson(const std::string& s)
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

	// Разбирает ответ TDLib; возвращает true, если это ошибка
	bool isError(const std::string& raw, std::string* message = nullptr)
	{
		jsonlite::Value v;
		if (!jsonlite::parse(raw, v))
			return false;
		const jsonlite::Value* type = v.find("@type");
		if (!type || type->asStr() != "error")
			return false;
		if (message)
		{
			if (const jsonlite::Value* m = v.find("message"))
				*message = m->asStr();
		}
		return true;
	}

	// Текст сообщения: обычный текст или подпись к медиа
	std::string messageTextOf(const jsonlite::Value& message)
	{
		const jsonlite::Value* content = message.find("content");
		if (!content)
			return "";

		const jsonlite::Value* type = content->find("@type");
		std::string contentType = type ? type->asStr() : "";

		if (contentType == "messageText")
		{
			const jsonlite::Value* text = content->find("text");
			if (const jsonlite::Value* body = text ? text->find("text") : nullptr)
				return body->asStr();
		}
		else if (const jsonlite::Value* caption = content->find("caption"))
		{
			if (const jsonlite::Value* body = caption->find("text"))
				return body->asStr();
		}
		return "";
	}

	// Заявка — сообщение с меткой «ЗАЯВКА» и идентификатором.
	// ЗАЯВКА [123] ...  /  ЗАЯВКА 123 ...
	// (формат распознавания при необходимости правится здесь)
	bool isOrderMessage(const std::string& text, std::string* orderId)
	{
		if (orderId)
			orderId->clear();

		size_t pos = findWordCi(text, "заявка");
		if (pos == std::string::npos)
			return false;

		size_t i = pos + std::strlen("заявка");
		// между словом и идентификатором допускаем только разделители
		while (i < text.size() && (text[i] == ' ' || text[i] == '\t' ||
			text[i] == ':' || text[i] == '№' || text[i] == '#'))
			++i;

		if (i < text.size() && text[i] == '[')
		{
			size_t close = text.find(']', i + 1);
			if (close == std::string::npos)
				return false;
			if (orderId)
				*orderId = trim(text.substr(i + 1, close - i - 1));
			return true;
		}

		size_t digitsStart = i;
		while (i < text.size() && std::isdigit((unsigned char)text[i]))
			++i;
		if (i == digitsStart)
			return false;
		if (orderId)
			*orderId = text.substr(digitsStart, i - digitsStart);
		return true;
	}
}

// ---------------------------------------------------------------------------
// TelegramBot
// ---------------------------------------------------------------------------

TelegramBot::~TelegramBot()
{
	if (clientId_ >= 0 && tdDestroyClientId_)
		tdDestroyClientId_(clientId_);
#ifdef _WIN32
	if (module_)
		FreeLibrary((HMODULE)module_);
#else
	if (module_)
		dlclose(module_);
#endif
}

bool TelegramBot::loadLibrary(const std::string& dllPath)
{
#ifdef _WIN32
	HMODULE handle = LoadLibraryA(dllPath.c_str());
	if (!handle)
	{
		std::cerr << "Не удалось загрузить " << dllPath
			<< " (положите tdjson.dll рядом с exe)." << std::endl;
		return false;
	}
	tdCreateClientId_ = (TdCreateClientId)GetProcAddress(handle, "td_create_client_id");
	tdSend_ = (TdSend)GetProcAddress(handle, "td_send");
	tdReceive_ = (TdReceive)GetProcAddress(handle, "td_receive");
	tdExecute_ = (TdExecute)GetProcAddress(handle, "td_execute");
	tdDestroyClientId_ = (TdDestroyClientId)GetProcAddress(handle, "td_destroy_client_id");
	module_ = handle;
#else
	void* handle = dlopen(dllPath.c_str(), RTLD_NOW);
	if (!handle)
	{
		std::cerr << "Не удалось загрузить " << dllPath << ": " << dlerror() << std::endl;
		return false;
	}
	tdCreateClientId_ = (TdCreateClientId)dlsym(handle, "td_create_client_id");
	tdSend_ = (TdSend)dlsym(handle, "td_send");
	tdReceive_ = (TdReceive)dlsym(handle, "td_receive");
	tdExecute_ = (TdExecute)dlsym(handle, "td_execute");
	tdDestroyClientId_ = (TdDestroyClientId)dlsym(handle, "td_destroy_client_id");
	module_ = handle;
#endif

	if (!tdCreateClientId_ || !tdSend_ || !tdReceive_ || !tdDestroyClientId_)
	{
		std::cerr << "В " << dllPath << " нет нужных функций TDLib (td_create_client_id, "
			"td_send, td_receive, td_destroy_client_id)." << std::endl;
		return false;
	}
	return true;
}

bool TelegramBot::loadConfig(const std::string& path)
{
	std::ifstream file(path, std::ios::binary);
	if (!file.is_open())
	{
		std::cerr << "Не удалось открыть конфиг: " << path << std::endl;
		return false;
	}

	std::string line;
	while (std::getline(file, line))
	{
		std::string text = trim(line);
		if (text.empty() || text[0] == '#' || text[0] == ';')
			continue;

		size_t eq = text.find('=');
		if (eq == std::string::npos)
			continue;

		std::string key = toLowerUtf8(trim(text.substr(0, eq)));
		std::string value = trim(text.substr(eq + 1));

		if (key == "api_id")
			config_.apiId = std::atoi(value.c_str());
		else if (key == "api_hash")
			config_.apiHash = value;
		else if (key == "phone")
			config_.phone = value;
		else if (key == "chat")
			config_.chat = value;
		else if (key == "file" || key == "output")
			config_.outputFile = value;
	}

	if (config_.apiId == 0 || config_.apiHash.empty())
	{
		std::cerr << "В конфиге нужны api_id и api_hash (my.telegram.org)." << std::endl;
		return false;
	}
	if (config_.outputFile.empty())
		config_.outputFile = "../orders.txt";
	return true;
}

void TelegramBot::sendJson(const std::string& body)
{
	if (clientId_ < 0 || !tdSend_)
		return;
	if (const char* result = tdSend_(clientId_, body.c_str()))
		processMessage(result);
}

std::string TelegramBot::request(const std::string& body)
{
	std::string extra = "req" + std::to_string(++extraCounter_);
	std::string query = body;
	if (query.empty() || query.back() != '}')
		return "";
	query.pop_back(); // убираем '}'
	query += ",\"@extra\":\"" + extra + "\"}";

	if (const char* result = tdSend_(clientId_, query.c_str()))
		processMessage(result);

	while (replies_.find(extra) == replies_.end())
		pumpOnce();

	std::string response = replies_[extra];
	replies_.erase(extra);
	return response;
}

void TelegramBot::pumpOnce()
{
	if (!tdReceive_)
		return;
	if (const char* update = tdReceive_(1.0))
		processMessage(update);
}

void TelegramBot::processMessage(const std::string& raw)
{
	jsonlite::Value v;
	if (!jsonlite::parse(raw, v))
		return;

	const jsonlite::Value* type = v.find("@type");
	if (!type)
		return;
	const jsonlite::Value* extra = v.find("@extra");

	if (type->asStr() == "updateAuthorizationState")
	{
		const jsonlite::Value* state = v.find("authorization_state");
		const jsonlite::Value* stateType = state ? state->find("@type") : nullptr;
		if (stateType)
			handleAuthState(stateType->asStr());
	}

	if (extra)
		replies_[extra->asStr()] = raw;
	else if (type->asStr() == "error")
	{
		const jsonlite::Value* code = v.find("code");
		const jsonlite::Value* message = v.find("message");
		std::cerr << "Telegram, ошибка " << (code ? code->asLong() : 0)
			<< ": " << (message ? message->asStr() : "") << std::endl;
	}
}

void TelegramBot::handleAuthState(const std::string& state)
{
	if (state == "authorizationStateWaitTdlibParameters")
		return; // параметры отправляет authorize()

	if (state == "authorizationStateWaitPhoneNumber")
	{
		std::string phone = config_.phone;
		if (phone.empty())
		{
			std::cout << "Номер телефона (+7...): " << std::flush;
			std::getline(std::cin, phone);
		}
		sendJson("{\"@type\":\"setAuthenticationPhoneNumber\",\"phone_number\":\""
			+ escapeJson(phone) + "\"}");
	}
	else if (state == "authorizationStateWaitCode")
	{
		std::cout << "Код из Telegram: " << std::flush;
		std::string code;
		std::getline(std::cin, code);
		sendJson("{\"@type\":\"checkAuthenticationCode\",\"code\":\"" + escapeJson(code) + "\"}");
	}
	else if (state == "authorizationStateWaitPassword")
	{
		std::cout << "Пароль (двухфакторная аутентификация): " << std::flush;
		std::string password;
		std::getline(std::cin, password);
		sendJson("{\"@type\":\"checkAuthenticationPassword\",\"password\":\""
			+ escapeJson(password) + "\"}");
	}
	else if (state == "authorizationStateWaitOtherDeviceConfirmation")
	{
		std::cout << "Подтвердите вход в приложении Telegram на другом устройстве..." << std::endl;
	}
	else if (state == "authorizationStateReady")
	{
		authorized_ = true;
		std::cout << "Telegram: вход выполнен." << std::endl;
	}
	else if (state == "authorizationStateLoggingOut" || state == "authorizationStateClosing" ||
		state == "authorizationStateClosed")
	{
		std::cout << "Telegram: сессия закрывается." << std::endl;
	}
}

bool TelegramBot::authorize()
{
	if (clientId_ < 0)
	{
		if (!tdCreateClientId_)
		{
			std::cerr << "Сначала вызовите loadLibrary()." << std::endl;
			return false;
		}
		clientId_ = tdCreateClientId_();
	}

	std::string apiId = std::to_string(config_.apiId);
	std::string apiHash = escapeJson(config_.apiHash);

	// Полный набор параметров (для старых сборок TDLib)
	std::string full = "{\"@type\":\"setTdlibParameters\",\"use_test_dc\":false,"
		"\"database_directory\":\"tdlib_data\",\"use_message_database\":true,"
		"\"use_secret_chats\":false,\"api_id\":" + apiId + ",\"api_hash\":\"" + apiHash +
		"\",\"system_language_code\":\"ru\",\"system_version\":\"10.0\","
		"\"device_model\":\"PC\",\"application_version\":\"1.0\"}";

	// Минимальный набор (для новых сборок TDLib)
	std::string minimal = "{\"@type\":\"setTdlibParameters\","
		"\"database_directory\":\"tdlib_data\",\"api_id\":" + apiId +
		",\"api_hash\":\"" + apiHash + "\",\"system_language_code\":\"ru\","
		"\"device_model\":\"PC\",\"application_version\":\"1.0\"}";

	std::string response = request(full);
	std::string error;
	if (isError(response, &error))
	{
		response = request(minimal);
		if (isError(response, &error))
		{
			std::cerr << "Не удалось задать параметры TDLib: " << error << std::endl;
			return false;
		}
	}

	while (!authorized_)
		pumpOnce();

	return true;
}

long long TelegramBot::resolveChat(const std::string& chatRef)
{
	std::string ref = trim(chatRef);
	if (ref.empty())
	{
		std::cout << "Чат (@username, название или id): " << std::flush;
		std::getline(std::cin, ref);
		ref = trim(ref);
	}
	if (ref.empty())
		return 0;

	// Число (возможно отрицательное) — это уже id чата
	if (ref.find_first_not_of("-0123456789") == std::string::npos)
		return std::stoll(ref);

	std::string username = ref;
	if (!username.empty() && username[0] == '@')
		username = username.substr(1);

	std::string response = request("{\"@type\":\"searchPublicChat\",\"username\":\""
		+ escapeJson(username) + "\"}");
	jsonlite::Value chat;
	if (!isError(response) && jsonlite::parse(response, chat))
	{
		const jsonlite::Value* id = chat.find("id");
		const jsonlite::Value* title = chat.find("title");
		if (id && id->asLong() != 0)
		{
			if (title)
				std::cout << "Чат: " << title->asStr() << " (id " << id->asLong() << ")"
					<< std::endl;
			return id->asLong();
		}
	}

	// Запасной вариант — поиск по названию среди своих чатов
	response = request("{\"@type\":\"searchChat\",\"chat_list\":null,\"query\":\""
		+ escapeJson(ref) + "\",\"limit\":1}");
	jsonlite::Value found;
	if (!isError(response) && jsonlite::parse(response, found))
	{
		const jsonlite::Value* chats = found.find("chats");
		if (chats && !chats->items.empty())
		{
			const jsonlite::Value* id = chats->items[0].find("id");
			const jsonlite::Value* title = chats->items[0].find("title");
			if (id)
			{
				if (title)
					std::cout << "Чат: " << title->asStr() << " (id " << id->asLong() << ")"
						<< std::endl;
				return id->asLong();
			}
		}
	}

	std::cerr << "Чат не найден: " << ref << std::endl;
	return 0;
}

std::string TelegramBot::userName(long long userId)
{
	auto cached = userCache_.find(userId);
	if (cached != userCache_.end())
		return cached->second;

	std::string name;
	std::string response = request("{\"@type\":\"getUser\",\"user_id\":" + std::to_string(userId) + "}");
	jsonlite::Value user;
	if (!isError(response) && jsonlite::parse(response, user))
	{
		const jsonlite::Value* first = user.find("first_name");
		const jsonlite::Value* last = user.find("last_name");
		if (first && !first->asStr().empty())
			name = first->asStr();
		if (last && !last->asStr().empty())
			name += (name.empty() ? "" : " ") + last->asStr();
	}

	userCache_[userId] = name;
	return name;
}

bool TelegramBot::downloadOrders()
{
	if (!authorized_)
	{
		if (!authorize())
			return false;
	}

	chatId_ = resolveChat(config_.chat);
	if (chatId_ == 0)
		return false;

	// Файл-индекс: какие сообщения уже записаны (борьба с дублями при повторных прогонах)
	std::string indexPath = config_.outputFile + ".index";
	seen_.clear();
	{
		std::ifstream indexIn(indexPath, std::ios::binary);
		std::string line;
		while (std::getline(indexIn, line))
		{
			line = trim(line);
			if (!line.empty())
				seen_.insert(line);
		}
	}

	std::ofstream out(config_.outputFile, std::ios::app | std::ios::binary);
	if (!out.is_open())
	{
		std::cerr << "Не удалось открыть файл: " << config_.outputFile << std::endl;
		return false;
	}
	std::ofstream indexOut(indexPath, std::ios::app | std::ios::binary);

	std::cout << "Читаю историю чата..." << std::endl;

	long long total = 0, found = 0, added = 0;
	long long offset = 0;
	const int limit = 100;

	while (true)
	{
		std::string response = request("{\"@type\":\"getChatHistory\",\"chat_id\":"
			+ std::to_string(chatId_) + ",\"offset\":" + std::to_string(offset)
			+ ",\"limit\":" + std::to_string(limit) + ",\"only_local\":false}");

		jsonlite::Value history;
		if (!jsonlite::parse(response, history))
		{
			std::cerr << "Некорректный ответ getChatHistory." << std::endl;
			break;
		}

		const jsonlite::Value* messages = history.find("messages");
		if (!messages || messages->items.empty())
			break;

		long long oldest = LLONG_MAX;
		for (const jsonlite::Value& message : messages->items)
		{
			const jsonlite::Value* id = message.find("id");
			long long messageId = id ? id->asLong() : 0;
			if (messageId != 0 && messageId < oldest)
				oldest = messageId;

			++total;
			std::string text = trim(messageTextOf(message));
			if (text.empty())
				continue;

			std::string orderId;
			if (!isOrderMessage(text, &orderId))
				continue;

			++found;
			std::string key = std::to_string(chatId_) + ":" + std::to_string(messageId);
			if (!seen_.insert(key).second)
				continue; // уже записано раньше

			// Пишем текст сообщения как отдельный блок: между блоками — пустая строка
			out << text << "\n\n";
			indexOut << key << "\n";
			++added;

			std::string author;
			if (const jsonlite::Value* sender = message.find("sender_id"))
			{
				const jsonlite::Value* userId = sender->find("user_id");
				if (userId)
					author = userName(userId->asLong());
			}
			std::cout << "  заявка " << (orderId.empty() ? key : orderId)
				<< (author.empty() ? "" : " (" + author + ")") << std::endl;
		}

		out.flush();
		indexOut.flush();

		if (oldest == LLONG_MAX || (int)messages->items.size() < limit)
			break;
		if (oldest <= offset)
			break; // защита от зацикливания
		offset = oldest;
	}

	std::cout << "Сообщений прочитано: " << total
		<< ", заявок найдено: " << found
		<< ", добавлено новых: " << added << " -> " << config_.outputFile << std::endl;
	return true;
}
