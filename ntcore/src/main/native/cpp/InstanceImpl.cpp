// Copyright (c) FIRST and other WPILib contributors.
// Open Source Software; you can modify and/or share it under the terms of
// the WPILib BSD license file in the root directory of this project.

#include "InstanceImpl.h"

using namespace nt;

std::atomic<int> InstanceImpl::s_default{-1};
std::atomic<InstanceImpl*> InstanceImpl::s_instances[kNumInstances];
wpi::mutex InstanceImpl::s_mutex;

using namespace std::placeholders;

InstanceImpl::InstanceImpl(int inst)
    : logger_impl(inst),
      logger(
          std::bind(&LoggerImpl::Log, &logger_impl, _1, _2, _3, _4)),  // NOLINT
      connectionList(inst),
      localStorage(inst, logger),
      m_inst{inst} {
  logger.set_min_level(logger_impl.GetMinLevel());
}

InstanceImpl* InstanceImpl::GetDefault() {
  return Get(GetDefaultIndex());
}

InstanceImpl* InstanceImpl::Get(int inst) {
  if (inst < 0 || inst >= kNumInstances) {
    return nullptr;
  }
  return s_instances[inst];
}

int InstanceImpl::GetDefaultIndex() {
  int inst = s_default;
  if (inst >= 0) {
    return inst;
  }

  // slow path
  std::scoped_lock lock(s_mutex);

  // double-check
  inst = s_default;
  if (inst >= 0) {
    return inst;
  }

  // alloc and save
  inst = AllocImpl();
  s_default = inst;
  return inst;
}

int InstanceImpl::Alloc() {
  std::scoped_lock lock(s_mutex);
  return AllocImpl();
}

int InstanceImpl::AllocImpl() {
  int inst = 0;
  for (; inst < kNumInstances; ++inst) {
    if (!s_instances[inst]) {
      s_instances[inst] = new InstanceImpl(inst);
      return inst;
    }
  }
  return -1;
}

void InstanceImpl::Destroy(int inst) {
  std::scoped_lock lock(s_mutex);
  if (inst < 0 || inst >= kNumInstances) {
    return;
  }

  delete s_instances[inst].exchange(nullptr);
}

void InstanceImpl::StartLocal() {
  std::scoped_lock lock{m_mutex};
  if (networkMode != NT_NET_MODE_NONE) {
    return;
  }
  networkMode = NT_NET_MODE_LOCAL;
}

void InstanceImpl::StopLocal() {
  std::scoped_lock lock{m_mutex};
  if ((networkMode & NT_NET_MODE_LOCAL) == 0) {
    return;
  }
  networkMode = NT_NET_MODE_NONE;
}

void InstanceImpl::StartServer(std::string_view persistFilename,
                               std::string_view listenAddress,
                               unsigned int port3, unsigned int port4) {
  std::scoped_lock lock{m_mutex};
  if (networkMode != NT_NET_MODE_NONE) {
    return;
  }
  m_networkServer = std::make_shared<NetworkServer>(
      persistFilename, listenAddress, port3, port4, localStorage,
      connectionList, logger, [this] {
        std::scoped_lock lock{m_mutex};
        networkMode &= ~NT_NET_MODE_STARTING;
      });
  networkMode = NT_NET_MODE_SERVER | NT_NET_MODE_STARTING;
}

void InstanceImpl::StopServer() {
  std::scoped_lock lock{m_mutex};
  if ((networkMode & NT_NET_MODE_SERVER) == 0) {
    return;
  }
  m_networkServer.reset();
  networkMode = NT_NET_MODE_NONE;
}

void InstanceImpl::StartClient3(std::string_view identity) {
  std::scoped_lock lock{m_mutex};
  if (networkMode != NT_NET_MODE_NONE) {
    return;
  }
  m_networkClient = std::make_shared<NetworkClient3>(
      m_inst, identity, localStorage, connectionList, logger);
  if (!m_servers.empty()) {
    m_networkClient->SetServers(m_servers);
  }
  networkMode = NT_NET_MODE_CLIENT3;
}

void InstanceImpl::StartClient4(std::string_view identity) {
  std::scoped_lock lock{m_mutex};
  if (networkMode != NT_NET_MODE_NONE) {
    return;
  }
  m_networkClient = std::make_shared<NetworkClient>(
      m_inst, identity, localStorage, connectionList, logger);
  if (!m_servers.empty()) {
    m_networkClient->SetServers(m_servers);
  }
  networkMode = NT_NET_MODE_CLIENT4;
}

void InstanceImpl::StopClient() {
  std::scoped_lock lock{m_mutex};
  if ((networkMode & (NT_NET_MODE_CLIENT3 | NT_NET_MODE_CLIENT4)) == 0) {
    return;
  }
  m_networkClient.reset();
  networkMode = NT_NET_MODE_NONE;
}

void InstanceImpl::SetServers(
    std::span<const std::pair<std::string, unsigned int>> servers) {
  std::scoped_lock lock{m_mutex};
  m_servers = {servers.begin(), servers.end()};
  if (m_networkClient) {
    m_networkClient->SetServers(servers);
  }
}

std::shared_ptr<NetworkServer> InstanceImpl::GetServer() {
  std::scoped_lock lock{m_mutex};
  return m_networkServer;
}

std::shared_ptr<INetworkClient> InstanceImpl::GetClient() {
  std::scoped_lock lock{m_mutex};
  return m_networkClient;
}
