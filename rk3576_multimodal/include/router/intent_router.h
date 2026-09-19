#pragma once
#include <string>

enum class RequestType { Text, Vision };
struct RouteDecision
{
    RequestType type = RequestType::Text;
    std::string reason;
};
class IntentRouter
{
public:
    RouteDecision route(const std::string& question) const;
};
const char* requestTypeName(RequestType type);
