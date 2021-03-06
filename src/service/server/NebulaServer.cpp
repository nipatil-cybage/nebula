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

#include <cstdlib>
#include <fmt/format.h>
#include <gflags/gflags.h>
#include <glog/logging.h>
#include <grpcpp/grpcpp.h>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>

#include "NebulaServer.h"
#include "NodeSync.h"
#include "common/Chars.h"
#include "common/Evidence.h"
#include "common/Folly.h"
#include "common/TaskScheduler.h"
#include "execution/BlockManager.h"
#include "execution/meta/TableService.h"
#include "ingest/SpecRepo.h"
#include "memory/Batch.h"
#include "meta/ClusterInfo.h"
#include "meta/NBlock.h"
#include "meta/NNode.h"
#include "meta/Table.h"
#include "nebula.grpc.pb.h"
#include "service/base/NebulaService.h"
#include "service/node/RemoteNodeConnector.h"
#include "storage/CsvReader.h"
#include "storage/NFS.h"
#include "storage/NFileSystem.h"

// use "host.docker.internal" for docker env
DEFINE_string(CLS_CONF, "configs/cluster.yml", "cluster config file");
DEFINE_uint64(CLS_CONF_UPDATE_INTERVAL, 5000, "interval in milliseconds to update cluster config");
DEFINE_uint64(NODE_SYNC_INTERVAL, 5000, "interval in ms to conduct node sync");
DEFINE_uint32(MAX_TABLES_RETURN, 500, "max tables to fetch to display");

/**
 * A cursor template that help iterating a container.
 * (should we use std::iterator instead?)
 */
namespace nebula {
namespace service {
namespace server {

using grpc::ServerContext;
using grpc::Status;
using nebula::api::dsl::QueryContext;
using nebula::common::Evidence;
using nebula::common::SingleCommandTask;
using nebula::common::Task;
using nebula::common::TaskType;
using nebula::execution::BlockManager;
using nebula::execution::io::BlockLoader;
using nebula::execution::meta::TableService;
using nebula::memory::Batch;
using nebula::meta::Table;
using nebula::service::base::ErrorCode;
using nebula::service::base::ServiceProperties;
using nebula::service::node::RemoteNodeConnector;
using nebula::storage::CsvReader;
using nebula::storage::NFileSystem;
using nebula::surface::RowCursorPtr;
using nebula::surface::RowData;
using nebula::type::Kind;
using nebula::type::Schema;
using nebula::type::TypeNode;
using nebula::type::TypeSerializer;

grpc::Status V1ServiceImpl::Tables(grpc::ServerContext*, const ListTables* request, TableList* reply) {
  auto bm = BlockManager::init();
  auto limit = request->limit();
  if (limit < 1) {
    limit = FLAGS_MAX_TABLES_RETURN;
  }

  for (const auto& table : bm->getTables(limit)) {
    reply->add_table(table);
  }

  LOG(INFO) << "Served table list request.";
  return Status::OK;
}

grpc::Status V1ServiceImpl::State(grpc::ServerContext*, const TableStateRequest* request, TableStateResponse* reply) {

  const auto table = TableService::singleton()->query(request->table());
  auto bm = BlockManager::init();
  // query the table's state
  auto metrics = bm->getTableMetrics(table->name());
  reply->set_blockcount(std::get<0>(metrics));
  reply->set_rowcount(std::get<1>(metrics));
  reply->set_memsize(std::get<2>(metrics));
  reply->set_mintime(std::get<3>(metrics));
  reply->set_maxtime(std::get<4>(metrics));

  // TODO(cao) - need meta data system to query table info

  auto schema = table->schema();
  for (size_t i = 0, size = schema->size(); i < size; ++i) {
    auto column = schema->childType(i);
    if (!column->isScalar(column->k())) {
      if (!column->isCompound(column->k())) {
        reply->add_dimension(column->name());
      }
    } else {
      reply->add_metric(column->name());
    }
  }

  LOG(INFO) << "Served table stats request for " << request->table();
  return Status::OK;
}

grpc::Status V1ServiceImpl::Query(grpc::ServerContext* ctx, const QueryRequest* request, QueryResponse* reply) {
  // validate the query request and build the call
  nebula::common::Evidence::Duration tick;
  ErrorCode error = ErrorCode::NONE;

  auto tableName = request->table();
  constexpr auto NUCLEAR = "_nuclear_";
  if (NUCLEAR == tableName) {
    LOG(INFO) << "Received a nuclear command, tearing down everything";
    // DEBUG/PROFILE PURPOSE:
    // shutdown the local node by this command
    RemoteNodeConnector connector{ nullptr };
    auto nodes = nebula::meta::ClusterInfo::singleton().nodes();
    N_ENSURE(nodes.size() > 0, "cluster info has no nodes??");
    auto client = connector.makeClient(*nodes.begin(), threadPool_);
    Task task(TaskType::COMMAND, SingleCommandTask::shutdown());
    client->task(task);
    return Status::OK;
  }

  // get the table
  auto table = TableService::singleton()->query(tableName);

  // build the query
  auto query = handler_.build(*table, *request, error);
  if (error != ErrorCode::NONE) {
    return replyError(error, reply, 0);
  }

  // build query context
  const auto& metadata = ctx->client_metadata();

  // fetch user info keys, refer userInfo definition in node.js serving http traffic
  // auth, authorization, user, groups
  std::string user{ "unauth" };
  std::unordered_set<std::string> groups;
  auto itr = metadata.find("nebula-auth");
  if (itr != metadata.end() && itr->second == "1") {
    auto u = metadata.find("nebula-user");
    if (u != metadata.end()) {
      user = std::string(u->second.data(), u->second.size());
    }

    auto g = metadata.find("nebula-groups");
    if (g != metadata.end()) {
      groups = nebula::common::Chars::split(g->second.data(), g->second.size());
    }
  }

  // compile the query into a plan
  LOG(INFO) << "Started a query for user: " << user << ", with groups:" << groups.size();
  QueryContext queryContext{ user, std::move(groups) };
  auto plan = handler_.compile(
    query, { request->start(), request->end() }, queryContext, error);
  if (error != ErrorCode::NONE) {
    return replyError(error, reply, 0);
  }
  N_ENSURE_NOT_NULL(plan, "Incorrect query compile");

  // create a remote connector and execute the query plan
  auto connector = std::make_shared<RemoteNodeConnector>(query);
  RowCursorPtr result = handler_.query(threadPool_, *plan, connector, error);
  auto durationMs = tick.elapsedMs();
  if (error != ErrorCode::NONE) {
    return replyError(error, reply, durationMs);
  }

  LOG(INFO) << "Finished a query in " << durationMs;

  // return normal serialized data
  auto stats = reply->mutable_stats();
  stats->set_querytimems(durationMs);
  // TODO(cao) - read it from underlying execution
  stats->set_rowsscanned(0);

  // TODO(cao) - use JSON for now, this should come from message request
  // User/client can specify what kind of format of result it expects
  reply->set_type(DataType::JSON);
  reply->set_data(ServiceProperties::jsonify(result, plan->getOutputSchema()));

  return Status::OK;
}

grpc::Status V1ServiceImpl::replyError(ErrorCode code, QueryResponse* reply, size_t durationMs) const {
  N_ENSURE_NE(code, ErrorCode::NONE, "Error Reply Code Not 0");

  auto error = ServiceProperties::errorMessage(code);
  auto stats = reply->mutable_stats();
  stats->set_error(code);
  stats->set_message(error);
  stats->set_querytimems(durationMs);

  return grpc::Status(grpc::StatusCode::INTERNAL, error);
}

// Logic and data behind the server's behavior.
class EchoServiceImpl final : public Echo::Service {
  Status EchoBack(ServerContext*, const EchoRequest* request, EchoResponse* reply) override {
    std::string prefix("This is from nebula: ");
    reply->set_message(prefix + request->name());
    return Status::OK;
  }
};

} // namespace server
} // namespace service
} // namespace nebula

std::string LoadClusterConfig() {
  // NCONF is one enviroment variable to overwrite cluster config in the runtime
  if (const char* nConf = std::getenv("NCONF")) {
    return nConf;
  }

  // reading it from gflags which is usually passed through docker build
  return FLAGS_CLS_CONF;
}

// NOTE: main function can't be placed inside a namespace
// Otherwise you may get undefined symbol "_main" error in link
void RunServer() {
  std::string server_address(fmt::format("0.0.0.0:{0}", nebula::service::base::ServiceProperties::PORT));
  nebula::service::server::EchoServiceImpl echoService;
  nebula::service::server::V1ServiceImpl v1Service;

  grpc::ServerBuilder builder;
  // Listen on the given address without any authentication mechanism.
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  // Register "service" as the instance through which we'll communicate with
  // clients. In this case it corresponds to an *synchronous* service.
  builder.RegisterService(&echoService).RegisterService(&v1Service);
  // Finally assemble the server.
  std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
  LOG(INFO) << "Nebula server listening on " << server_address;

  // a unique spec repo per server
  nebula::ingest::SpecRepo specRepo;

  // start node sync to sync all node's states and tasks
  // auto nsync = nebula::service::server::NodeSync::async(v1Service.pool(), specRepo, FLAGS_NODE_SYNC_INTERVAL);

  // TODO (cao): start a thread to sync up with etcd setup for cluster info.
  // register cluster info, we're using two different time based scheduelr currently
  // one is NodeSync uses folly::FunctionScheduler and TaskScheduler is built on top of EventBase
  // We're using task scheduler to pull all config changes and spec generation, assignment
  // NodeSync will be responsible to pull all state info from each Node and update them in server
  nebula::common::TaskScheduler taskScheduler;

  // having a local file system to detect change of cluster config
  auto& pool = v1Service.pool();
  std::string confSignature;
  taskScheduler.setInterval(
    FLAGS_CLS_CONF_UPDATE_INTERVAL,
    [&pool, &specRepo, &confSignature] {
      // load config into cluster info
      auto conf = LoadClusterConfig();

      // if the conf is a S3 file, download it and replace the conf as the local copy
      auto uri = nebula::storage::parse(conf);
      bool copied = false;
      if (uri.schema == "s3") {
        // create a s3 file system
        auto fs = nebula::storage::makeFS("s3", uri.host);
        conf = fs->copy(uri.path);
        copied = true;
      }

      // assuming everything else are local file - if not, let runtime fails you
      auto fs = nebula::storage::makeFS("local");
      auto fi = fs->info(conf);
      auto sign = fi.signature();

      // for copied data, they will have random name and latest timestamp
      // so we use size and content hash as its signature
      if (copied) {
        const auto size = fi.size;
        auto data = std::make_unique<char[]>(size);
        N_ENSURE_EQ(fs->read(conf, data.get()), size, "should read all bytes of the conf file");
        sign = fmt::format("{0}_{1}", size, nebula::common::Hasher::hash64(data.get(), size));
      }

      if (sign != confSignature) {
        LOG(INFO) << "Loading nebula cluster config: " << conf;

        // update the sign
        confSignature = sign;

        // load the new conf
        auto& ci = nebula::meta::ClusterInfo::singleton();
        ci.load(conf);

        // TODO(cao) - how to support table schema/column props evolution?
        nebula::execution::meta::TableService::singleton()->enroll(ci);
      }

      // if the file is copied tmp file, delete it
      if (copied) {
        unlink(conf.c_str());
      }

      {
        // sync cluster state
        nebula::service::server::NodeSync::sync(pool, specRepo);
      }
    });

  // NOTE that, this is blocking main thread to wait for server down
  // this may prevent system to exit properly, will revisit and revise.
  // run the loop.
  taskScheduler.run();

  // Wait for the server to shutdown. Note that some other thread must be
  // responsible for shutting down the server for this call to ever return.
  server->Wait();

  // shut down node sync process
  // nsync->shutdown();
}

int main(int argc, char** argv) {
  folly::init(&argc, &argv);
  FLAGS_logtostderr = 1;

  RunServer();

  return 0;
}