/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "net/RelayListenSocket.h"

#include "arch/Arch.h"
#include "arch/ArchException.h"
#include "base/IEventQueue.h"
#include "base/Log.h"
#include "net/NetworkAddress.h"
#include "net/SocketException.h"
#include "net/SocketMultiplexer.h"
#include "net/TCPSocket.h"
#include "net/TSocketMultiplexerMethodJob.h"

RelayListenSocket::RelayListenSocket(
    IEventQueue *events, SocketMultiplexer *socketMultiplexer, const std::string &relayHost, int relayPort,
    const std::string &roomId, IArchNetwork::AddressFamily family
)
    : m_events(events),
      m_socketMultiplexer(socketMultiplexer),
      m_relayHost(relayHost),
      m_relayPort(relayPort),
      m_roomId(roomId),
      m_family(family)
{
}

RelayListenSocket::~RelayListenSocket()
{
  try {
    disconnectRelayLocked();
  } catch (...) {
  }
}

void RelayListenSocket::bind(const NetworkAddress & /*addr*/)
{
  std::scoped_lock lock(m_mutex);
  m_bound = true;
  connectToRelayLocked();
}

void RelayListenSocket::close()
{
  std::scoped_lock lock(m_mutex);
  m_bound = false;
  disconnectRelayLocked();
}

void *RelayListenSocket::getEventTarget() const
{
  return const_cast<void *>(static_cast<const void *>(this));
}

std::unique_ptr<IDataSocket> RelayListenSocket::accept()
{
  ArchSocket connectedSock = nullptr;
  bool shouldReconnect = false;

  {
    std::scoped_lock lock(m_mutex);

    if (m_relayState != RelayState::Connected || m_socket == nullptr) {
      return nullptr;
    }

    // Transfer ownership of the relay socket
    connectedSock = m_socket;
    m_socket = nullptr;
    m_relayState = RelayState::Idle;
    shouldReconnect = m_bound;
  }

  // Create the data socket outside the lock
  auto dataSocket = std::make_unique<TCPSocket>(m_events, m_socketMultiplexer, connectedSock);

  // Re-register for the next client (same room code)
  if (shouldReconnect) {
    std::scoped_lock lock(m_mutex);
    connectToRelayLocked();
  }

  return dataSocket;
}

// ─────────────────── private (called with m_mutex held) ─────────────────────

void RelayListenSocket::connectToRelayLocked()
{
  disconnectRelayLocked();

  try {
    m_socket = ARCH->newSocket(m_family, IArchNetwork::SocketType::Stream);
  } catch (const ArchNetworkException &e) {
    LOG_WARN("relay-listen: failed to create socket: %s", e.what());
    return;
  }

  NetworkAddress relayAddr(m_relayHost, m_relayPort);
  try {
    relayAddr.resolve();
  } catch (...) {
    ARCH->closeSocket(m_socket);
    m_socket = nullptr;
    LOG_WARN("relay-listen: cannot resolve relay host '%s'", m_relayHost.c_str());
    return;
  }

  m_relayState = RelayState::Connecting;
  m_lineBuffer.clear();

  try {
    ARCH->setNoDelayOnSocket(m_socket, true);
    if (ARCH->connectSocket(m_socket, relayAddr.getAddress())) {
      // Synchronous connect: send handshake immediately
      const std::string hs = "RELAY " + m_roomId + " server\n";
      ARCH->writeSocket(m_socket, hs.c_str(), hs.size());
      m_relayState = RelayState::Waiting;
      LOG_DEBUG("relay-listen: handshake sent, waiting for client in room '%s'", m_roomId.c_str());
    }
    // Async connect: serviceConnecting fires when writable
  } catch (const ArchNetworkException &e) {
    LOG_WARN("relay-listen: connect failed: %s", e.what());
    ARCH->closeSocket(m_socket);
    m_socket = nullptr;
    m_relayState = RelayState::Idle;
    return;
  }

  m_socketMultiplexer->addSocket(this, newJobLocked());
}

void RelayListenSocket::disconnectRelayLocked()
{
  if (m_socket != nullptr) {
    m_socketMultiplexer->removeSocket(this);
    try {
      ARCH->closeSocket(m_socket);
    } catch (...) {
    }
    m_socket = nullptr;
  }
  m_relayState = RelayState::Idle;
}

ISocketMultiplexerJob *RelayListenSocket::newJobLocked()
{
  if (m_socket == nullptr) {
    return nullptr;
  }
  switch (m_relayState) {
  case RelayState::Connecting:
    return new TSocketMultiplexerMethodJob<RelayListenSocket>(
        this, &RelayListenSocket::serviceConnecting, m_socket, false, true
    );
  case RelayState::Waiting:
    return new TSocketMultiplexerMethodJob<RelayListenSocket>(
        this, &RelayListenSocket::serviceWaiting, m_socket, true, false
    );
  default:
    return nullptr;
  }
}

// ─────────── multiplexer callbacks (called from multiplexer thread) ──────────
// These do NOT hold m_mutex because they may need to call connectToRelayLocked.

ISocketMultiplexerJob *
RelayListenSocket::serviceConnecting(ISocketMultiplexerJob *job, bool /*read*/, bool write, bool error)
{
  if (error || write) {
    try {
      ARCH->throwErrorOnSocket(m_socket);
    } catch (const ArchNetworkException &e) {
      LOG_WARN("relay-listen: connection to relay failed: %s", e.what());
      {
        std::scoped_lock lock(m_mutex);
        disconnectRelayLocked();
        if (m_bound) connectToRelayLocked();
      }
      return nullptr;
    }

    if (write) {
      const std::string hs = "RELAY " + m_roomId + " server\n";
      try {
        ARCH->writeSocket(m_socket, hs.c_str(), hs.size());
      } catch (const ArchNetworkException &e) {
        LOG_WARN("relay-listen: handshake write failed: %s", e.what());
        std::scoped_lock lock(m_mutex);
        disconnectRelayLocked();
        return nullptr;
      }
      LOG_DEBUG("relay-listen: registered as server for room '%s'", m_roomId.c_str());
      {
        std::scoped_lock lock(m_mutex);
        m_relayState = RelayState::Waiting;
        return newJobLocked();
      }
    }
  }
  return job;
}

ISocketMultiplexerJob *
RelayListenSocket::serviceWaiting(ISocketMultiplexerJob *job, bool read, bool /*write*/, bool error)
{
  if (error) {
    LOG_WARN("relay-listen: socket error while waiting for client");
    std::scoped_lock lock(m_mutex);
    disconnectRelayLocked();
    if (m_bound) connectToRelayLocked();
    return nullptr;
  }

  if (!read) {
    return job;
  }

  uint8_t buf[512];
  size_t n = 0;
  try {
    n = ARCH->readSocket(m_socket, buf, sizeof(buf));
  } catch (const ArchNetworkException &e) {
    LOG_WARN("relay-listen: read error: %s", e.what());
    std::scoped_lock lock(m_mutex);
    disconnectRelayLocked();
    if (m_bound) connectToRelayLocked();
    return nullptr;
  }

  if (n == 0) {
    LOG_WARN("relay-listen: relay closed connection; retrying");
    std::scoped_lock lock(m_mutex);
    disconnectRelayLocked();
    if (m_bound) connectToRelayLocked();
    return nullptr;
  }

  m_lineBuffer.append(reinterpret_cast<const char *>(buf), n);

  size_t nl = std::string::npos;
  while ((nl = m_lineBuffer.find('\n')) != std::string::npos) {
    std::string line = m_lineBuffer.substr(0, nl);
    m_lineBuffer = m_lineBuffer.substr(nl + 1);
    if (!line.empty() && line.back() == '\r') line.pop_back();

    if (line == "WAITING") {
      LOG_DEBUG("relay-listen: confirmed waiting for client in room '%s'", m_roomId.c_str());

    } else if (line == "CONNECTED") {
      LOG_NOTE("relay-listen: client joined room '%s'", m_roomId.c_str());
      {
        std::scoped_lock lock(m_mutex);
        m_relayState = RelayState::Connected;
        m_socketMultiplexer->removeSocket(this);
      }
      m_events->addEvent(Event(EventTypes::ListenSocketConnecting, this));
      return nullptr;

    } else if (line.rfind("ERROR", 0) == 0) {
      LOG_WARN("relay-listen: relay error: %s", line.c_str());
      std::scoped_lock lock(m_mutex);
      disconnectRelayLocked();
      if (m_bound) connectToRelayLocked();
      return nullptr;
    }
  }

  return job;
}
