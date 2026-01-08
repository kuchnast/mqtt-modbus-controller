#include <modbus/modbus.h>
#include <mqtt/async_client.h>
#include <iostream>
#include <map>
#include <vector>
#include <string>
#include <chrono>
#include <thread>
#include <csignal>
#include <cstring>
#include <syslog.h>
#include <atomic>

// ===== KONFIGURACJA =====
#define SERIAL_PORT "/dev/ttyUSB0"
#define BAUDRATE 9600
#define POLL_INTERVAL_MS 400  // 400ms polling dla przycisków
#define REFRESH_INTERVAL_SEC 10  // Co 10s aktualizuj stany przycisków
#define MQTT_ADDRESS "tcp://localhost:1883"
#define MQTT_CLIENT_ID "modbus_poller"
#define MQTT_QOS 1
#define MQTT_RETAINED true
#define MAX_RETRIES 3

// Struktura opisująca przycisk (INPUT)
struct Button {
    int slave;
    int address;
    std::string name;
    std::string mqtt_topic;
    bool last_state;
    std::chrono::steady_clock::time_point last_publish;
    
    Button(int s, int a, const std::string& n) 
        : slave(s), address(a), name(n), last_state(false),
          last_publish(std::chrono::steady_clock::now()) {
        mqtt_topic = "modbus/button/" + name + "/state";
    }
};

// Struktura opisująca przekaźnik (OUTPUT)
struct Relay {
    int slave;
    int address;
    std::string name;
    std::string mqtt_command_topic;
    std::string mqtt_state_topic;
    bool current_state;
    
    Relay(int s, int a, const std::string& n)
        : slave(s), address(a), name(n), current_state(false) {
        mqtt_command_topic = "modbus/relay/" + name + "/set";
        mqtt_state_topic = "modbus/relay/" + name + "/state";
    }
};

// ===== GLOBALNE ZMIENNE =====
modbus_t *ctx = nullptr;
mqtt::async_client *client = nullptr;
std::atomic<bool> running(true);
std::vector<Button> buttons;
std::vector<Relay> relays;

// Kolejka komend do przekaźników (thread-safe)
std::mutex relay_queue_mutex;
std::vector<std::pair<std::string, bool>> relay_command_queue;

// Statystyki błędów
std::atomic<int> read_errors(0);
std::atomic<int> write_errors(0);
std::atomic<int> read_success(0);
std::atomic<int> write_success(0);

// ===== SIGNAL HANDLER =====
void signal_handler(int signum) {
    std::cout << "\nOtrzymano sygnał " << signum << ", zatrzymywanie..." << std::endl;
    running = false;
}

// ===== INICJALIZACJA PRZYCISKÓW =====
void init_buttons() {
    // SLAVE 48 - 8 przycisków
    buttons.emplace_back(48, 0, "garaz_gora");
    buttons.emplace_back(48, 1, "garaz_dol");
    buttons.emplace_back(48, 2, "oranzeria_wschod_lewe_gora");
    buttons.emplace_back(48, 3, "oranzeria_wschod_lewe_dol");
    buttons.emplace_back(48, 4, "oranzeria_poludnie_prawe_gora");
    buttons.emplace_back(48, 5, "oranzeria_poludnie_prawe_dol");
    buttons.emplace_back(48, 6, "oranzeria_duze_gora");
    buttons.emplace_back(48, 7, "oranzeria_duze_dol");
    
    // SLAVE 49 - 8 przycisków
    buttons.emplace_back(49, 0, "klatka_schodowa_gora");
    buttons.emplace_back(49, 1, "klatka_schodowa_dol");
    buttons.emplace_back(49, 2, "oranzeria_poludnie_lewe_gora");
    buttons.emplace_back(49, 3, "oranzeria_poludnie_lewe_dol");
    buttons.emplace_back(49, 4, "salon_male_zachod_gora");
    buttons.emplace_back(49, 5, "salon_male_zachod_dol");
    buttons.emplace_back(49, 6, "salon_male_poludnie_gora");
    buttons.emplace_back(49, 7, "salon_male_poludnie_dol");
    
    // SLAVE 50 - 8 przycisków
    buttons.emplace_back(50, 0, "kuchnia_gora");
    buttons.emplace_back(50, 1, "kuchnia_dol");
    buttons.emplace_back(50, 2, "kotlownia_wschod_gora");
    buttons.emplace_back(50, 3, "kotlownia_wschod_dol");
    buttons.emplace_back(50, 4, "kotlownia_polnoc_gora");
    buttons.emplace_back(50, 5, "kotlownia_polnoc_dol");
    buttons.emplace_back(50, 6, "maly_pokoj_gora");
    buttons.emplace_back(50, 7, "maly_pokoj_dol");
    
    // SLAVE 51 - 8 przycisków
    buttons.emplace_back(51, 0, "oranzeria_polnoc_gora");
    buttons.emplace_back(51, 1, "oranzeria_polnoc_dol");
    buttons.emplace_back(51, 2, "oranzeria_wschod_prawe_gora");
    buttons.emplace_back(51, 3, "oranzeria_wschod_prawe_dol");
    buttons.emplace_back(51, 4, "salon_duze_gora");
    buttons.emplace_back(51, 5, "salon_duze_dol");
    buttons.emplace_back(51, 6, "lazienka_gora");
    buttons.emplace_back(51, 7, "lazienka_dol");
}

// ===== INICJALIZACJA PRZEKAŹNIKÓW =====
void init_relays() {
    // SLAVE 32 - 32 przekaźniki (zgodnie z configuration.yaml)
    relays.emplace_back(32, 0, "klatka_schodowa_gora");
    relays.emplace_back(32, 1, "klatka_schodowa_dol");
    relays.emplace_back(32, 2, "oranzeria_duze_gora");
    relays.emplace_back(32, 3, "oranzeria_duze_dol");
    relays.emplace_back(32, 4, "oranzeria_wschod_prawe_dol");
    relays.emplace_back(32, 5, "oranzeria_wschod_prawe_gora");
    relays.emplace_back(32, 6, "garaz_gora");
    relays.emplace_back(32, 7, "garaz_dol");
    relays.emplace_back(32, 8, "salon_male_zachod_gora");
    relays.emplace_back(32, 9, "salon_male_zachod_dol");
    relays.emplace_back(32, 10, "oranzeria_poludnie_prawe_dol");
    relays.emplace_back(32, 11, "oranzeria_poludnie_prawe_gora");
    relays.emplace_back(32, 12, "kotlownia_polnoc_gora");
    relays.emplace_back(32, 13, "kotlownia_polnoc_dol");
    relays.emplace_back(32, 14, "oranzeria_poludnie_lewe_gora");
    relays.emplace_back(32, 15, "oranzeria_poludnie_lewe_dol");
    relays.emplace_back(32, 16, "kotlownia_wschod_gora");
    relays.emplace_back(32, 17, "kotlownia_wschod_dol");
    relays.emplace_back(32, 18, "kuchnia_gora");
    relays.emplace_back(32, 19, "kuchnia_dol");
    relays.emplace_back(32, 20, "salon_duze_gora");
    relays.emplace_back(32, 21, "salon_duze_dol");
    relays.emplace_back(32, 22, "oranzeria_polnoc_gora");
    relays.emplace_back(32, 23, "oranzeria_polnoc_dol");
    relays.emplace_back(32, 24, "oranzeria_wschod_lewe_gora");
    relays.emplace_back(32, 25, "oranzeria_wschod_lewe_dol");
    relays.emplace_back(32, 26, "lazienka_gora");
    relays.emplace_back(32, 27, "lazienka_dol");
    relays.emplace_back(32, 28, "salon_male_poludnie_gora");
    relays.emplace_back(32, 29, "salon_male_poludnie_dol");
    relays.emplace_back(32, 30, "maly_pokoj_gora");
    relays.emplace_back(32, 31, "maly_pokoj_dol");
}

// ===== INICJALIZACJA MODBUS =====
bool init_modbus() {
    ctx = modbus_new_rtu(SERIAL_PORT, BAUDRATE, 'N', 8, 1);
    if (ctx == nullptr) {
        std::cerr << "Nie można utworzyć kontekstu Modbus RTU" << std::endl;
        return false;
    }
    
    if (modbus_set_slave(ctx, 1) == -1) {
        std::cerr << "Błąd ustawienia slave" << std::endl;
        modbus_free(ctx);
        return false;
    }
    
    if (modbus_connect(ctx) == -1) {
        std::cerr << "Błąd połączenia: " << modbus_strerror(errno) << std::endl;
        modbus_free(ctx);
        return false;
    }
    
    // Zwiększone timeouty dla stabilności
    struct timeval response_timeout;
    response_timeout.tv_sec = 1;
    response_timeout.tv_usec = 0;
    modbus_set_response_timeout(ctx, response_timeout.tv_sec, response_timeout.tv_usec);
    
    struct timeval byte_timeout;
    byte_timeout.tv_sec = 0;
    byte_timeout.tv_usec = 500000;  // 500ms
    modbus_set_byte_timeout(ctx, byte_timeout.tv_sec, byte_timeout.tv_usec);
    
    std::cout << "✓ Modbus RTU połączony na " << SERIAL_PORT 
              << " @ " << BAUDRATE << " baud" << std::endl;
    std::cout << "  Timeout: 1s response, 500ms byte" << std::endl;
    return true;
}

// ===== CALLBACK MQTT =====
class callback : public virtual mqtt::callback {
private:
    void message_arrived(mqtt::const_message_ptr msg) override {
        std::string topic = msg->get_topic();
        std::string payload = msg->to_string();
        
        // Sprawdź czy to komenda do przekaźnika
        if (topic.find("modbus/relay/") == 0 && topic.find("/set") != std::string::npos) {
            size_t start = strlen("modbus/relay/");
            size_t end = topic.find("/set");
            std::string relay_name = topic.substr(start, end - start);
            
            bool state = (payload == "ON");
            
            // Dodaj do kolejki komend
            {
                std::lock_guard<std::mutex> lock(relay_queue_mutex);
                relay_command_queue.push_back({relay_name, state});
            }
            
            std::cout << "MQTT CMD: " << relay_name << " = " << payload << std::endl;
        }
    }
    
public:
    void connection_lost(const std::string& cause) override {
        std::cerr << "Utracono połączenie MQTT: " << cause << std::endl;
        std::cerr << "Auto-reconnect powinien przywrócić połączenie..." << std::endl;
    }
    
    void connected(const std::string& cause) override {
        std::cout << "Połączono ponownie z MQTT" << std::endl;
    }
};

// ===== INICJALIZACJA MQTT =====
bool init_mqtt() {
    try {
        client = new mqtt::async_client(MQTT_ADDRESS, MQTT_CLIENT_ID);
        
        static callback cb;
        client->set_callback(cb);
        
        mqtt::connect_options connOpts;
        connOpts.set_clean_session(true);
        connOpts.set_automatic_reconnect(true);
        connOpts.set_keep_alive_interval(60);
        connOpts.set_user_name("test");
        connOpts.set_password("test");

        mqtt::message willmsg("modbus/poller/status", "offline", MQTT_QOS, MQTT_RETAINED);
        mqtt::will_options will(willmsg);
        connOpts.set_will(will);
        
        std::cout << "Łączenie z MQTT broker: " << MQTT_ADDRESS << "..." << std::endl;
        mqtt::token_ptr conntok = client->connect(connOpts);
        conntok->wait();
        
        // Subskrybuj wszystkie komendy do przekaźników
        client->subscribe("modbus/relay/+/set", MQTT_QOS)->wait();
        std::cout << "✓ Subskrybowano: modbus/relay/+/set" << std::endl;
        
        // Publikuj status online
        auto msg = mqtt::make_message("modbus/poller/status", "online");
        msg->set_qos(MQTT_QOS);
        msg->set_retained(MQTT_RETAINED);
        client->publish(msg)->wait();
        
        std::cout << "✓ MQTT połączony" << std::endl;
        return true;
        
    } catch (const mqtt::exception& exc) {
        std::cerr << "Błąd MQTT: " << exc.what() << std::endl;
        return false;
    }
}

// ===== ODCZYT DISCRETE INPUTS Z RETRY =====
bool read_discrete_inputs(int slave, int start_addr, int count, uint8_t *dest) {
    for (int retry = 0; retry < MAX_RETRIES; retry++) {
        modbus_set_slave(ctx, slave);
        
        int rc = modbus_read_input_bits(ctx, start_addr, count, dest);
        if (rc != -1) {
            read_success++;
            return true;
        }
        
        // Błąd - retry z krótkim opóźnieniem
        if (retry < MAX_RETRIES - 1) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
    
    // Wszystkie próby failed
    read_errors++;
    static auto last_error = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::seconds>(now - last_error).count() > 10) {
        std::cerr << "Błąd odczytu slave " << slave 
                  << " (po " << MAX_RETRIES << " próbach): " 
                  << modbus_strerror(errno) << std::endl;
        last_error = now;
    }
    return false;
}

// ===== ZAPIS POJEDYNCZEGO COIL Z RETRY =====
bool write_coil(int slave, int address, bool state) {
    for (int retry = 0; retry < MAX_RETRIES; retry++) {
        modbus_set_slave(ctx, slave);
        
        int rc = modbus_write_bit(ctx, address, state ? 1 : 0);
        if (rc != -1) {
            write_success++;
            return true;
        }
        
        // Błąd - retry z krótkim opóźnieniem
        if (retry < MAX_RETRIES - 1) {
            std::cerr << "Błąd zapisu coil slave " << slave 
                      << " addr " << address << ", próba " << (retry + 1) 
                      << "/" << MAX_RETRIES << ": " << modbus_strerror(errno) << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    
    // Wszystkie próby failed
    write_errors++;
    std::cerr << "KRYTYCZNY: Nie udało się zapisać coil slave " << slave 
              << " addr " << address << " po " << MAX_RETRIES 
              << " próbach!" << std::endl;
    return false;
}

// ===== PUBLIKACJA STANU PRZYCISKU =====
void publish_button_state(Button& btn, bool state, bool force = false) {
    try {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            now - btn.last_publish).count();
        
        // Publikuj jeśli zmiana stanu LUB force LUB minęło REFRESH_INTERVAL_SEC
        if (force || state != btn.last_state || elapsed >= REFRESH_INTERVAL_SEC) {
            const char* payload = state ? "ON" : "OFF";
            
            auto msg = mqtt::make_message(btn.mqtt_topic, payload);
            msg->set_qos(MQTT_QOS);
            msg->set_retained(MQTT_RETAINED);
            
            auto tok = client->publish(msg);
            tok->wait();
            
            if (state != btn.last_state) {
                std::cout << "BTN: " << btn.name << " = " << payload << std::endl;
            }
            
            btn.last_publish = now;
        }
        
    } catch (const mqtt::exception& exc) {
        std::cerr << "Błąd publikacji MQTT dla " << btn.name 
                  << ": " << exc.what() << std::endl;
    }
}

// ===== PUBLIKACJA STANU PRZEKAŹNIKA =====
void publish_relay_state(const Relay& relay, bool state) {
    try {
        const char* payload = state ? "ON" : "OFF";
        
        auto msg = mqtt::make_message(relay.mqtt_state_topic, payload);
        msg->set_qos(MQTT_QOS);
        msg->set_retained(MQTT_RETAINED);
        
        auto tok = client->publish(msg);
        tok->wait();
        
    } catch (const mqtt::exception& exc) {
        std::cerr << "Błąd publikacji stanu przekaźnika " << relay.name 
                  << ": " << exc.what() << std::endl;
    }
}

// ===== GŁÓWNA PĘTLA POLLINGU =====
void polling_loop() {
    uint8_t input_bits[8];
    
    // Grupuj przyciski według slave ID
    std::map<int, std::vector<Button*>> buttons_by_slave;
    for (auto& btn : buttons) {
        buttons_by_slave[btn.slave].push_back(&btn);
    }
    
    // Mapa przekaźników po nazwie
    std::map<std::string, Relay*> relays_by_name;
    for (auto& relay : relays) {
        relays_by_name[relay.name] = &relay;
    }
    
    std::cout << "Rozpoczynam polling przycisków co " << POLL_INTERVAL_MS << "ms..." << std::endl;
    std::cout << "Odświeżanie stanów przycisków co " << REFRESH_INTERVAL_SEC << "s..." << std::endl;
    std::cout << "Obsługa " << relays.size() << " przekaźników przez MQTT..." << std::endl;
    std::cout << "Retry: " << MAX_RETRIES << " próby dla każdej operacji" << std::endl;
    
    auto last_stats = std::chrono::steady_clock::now();
    
    while (running) {
        auto start_time = std::chrono::steady_clock::now();
        
        // ===== 1. ODCZYT PRZYCISKÓW =====
        for (auto& [slave, slave_buttons] : buttons_by_slave) {
            if (read_discrete_inputs(slave, 0, 8, input_bits)) {
                for (auto* btn : slave_buttons) {
                    bool current_state = input_bits[btn->address];
                    
                    // Publikuj zmianę lub okresowy refresh
                    publish_button_state(*btn, current_state);
                    btn->last_state = current_state;
                }
            }
        }
        
        // ===== 2. OBSŁUGA KOMEND DO PRZEKAŹNIKÓW =====
        {
            std::lock_guard<std::mutex> lock(relay_queue_mutex);
            
            for (auto& [relay_name, desired_state] : relay_command_queue) {
                auto it = relays_by_name.find(relay_name);
                if (it != relays_by_name.end()) {
                    Relay* relay = it->second;
                    
                    // Zapisz stan do Modbus z retry
                    if (write_coil(relay->slave, relay->address, desired_state)) {
                        relay->current_state = desired_state;
                        
                        // Publikuj potwierdzenie stanu
                        publish_relay_state(*relay, desired_state);
                        
                        std::cout << "RELAY: " << relay->name << " @ slave " << relay->slave 
                                  << " addr " << relay->address << " = " 
                                  << (desired_state ? "ON" : "OFF") << std::endl;
                    } else {
                        std::cerr << "BŁĄD: Nie udało się ustawić przekaźnika " 
                                  << relay->name << "!" << std::endl;
                    }
                }
            }
            
            relay_command_queue.clear();
        }
        
        // ===== 3. STATYSTYKI CO 60s =====
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - last_stats).count() >= 60) {
            int total_reads = read_success + read_errors;
            int total_writes = write_success + write_errors;
            
            std::cout << "\n===== STATYSTYKI (ostatnie 60s) =====" << std::endl;
            std::cout << "Odczyty: " << read_success << " OK, " << read_errors << " błędów";
            if (total_reads > 0) {
                std::cout << " (" << (100.0 * read_success / total_reads) << "% sukces)";
            }
            std::cout << std::endl;
            
            std::cout << "Zapisy: " << write_success << " OK, " << write_errors << " błędów";
            if (total_writes > 0) {
                std::cout << " (" << (100.0 * write_success / total_writes) << "% sukces)";
            }
            std::cout << std::endl << std::endl;
            
            // Reset
            read_success = 0;
            read_errors = 0;
            write_success = 0;
            write_errors = 0;
            last_stats = now;
        }
        
        // ===== 4. CZEKAJ DO NASTĘPNEGO CYKLU =====
        auto end_time = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time).count();
        
        if (elapsed < POLL_INTERVAL_MS) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(POLL_INTERVAL_MS - elapsed));
        }
    }
}

// ===== CLEANUP =====
void cleanup() {
    std::cout << "Sprzątanie..." << std::endl;
    
    if (client) {
        try {
            auto msg = mqtt::make_message("modbus/poller/status", "offline");
            msg->set_qos(MQTT_QOS);
            msg->set_retained(MQTT_RETAINED);
            client->publish(msg)->wait_for(std::chrono::seconds(2));
            
            client->disconnect()->wait();
            delete client;
        } catch (const mqtt::exception& exc) {
            std::cerr << "Błąd podczas rozłączania MQTT: " << exc.what() << std::endl;
        }
    }
    
    if (ctx) {
        modbus_close(ctx);
        modbus_free(ctx);
    }
    
    std::cout << "✓ Zakończono" << std::endl;
}

// ===== MAIN =====
int main(int argc, char* argv[]) {
    std::cout << "==================================" << std::endl;
    std::cout << "Modbus RTU to MQTT Gateway v2" << std::endl;
    std::cout << "Buttons (IN) + Relays (OUT)" << std::endl;
    std::cout << "==================================" << std::endl;
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    init_buttons();
    init_relays();
    std::cout << "✓ " << buttons.size() << " przycisków" << std::endl;
    std::cout << "✓ " << relays.size() << " przekaźników" << std::endl;
    
    if (!init_modbus()) {
        return 1;
    }
    
    if (!init_mqtt()) {
        cleanup();
        return 1;
    }
    
    polling_loop();
    
    cleanup();
    
    return 0;
}