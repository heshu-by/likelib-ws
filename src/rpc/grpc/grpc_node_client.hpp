#pragma once

#include <public_rpc.grpc.pb.h>

#include "rpc/base_rpc.hpp"

#include <grpcpp/grpcpp.h>

#include <string>

namespace rpc
{

/// Class implementing connect to node by gRPC and call methods
class GrpcNodeClient final : BaseRpc
{
  public:
    explicit GrpcNodeClient(const std::string& connect_address);

    ~GrpcNodeClient() override = default;

    OperationStatus test(uint32_t api_version) override;

    std::string balance(const lk::Address& address) override;

    Info info() override;

    lk::Block get_block(const base::Sha256& block_hash) override;

    std::tuple<OperationStatus, lk::Address, std::uint64_t> transaction_create_contract(
      lk::Balance amount,
      const lk::Address& from_address,
      const base::Time& transaction_time,
      std::uint64_t gas,
      const std::string& contract_code,
      const std::string& init,
      const lk::Sign& signature) override;

    std::tuple<OperationStatus, std::string, std::uint64_t> transaction_message_call(lk::Balance amount,
                                                                                   const lk::Address& from_address,
                                                                                   const lk::Address& to_address,
                                                                                   const base::Time& transaction_time,
                                                                                   std::uint64_t fee,
                                                                                   const std::string& data,
                                                                                   const lk::Sign& signature) override;

  private:
    std::unique_ptr<likelib::NodePublicInterface::Stub> _stub;
};

} // namespace rpc