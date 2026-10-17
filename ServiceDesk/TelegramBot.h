#pragma once

#include <map>
#include <set>
#include <string>

// Клиент Telegram на базе TDLib (MTProto, вход от имени пользователя).
//
// Читает всю историю чата, находит сообщения-заявки («ЗАЯВКА [id]»)
// и дописывает их текст в один текстовый файл — для последующей
// обработки классом OrderConverter (текст -> JSON).
//
// TDLib подключается динамически (tdjson.dll рядом с exe),
// поэтому проект собирается без установленной TDLib.
class TelegramBot
{
public:
	struct Config
	{
		int apiId = 0;          // api_id с my.telegram.org
		std::string apiHash;    // api_hash с my.telegram.org
		std::string phone;      // номер телефона в формате +7...
		std::string chat;       // @username, название или числовой id чата
		std::string outputFile; // файл, куда дописываем найденные заявки
	};

	TelegramBot() = default;
	~TelegramBot();

	TelegramBot(const TelegramBot&) = delete;
	TelegramBot& operator=(const TelegramBot&) = delete;

	// Загружает tdjson.dll и её функции
	bool loadLibrary(const std::string& dllPath = "tdjson.dll");

	// Читает конфиг вида «ключ = значение» (api_id, api_hash, phone, chat, file)
	bool loadConfig(const std::string& path);

	void setConfig(const Config& config) { config_ = config; }
	const Config& config() const { return config_; }

	// Проходит авторизацию: код и пароль 2FA спрашивает в консоли
	bool authorize();

	// Скачивает историю чата и дописывает новые заявки в файл
	bool downloadOrders();

private:
	using TdCreateClientId = int (*)();
	using TdSend = const char* (*)(int, const char*);
	using TdReceive = const char* (*)(double);
	using TdExecute = const char* (*)(const char*);
	using TdDestroyClientId = void (*)(int);

	// Отправляет запрос, не ожидая ответа (ответы видны в processMessage)
	void sendJson(const std::string& body);

	// Отправляет запрос и ждёт ответ с тем же @extra, попутно
	// обрабатывая обновления (в том числе шаги авторизации)
	std::string request(const std::string& body);

	void processMessage(const std::string& raw);
	void handleAuthState(const std::string& state);
	void pumpOnce();

	// chatRef -> chat_id: @username ищется через searchPublicChat,
	// число воспринимается как id, остальное — как поиск по названию
	long long resolveChat(const std::string& chatRef);
	long long currentChatId() const { return chatId_; }
	std::string userName(long long userId);

	Config config_;
	void* module_ = nullptr;
	TdCreateClientId tdCreateClientId_ = nullptr;
	TdSend tdSend_ = nullptr;
	TdReceive tdReceive_ = nullptr;
	TdExecute tdExecute_ = nullptr;
	TdDestroyClientId tdDestroyClientId_ = nullptr;

	int clientId_ = -1;
	long long extraCounter_ = 0;
	long long chatId_ = 0;
	std::map<std::string, std::string> replies_;   // @extra -> ответ
	std::map<long long, std::string> userCache_;   // user_id -> имя
	std::set<std::string> seen_;                   // «chatId:messageId» уже записанные
	bool authorized_ = false;
};
