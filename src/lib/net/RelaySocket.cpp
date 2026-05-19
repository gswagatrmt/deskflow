/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "net/RelaySocket.h"

#include "arch/Arch.h"
#include "arch/ArchException.h"
#include "base/BaseException.h"
#include "base/IEventQueue.h"
#include "base/Log.h"
#include "mt/Lock.h"
#include "net/NetworkAddress.h"
#include "net/SocketException.h"
#include "net/SocketMultiplexer.h"
#include "net/TSocketMultiplexerMethodJob.h"


RelaySocket::RelaySocket(
    IEventQueue *events, SocketMultiplexer *socketMultiplexer, const std::string &relayHost, int relayPort,
    const std::string &roomId, IArchNetwork::AddressFamily family
)
    : TCPSocket(events, socketMultiplexer, family),
      m_relayHost(relayHost),
      m_relayPort(relayPort),
      m_roomId(roomId)
{
}

void RelaySocket::connect(const NetworkAddress & /*serverAddr*/)
{
  // Resolve relay address (must happen before acquiring mutex)
  NetworkAddress relayAddr(m_relayHost, m_relayPort);
  try {
    relayAddr.resolve();
  } catch (const BaseException &e) {
    LOG_WARN("relay: cannot resolve relay host '%s': %s", m_relayHost.c_str(), e.what());
    auto *info = new IDataSocket::ConnectionFailedInfo(e.what());
    getEvents()->addEvent(
        Event(EventTypes::DataSocketConnectionFailed, getEventTarget(), info, Event::EventFlags::DontFreeData)
    );
    return;
  } catch (const std::exception &e) {
    LOG_WARN("relay: cannot resolve relay host '%s': %s", m_relayHost.c_str(), e.what());
    auto *info = new IDataSocket::ConnectionFailedInfo(e.what());
    getEvents()->addEvent(
        Event(EventTypes::DataSocketConnectionFailed, getEventTarget(), info, Event::EventFlags::DontFreeData)
    );
    return;
  }

  {
    Lock lock(&getMutex());

    if (getSocket() == nullptr || isConnected()) {
      auto *info = new IDataSocket::ConnectionFailedInfo("busy");
      getEvents()->addEvent(
          Event(EventTypes::DataSocketConnectionFailed, getEventTarget(), info, Event::EventFlags::DontFreeData)
      );
      return;
    }

    m_relayState = RelayState::Connecting;

    try {
      if (ARCH->connectSocket(getSocket(), relayAddr.getAddress())) {
        // Immediate (synchronous) connect — send relay handshake right away
        const std::string hs = "RELAY " + m_roomId + " client\n";
        ARCH->writeSocket(getSocket(), hs.c_str(), hs.size());
        m_relayState = RelayState::Handshaking;
        setReadable(true);
        // Do NOT call onConnected() / sendEvent(DataSocketConnected) yet —
        // that only happens when the relay responds with CONNECTED.
      } else {
        // Asynchronous connect; serviceRelayConnecting fires when writable
        setWritable(true);
      }
    } catch (const ArchNetworkException &e) {
      throw SocketConnectException(e.what());
    }
  } // lock released

  setJob(newJob());
}

TCPSocket::JobResult RelaySocket::doRead()
{
  if (m_relayState == RelayState::Connected) {
    return TCPSocket::doRead();
  }

  if (m_relayState != RelayState::Handshaking) {
    return JobResult::Retry;
  }

  // Read relay handshake response
  uint8_t rawBuf[1024];
  const size_t n = ARCH->readSocket(getSocket(), rawBuf, sizeof(rawBuf));

  if (n == 0) {
    LOG_WARN("relay: server closed connection during handshake");
    auto *info = new IDataSocket::ConnectionFailedInfo("relay closed connection during handshake");
    getEvents()->addEvent(
        Event(EventTypes::DataSocketConnectionFailed, getEventTarget(), info, Event::EventFlags::DontFreeData)
    );
    setConnected(false);
    setReadable(false);
    setWritable(false);
    return JobResult::New;
  }

  m_lineBuffer.append(reinterpret_cast<const char *>(rawBuf), n);

  // Process complete lines
  size_t nl = std::string::npos;
  while ((nl = m_lineBuffer.find('\n')) != std::string::npos) {
    std::string line = m_lineBuffer.substr(0, nl);
    m_lineBuffer = m_lineBuffer.substr(nl + 1);

    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }

    if (line == "CONNECTED") {
      LOG_NOTE("relay: connected to peer in room '%s'", m_roomId.c_str());
      m_relayState = RelayState::Connected;

      // Enter connected state (mirrors TCPSocket::onConnected)
      setConnected(true);
      setReadable(true);
      setWritable(true);

      // Notify upper layers that the connection is ready
      getEvents()->addEvent(Event(EventTypes::DataSocketConnected, getEventTarget()));

      // Any bytes already received after CONNECTED belong to the deskflow protocol
      if (!m_lineBuffer.empty()) {
        m_inputBuffer.write(m_lineBuffer.data(), static_cast<uint32_t>(m_lineBuffer.size()));
        m_lineBuffer.clear();
        getEvents()->addEvent(Event(EventTypes::StreamInputReady, getEventTarget()));
      }
      return JobResult::New;

    } else if (line == "WAITING") {
      LOG_DEBUG("relay: waiting for peer in room '%s'", m_roomId.c_str());

    } else if (line.rfind("ERROR", 0) == 0) {
      LOG_WARN("relay: %s", line.c_str());
      auto *info = new IDataSocket::ConnectionFailedInfo(line.c_str());
      getEvents()->addEvent(
          Event(EventTypes::DataSocketConnectionFailed, getEventTarget(), info, Event::EventFlags::DontFreeData)
      );
      setConnected(false);
      setReadable(false);
      setWritable(false);
      return JobResult::New;
    }
  }

  return JobResult::Retry;
}

ISocketMultiplexerJob *RelaySocket::newJob()
{
  if (getSocket() == nullptr) {
    return nullptr;
  }

  if (m_relayState == RelayState::Connecting) {
    // TCP connection to relay in progress; wait for writable (= connected)
    if (!isWritable()) {
      return nullptr;
    }
    return new TSocketMultiplexerMethodJob<RelaySocket>(
        this, &RelaySocket::serviceRelayConnecting, getSocket(), false, true
    );
  }

  if (m_relayState == RelayState::Handshaking) {
    // Waiting for relay CONNECTED response; watch for readable
    return new TSocketMultiplexerMethodJob<RelaySocket>(
        this, &RelaySocket::serviceRelayHandshaking, getSocket(), true, false
    );
  }

  // Connected — use base class job dispatch (calls doRead / doWrite)
  return TCPSocket::newJob();
}

ISocketMultiplexerJob *
RelaySocket::serviceRelayConnecting(ISocketMultiplexerJob *job, bool /*read*/, bool write, bool error)
{
  Lock lock(&getMutex());

  if (error || write) {
    // Check for connection errors (mirrors TCPSocket::serviceConnecting)
    try {
      ARCH->throwErrorOnSocket(getSocket());
    } catch (const ArchNetworkException &e) {
      auto *info = new IDataSocket::ConnectionFailedInfo(e.what());
      getEvents()->addEvent(
          Event(EventTypes::DataSocketConnectionFailed, getEventTarget(), info, Event::EventFlags::DontFreeData)
      );
      setConnected(false);
      setReadable(false);
      setWritable(false);
      return newJob();
    }

    if (write) {
      // TCP connected to relay — send handshake line
      const std::string handshake = "RELAY " + m_roomId + " client\n";
      try {
        ARCH->writeSocket(getSocket(), handshake.c_str(), handshake.size());
      } catch (const ArchNetworkException &e) {
        LOG_WARN("relay: handshake write failed: %s", e.what());
        auto *info = new IDataSocket::ConnectionFailedInfo(e.what());
        getEvents()->addEvent(
            Event(EventTypes::DataSocketConnectionFailed, getEventTarget(), info, Event::EventFlags::DontFreeData)
        );
        return nullptr;
      }
      LOG_DEBUG("relay: handshake sent for room '%s'", m_roomId.c_str());
      m_relayState = RelayState::Handshaking;
      setWritable(false); // stop watching write until CONNECTED
      setReadable(true);
      return newJob();
    }
  }

  return job;
}

ISocketMultiplexerJob *
RelaySocket::serviceRelayHandshaking(ISocketMultiplexerJob *job, bool read, bool /*write*/, bool error)
{
  Lock lock(&getMutex());

  if (error) {
    auto *info = new IDataSocket::ConnectionFailedInfo("relay socket error");
    getEvents()->addEvent(
        Event(EventTypes::DataSocketConnectionFailed, getEventTarget(), info, Event::EventFlags::DontFreeData)
    );
    return nullptr;
  }

  if (read) {
    switch (doRead()) {
    case JobResult::Break:
      return nullptr;
    case JobResult::New:
      return newJob();
    default:
      break;
    }
  }

  return job;
}
