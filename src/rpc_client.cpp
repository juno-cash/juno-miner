#include "rpc_client.h"
#include "logger.h"
#include <curl/curl.h>
#include <iostream>
#include <sstream>

// Callback for CURL to write received data
static size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

RPCClient::RPCClient(const std::string& url, const std::string& user, const std::string& password)
    : url_(url), user_(user), password_(password), request_id_(0) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

RPCClient::~RPCClient() {
    curl_global_cleanup();
}

bool RPCClient::call(const std::string& method, const Json::Value& params, Json::Value& result) {
    last_error_.clear(); // Clear previous error
    LOG_DEBUG_STREAM("RPC call: " << method);

    CURL* curl = curl_easy_init();
    if (!curl) {
        last_error_ = "Failed to initialize CURL";
        LOG_ERROR("Failed to initialize CURL");
        return false;
    }

    // Build JSON-RPC request
    Json::Value request;
    request["jsonrpc"] = "1.0";
    request["id"] = ++request_id_;
    request["method"] = method;
    request["params"] = params;

    Json::StreamWriterBuilder writer;
    std::string request_str = Json::writeString(writer, request);
    LOG_DEBUG_STREAM("RPC request: " << request_str.substr(0, 200)
                    << (request_str.size() > 200 ? "..." : ""));

    // Set up CURL options
    std::string response_str;
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url_.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_str.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_str);
    curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
    curl_easy_setopt(curl, CURLOPT_USERNAME, user_.c_str());
    curl_easy_setopt(curl, CURLOPT_PASSWORD, password_.c_str());

    // Set timeouts to prevent hanging
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);  // 10 seconds to connect
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);         // 30 seconds total timeout

    // Perform request
    CURLcode res = curl_easy_perform(curl);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        last_error_ = std::string("RPC request failed: ") + curl_easy_strerror(res);
        LOG_ERROR_STREAM("RPC request failed: " << curl_easy_strerror(res));
        return false;
    }

    LOG_DEBUG_STREAM("RPC response received, size: " << response_str.size() << " bytes");

    // Parse response
    Json::CharReaderBuilder reader;
    std::string errors;
    std::istringstream response_stream(response_str);
    Json::Value response;

    if (!Json::parseFromStream(reader, response_stream, &response, &errors)) {
        last_error_ = "Failed to parse JSON response: " + errors;
        LOG_ERROR_STREAM("Failed to parse JSON response: " << errors);
        return false;
    }

    if (response.isMember("error") && !response["error"].isNull()) {
        // Extract error message from RPC error response
        Json::Value error = response["error"];
        if (error.isMember("message") && error["message"].isString()) {
            last_error_ = "RPC error: " + error["message"].asString();
            LOG_WARNING_STREAM("RPC error from " << method << ": " << error["message"].asString());
        } else {
            Json::StreamWriterBuilder writer;
            writer["indentation"] = "";
            last_error_ = "RPC error: " + Json::writeString(writer, error);
        }
        return false;
    }

    if (!response.isMember("result")) {
        last_error_ = "Invalid RPC response: no result field";
        return false;
    }

    result = response["result"];
    return true;
}

bool RPCClient::get_block_template(Json::Value& result, const std::string& mining_address) {
    Json::Value params(Json::arrayValue);

    // Request parameters
    Json::Value request_obj;
    request_obj["capabilities"] = Json::Value(Json::arrayValue);
    request_obj["capabilities"].append("coinbasetxn");
    request_obj["capabilities"].append("workid");
    request_obj["capabilities"].append("coinbase/append");

    // Note: mining_address parameter is ignored
    // The node automatically uses the wallet's keypool address for coinbase
    // or -mineraddress config option if no wallet

    params.append(request_obj);

    return call("getblocktemplate", params, result);
}

bool RPCClient::submit_block(const std::string& hex_data, std::string& result) {
    Json::Value params(Json::arrayValue);
    params.append(hex_data);

    Json::Value response;
    if (!call("submitblock", params, response)) {
        return false;
    }

    // submitblock returns null on success, or status string
    // Possible responses:
    //   null - block accepted and validated
    //   "duplicate" - node already has valid copy
    //   "duplicate-inconclusive" - node has block but not validated
    //   "inconclusive" - block accepted but validation deferred (not on best chain yet)
    //   "duplicate-invalid" - block is invalid
    //   "rejected" - block validation failed
    //   "inconclusive-not-best-prevblk" - previous block not on best chain

    if (response.isNull()) {
        result = "accepted";
        return true;
    } else if (response.isString()) {
        result = response.asString();

        // These are considered successes (block was stored)
        if (result == "duplicate" ||
            result == "inconclusive" ||
            result == "duplicate-inconclusive") {
            return true;
        }

        // Everything else is a rejection
        return false;
    }

    result = "unknown";
    return false;
}

bool RPCClient::create_new_account(int& account_id) {
    Json::Value params(Json::arrayValue);
    Json::Value result;

    if (!call("z_getnewaccount", params, result)) {
        return false;
    }

    if (!result.isMember("account")) {
        return false;
    }

    account_id = result["account"].asInt();
    return true;
}

bool RPCClient::get_address_for_account(int account_id, std::string& address) {
    Json::Value params(Json::arrayValue);
    params.append(account_id);

    // Request only transparent address (p2pkh) for mining
    Json::Value receiver_types(Json::arrayValue);
    receiver_types.append("p2pkh");
    params.append(receiver_types);

    Json::Value result;
    if (!call("z_getaddressforaccount", params, result)) {
        return false;
    }

    if (!result.isMember("address")) {
        return false;
    }

    address = result["address"].asString();
    return true;
}

bool RPCClient::get_blockchain_info(Json::Value& result) {
    Json::Value params(Json::arrayValue);
    return call("getblockchaininfo", params, result);
}

bool RPCClient::get_mining_info(Json::Value& result) {
    Json::Value params(Json::arrayValue);
    return call("getmininginfo", params, result);
}

bool RPCClient::get_wallet_balance(Json::Value& result) {
    // Build a combined result with both transparent and shielded balances
    result = Json::Value(Json::objectValue);

    // Call getwalletinfo to get balance, immature_balance, etc.
    Json::Value params(Json::arrayValue);
    Json::Value wallet_info;

    if (call("getwalletinfo", params, wallet_info)) {
        // balance field contains mature balance (1000+ confirmations for coinbase)
        if (wallet_info.isMember("balance")) {
            // Convert from JNO to zatoshis (multiply by 100000000)
            int64_t mature_zats = (int64_t)(wallet_info["balance"].asDouble() * 100000000.0);
            result["transparent_mature"] = mature_zats;
        } else {
            result["transparent_mature"] = 0;
        }

        // immature_balance field contains immature coinbase balance (<1000 confirmations)
        if (wallet_info.isMember("immature_balance")) {
            // Convert from JNO to zatoshis (multiply by 100000000)
            int64_t immature_zats = (int64_t)(wallet_info["immature_balance"].asDouble() * 100000000.0);
            result["transparent_immature"] = immature_zats;
        } else {
            result["transparent_immature"] = 0;
        }

        // Total = mature + immature
        int64_t total_zats = result["transparent_mature"].asInt64() + result["transparent_immature"].asInt64();
        result["transparent_total"] = total_zats;
    } else {
        result["transparent_mature"] = 0;
        result["transparent_immature"] = 0;
        result["transparent_total"] = 0;
    }

    return true;
}

bool RPCClient::get_block_hash(uint64_t height, std::string& block_hash) {
    Json::Value params(Json::arrayValue);
    params.append(static_cast<Json::Value::UInt64>(height));

    Json::Value result;
    if (!call("getblockhash", params, result)) {
        return false;
    }

    if (!result.isString()) {
        return false;
    }

    block_hash = result.asString();
    return true;
}
