#ifndef RPC_CLIENT_H
#define RPC_CLIENT_H

#include <string>
#include <vector>
#include <cstdint>
#include <json/json.h>

class RPCClient {
public:
    RPCClient(const std::string& url, const std::string& user, const std::string& password);
    ~RPCClient();

    // RPC methods
    bool get_block_template(Json::Value& result, const std::string& mining_address);
    bool submit_block(const std::string& hex_data, std::string& result);
    bool create_new_account(int& account_id);
    bool get_address_for_account(int account_id, std::string& address);
    bool get_blockchain_info(Json::Value& result);
    bool get_mining_info(Json::Value& result);
    bool get_wallet_balance(Json::Value& result);
    bool get_block_hash(uint64_t height, std::string& block_hash);

    // Get the last error message
    std::string get_last_error() const { return last_error_; }

private:
    std::string url_;
    std::string user_;
    std::string password_;
    int request_id_;
    std::string last_error_;

    bool call(const std::string& method, const Json::Value& params, Json::Value& result);
};

#endif // RPC_CLIENT_H
