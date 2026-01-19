#include "mqtt_manager.hpp"

MqttManagerStats::MqttManagerStats(int ps,int pe,int mr)
    : publish_success(ps), publish_errors(pe), messages_received(mr){
    }

MqttManager::MqttManager(const MqttConfig& config)
    : config_(config), 
      publish_success_(0), 
      publish_errors_(0), 
      messages_received_(0),
      logger_("MqttManager") {
    
    client_ = std::make_unique<mqtt::async_client>(
        config_.broker_address,
        config_.client_id
    );
    
    client_->set_callback(*this);
    
    logger_.debug() << "MqttManager created for broker: " << config_.broker_address;
}

MqttManager::~MqttManager() {
    logger_.debug() << "MqttManager destructor called";
    disconnect();
}

bool MqttManager::connect() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    try {
        mqtt::connect_options connOpts;
        connOpts.set_clean_session(true);
        connOpts.set_automatic_reconnect(true);
        connOpts.set_keep_alive_interval(config_.keep_alive_sec);
        
        if (!config_.username.empty()) {
            connOpts.set_user_name(config_.username);
            connOpts.set_password(config_.password);
            logger_.debug() << "Using authentication for user: " << config_.username;
        }
        
        mqtt::message willmsg("modbus/poller/status", "offline", config_.qos, config_.retained);
        mqtt::will_options will(willmsg);
        connOpts.set_will(will);
        
        logger_.info() << "Connecting to MQTT broker: " << config_.broker_address;
        
        auto conntok = client_->connect(connOpts);
        if (!conntok->wait_for(std::chrono::milliseconds(5000))) {
            logger_.error() << "MQTT connection timeout";
            return false;
        }
        
        logger_.info() << "MQTT connected successfully";
        
        // Publish online status
        auto msg = mqtt::make_message("modbus/poller/status", "online");
        msg->set_qos(config_.qos);
        msg->set_retained(config_.retained);
        client_->publish(msg);
        
        return true;
        
    } catch (const mqtt::exception& exc) {
        logger_.error() << "MQTT connection error: " << exc.what();
        return false;
    }
}

void MqttManager::disconnect() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (client_ && client_->is_connected()) {
        try {
            logger_.info() << "Disconnecting from MQTT broker";
            
            auto msg = mqtt::make_message("modbus/poller/status", "offline");
            msg->set_qos(config_.qos);
            msg->set_retained(config_.retained);
            
            auto tok = client_->publish(msg);
            tok->wait_for(std::chrono::milliseconds(1000));
            
            client_->disconnect()->wait_for(std::chrono::milliseconds(1000));
            
            logger_.info() << "MQTT disconnected";
        } catch (const mqtt::exception& exc) {
            logger_.error() << "MQTT disconnect error: " << exc.what();
        }
    }
}

bool MqttManager::is_connected() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return client_ && client_->is_connected();
}

bool MqttManager::subscribe(const std::string& topic) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    try {
        auto subtok = client_->subscribe(topic, config_.qos);
        if (subtok->wait_for(std::chrono::milliseconds(2000))) {
            logger_.info() << "Subscribed to: " << topic;
            return true;
        } else {
            logger_.error() << "Subscribe timeout for topic: " << topic;
            return false;
        }
    } catch (const mqtt::exception& exc) {
        logger_.error() << "Subscribe error: " << exc.what();
        return false;
    }
}

bool MqttManager::publish(const std::string& topic, const std::string& payload, bool retained) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    try {
        auto msg = mqtt::make_message(topic, payload);
        msg->set_qos(config_.qos);
        msg->set_retained(retained);
        
        auto tok = client_->publish(msg);
        
        if (tok->wait_for(std::chrono::milliseconds(config_.operation_timeout_ms))) {
            publish_success_++;
            logger_.debug() << "Published to " << topic << ": " << payload;
            return true;
        } else {
            publish_errors_++;
            logger_.warning() << "Publish timeout for topic: " << topic;
            return false;
        }
        
    } catch (const mqtt::exception& exc) {
        publish_errors_++;
        logger_.error() << "Publish error (" << topic << "): " << exc.what();
        return false;
    }
}

void MqttManager::set_message_callback(MqttMessageCallback callback) {
    message_callback_ = callback;
}

void MqttManager::message_arrived(mqtt::const_message_ptr msg) {
    messages_received_++;
    logger_.debug() << "Message received on " << msg->get_topic() 
                    << ": " << msg->to_string();
    
    if (message_callback_) {
        message_callback_(msg->get_topic(), msg->to_string());
    }
}

void MqttManager::connection_lost(const std::string& cause) {
    logger_.warning() << "MQTT connection lost: " << cause;
    logger_.info() << "Auto-reconnect should restore connection...";
}

void MqttManager::connected(const std::string&) {
    logger_.info() << "MQTT reconnected successfully";
}

std::unique_ptr<MqttManagerStats> MqttManager::get_stats() const {
    return std::make_unique<MqttManagerStats>(
        publish_success_.load(),
        publish_errors_.load(),
        messages_received_.load()
    );
}

void MqttManager::reset_stats() {
    publish_success_ = 0;
    publish_errors_ = 0;
    messages_received_ = 0;
}