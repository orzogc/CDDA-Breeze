#pragma once
#ifndef CATA_SRC_NETWORK_H
#define CATA_SRC_NETWORK_H

#include <string>
#include <functional>
#include <vector>

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

bool init();
void cleanup();

RequestId start_get( const std::string &url );
RequestId start_post( const std::string &url, const std::string &data );

void process();

RequestStatus get_status( RequestId id );
RequestResult get_result( RequestId id );
void cancel_request( RequestId id );
void clear_completed();

std::vector<RequestId> get_all_requests();

} // namespace network

#endif // CATA_SRC_NETWORK_H
