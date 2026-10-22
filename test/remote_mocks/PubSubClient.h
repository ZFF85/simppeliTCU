#pragma once
#include <string>
#include <vector>
class PubSubClient {
public:
    struct Message { std::string topic; std::string payload; bool retained; };
    std::vector<Message> messages;
    std::vector<std::string> subscriptions;
    bool connected() { return true; }
    bool subscribe(const char* topic) { subscriptions.emplace_back(topic); return true; }
    bool publish(const char* topic, const char* payload, bool retained) {
        messages.push_back({topic, payload, retained});
        return true;
    }
};
