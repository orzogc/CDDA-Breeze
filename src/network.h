#pragma once
#ifndef CATA_SRC_NETWORK_H
#define CATA_SRC_NETWORK_H

#include <string>
#include <functional>
#include <vector>
#include <map>

namespace network
{

enum class RequestStatus
{
    Pending,
    InProgress,
    Completed,
    Failed
};

struct RequestResult
{
    RequestStatus status;
    int http_code;
    std::string response_body;
    std::string error_message;
};

using RequestId = size_t;
using Headers = std::map<std::string, std::string>;

bool init();
void cleanup();

RequestId start_get( const std::string &url );
RequestId start_get( const std::string &url, const Headers &headers );
RequestId start_post( const std::string &url, const std::string &data );
RequestId start_post( const std::string &url, const std::string &data, const Headers &headers );

void process();

RequestStatus get_status( RequestId id );
RequestResult get_result( RequestId id );
void cancel_request( RequestId id );
void clear_completed();

std::vector<RequestId> get_all_requests();

// 发送 pollinations API 请求
RequestId start_pollinations_request( const std::string &system_prompt,const std::string& user_prompt);

// 解析 pollinations API 响应，提取 content
std::string parse_pollinations_response( const std::string &json_response );

} // namespace network

#endif // CATA_SRC_NETWORK_H
