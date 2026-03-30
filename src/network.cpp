#include "network.h"
#include <curl/curl.h>
#include <map>
#include <vector>
#include <memory>

namespace
{

struct Request
{
    network::RequestId id;
    network::RequestStatus status;
    CURL *handle;
    std::string response_body;
    std::string error_message;
    long http_code;
    bool is_post;
    std::string post_data;
};

CURLM *g_multi_handle = nullptr;
network::RequestId g_next_id = 1;
std::map<network::RequestId, std::unique_ptr<Request>> g_requests;

size_t write_callback( void *contents, size_t size, size_t nmemb, std::string *s )
{
    size_t new_length = size * nmemb;
    try {
        s->append( static_cast<char *>( contents ), new_length );
    } catch( std::bad_alloc & ) {
        return 0;
    }
    return new_length;
}

Request *find_request( CURL *handle )
{
    for( auto &pair : g_requests ) {
        if( pair.second->handle == handle ) {
            return pair.second.get();
        }
    }
    return nullptr;
}

Request *find_request( network::RequestId id )
{
    auto it = g_requests.find( id );
    if( it != g_requests.end() ) {
        return it->second.get();
    }
    return nullptr;
}

} // anonymous namespace

namespace network
{

bool init()
{
    if( g_multi_handle ) {
        return true;
    }
    CURLcode res = curl_global_init( CURL_GLOBAL_DEFAULT );
    if( res != CURLE_OK ) {
        return false;
    }
    g_multi_handle = curl_multi_init();
    return g_multi_handle != nullptr;
}

void cleanup()
{
    for( auto &pair : g_requests ) {
        if( pair.second->handle ) {
            curl_multi_remove_handle( g_multi_handle, pair.second->handle );
            curl_easy_cleanup( pair.second->handle );
        }
    }
    g_requests.clear();

    if( g_multi_handle ) {
        curl_multi_cleanup( g_multi_handle );
        g_multi_handle = nullptr;
    }
    curl_global_cleanup();
}

RequestId start_get( const std::string &url )
{
    CURL *handle = curl_easy_init();
    if( !handle ) {
        return 0;
    }

    RequestId id = g_next_id++;
    auto req = std::make_unique<Request>();
    req->id = id;
    req->status = RequestStatus::Pending;
    req->handle = handle;
    req->http_code = 0;
    req->is_post = false;

    curl_easy_setopt( handle, CURLOPT_URL, url.c_str() );
    curl_easy_setopt( handle, CURLOPT_WRITEFUNCTION, write_callback );
    curl_easy_setopt( handle, CURLOPT_WRITEDATA, &req->response_body );
    curl_easy_setopt( handle, CURLOPT_FOLLOWLOCATION, 1L );
    curl_easy_setopt( handle, CURLOPT_SSL_VERIFYPEER, 0L );
    curl_easy_setopt( handle, CURLOPT_SSL_VERIFYHOST, 0L );
    curl_easy_setopt( handle, CURLOPT_PRIVATE, req.get() );

    curl_multi_add_handle( g_multi_handle, handle );
    req->status = RequestStatus::InProgress;

    g_requests[id] = std::move( req );
    return id;
}

RequestId start_post( const std::string &url, const std::string &data )
{
    CURL *handle = curl_easy_init();
    if( !handle ) {
        return 0;
    }

    RequestId id = g_next_id++;
    auto req = std::make_unique<Request>();
    req->id = id;
    req->status = RequestStatus::Pending;
    req->handle = handle;
    req->http_code = 0;
    req->is_post = true;
    req->post_data = data;

    curl_easy_setopt( handle, CURLOPT_URL, url.c_str() );
    curl_easy_setopt( handle, CURLOPT_POSTFIELDS, req->post_data.c_str() );
    curl_easy_setopt( handle, CURLOPT_POSTFIELDSIZE, req->post_data.size() );
    curl_easy_setopt( handle, CURLOPT_WRITEFUNCTION, write_callback );
    curl_easy_setopt( handle, CURLOPT_WRITEDATA, &req->response_body );
    curl_easy_setopt( handle, CURLOPT_FOLLOWLOCATION, 1L );
    curl_easy_setopt( handle, CURLOPT_SSL_VERIFYPEER, 0L );
    curl_easy_setopt( handle, CURLOPT_SSL_VERIFYHOST, 0L );
    curl_easy_setopt( handle, CURLOPT_PRIVATE, req.get() );

    curl_multi_add_handle( g_multi_handle, handle );
    req->status = RequestStatus::InProgress;

    g_requests[id] = std::move( req );
    return id;
}

void process()
{
    if( !g_multi_handle ) {
        return;
    }

    int running_handles = 0;
    CURLMcode mc;

    do {
        mc = curl_multi_perform( g_multi_handle, &running_handles );
    } while( mc == CURLM_CALL_MULTI_PERFORM );

    int msgs_left;
    CURLMsg *msg;
    while( ( msg = curl_multi_info_read( g_multi_handle, &msgs_left ) ) != nullptr ) {
        if( msg->msg == CURLMSG_DONE ) {
            CURL *handle = msg->easy_handle;
            Request *req = find_request( handle );
            if( req ) {
                if( msg->data.result == CURLE_OK ) {
                    curl_easy_getinfo( handle, CURLINFO_RESPONSE_CODE, &req->http_code );
                    req->status = RequestStatus::Completed;
                } else {
                    req->status = RequestStatus::Failed;
                    req->error_message = curl_easy_strerror( msg->data.result );
                }
                curl_multi_remove_handle( g_multi_handle, handle );
            }
        }
    }
}

RequestStatus get_status( RequestId id )
{
    Request *req = find_request( id );
    if( req ) {
        return req->status;
    }
    return RequestStatus::Failed;
}

RequestResult get_result( RequestId id )
{
    RequestResult result;
    Request *req = find_request( id );
    if( req ) {
        result.status = req->status;
        result.http_code = req->http_code;
        result.response_body = req->response_body;
        result.error_message = req->error_message;
    } else {
        result.status = RequestStatus::Failed;
        result.error_message = "Request not found";
    }
    return result;
}

void cancel_request( RequestId id )
{
    auto it = g_requests.find( id );
    if( it != g_requests.end() ) {
        Request *req = it->second.get();
        if( req->handle ) {
            curl_multi_remove_handle( g_multi_handle, req->handle );
            curl_easy_cleanup( req->handle );
            req->handle = nullptr;
        }
        req->status = RequestStatus::Failed;
    }
}

void clear_completed()
{
    for( auto it = g_requests.begin(); it != g_requests.end(); ) {
        Request *req = it->second.get();
        if( req->status == RequestStatus::Completed || req->status == RequestStatus::Failed ) {
            if( req->handle ) {
                curl_easy_cleanup( req->handle );
            }
            it = g_requests.erase( it );
        } else {
            ++it;
        }
    }
}

std::vector<RequestId> get_all_requests()
{
    std::vector<RequestId> ids;
    for( const auto &pair : g_requests ) {
        ids.push_back( pair.first );
    }
    return ids;
}

} // namespace network
