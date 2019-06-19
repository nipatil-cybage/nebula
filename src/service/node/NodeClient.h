/*
 * Copyright 2017-present Shawn Cao
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include <folly/init/Init.h>
#include <grpcpp/grpcpp.h>
#include <iostream>
#include <memory>
#include <string>
#include "node/node.grpc.fb.h"
#include "node/node_generated.h"
#include "service/nebula/NebulaService.h"

/**
 * Define node client which is responsible to talk to a node server for query fan out.
 */
namespace nebula {
namespace service {
class NodeClient {
public:
  NodeClient(const std::string& address, size_t port) {
    // create chanel via TCP connection - localhost? 127.0.0.1? 0.0.0.0?
    const auto addr = fmt::format("{0}:{1}", address, port);
    LOG(INFO) << "Node client connecting: " << addr;
    auto channel = grpc::CreateChannel(addr, grpc::InsecureChannelCredentials());

    this->stub_ = nebula::service::NodeServer::NewStub(channel);
  }

  // echo a name back from node server
  void echo(const std::string&);

  // stream multiple responses based on count
  void echos(const std::string&, size_t);

  virtual ~NodeClient() = default;

private:
  std::unique_ptr<nebula::service::NodeServer::Stub> stub_;
};
} // namespace service
} // namespace nebula