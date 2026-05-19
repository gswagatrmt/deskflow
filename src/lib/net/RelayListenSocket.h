/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "arch/IArchNetwork.h"
#include "net/IListenSocket.h"

#include <memory>
#include <mutex>
#include <string>

class IEventQueue;
class IDataSocket;
class SocketMultiplexer;
class ISocketMultiplexerJob;

/// @brief Listen socket that accepts connections through a relay server.
///
/// Instead of binding a local port, RelayListenSocket connects to the relay
/// server as the "server" peer for a given room code.  When a client peer
/// joins the same room the relay sends "CONNECTED\n" and a
/// ListenSocketConnecting event is fired.  accept() then returns a TCPSocket
/// that wraps the underlying relay connection (which is already bridged to
/// the remote deskflow client).
///
/// After each accept() the socket automatically reconnects to the relay so
/// that subsequent clients can join using the same room code.
class RelayListenSocket : public IListenSocket
{
public:
  RelayListenSocket(
      IEventQueue *events, SocketMultiplexer *socketMultiplexer, const std::string &relayHost, int relayPort,
      const std::string &roomId, IArchNetwork::AddressFamily family = IArchNetwork::AddressFamily::INet
  );
  ~RelayListenSocket() override;

  // ISocket overrides
  void bind(const NetworkAddress &) override;
  void close() override;
  void *getEventTarget() const override;

  // IListenSocket overrides
  std::unique_ptr<IDataSocket> accept() override;

private:
  enum class RelayState
  {
    Idle,
    Connecting, ///< TCP connection to relay in progress.
    Waiting,    ///< Connected to relay; waiting for client peer to join the room.
    Connected   ///< Client peer joined; relay bridged; ready to accept().
  };

  /// Must be called with m_mutex held.
  void connectToRelayLocked();
  /// Must be called with m_mutex held.
  void disconnectRelayLocked();
  /// Must be called with m_mutex held.
  ISocketMultiplexerJob *newJobLocked();

  ISocketMultiplexerJob *serviceConnecting(ISocketMultiplexerJob *, bool read, bool write, bool error);
  ISocketMultiplexerJob *serviceWaiting(ISocketMultiplexerJob *, bool read, bool write, bool error);

  IEventQueue *m_events;
  SocketMultiplexer *m_socketMultiplexer;
  std::string m_relayHost;
  int m_relayPort{24801};
  std::string m_roomId;
  IArchNetwork::AddressFamily m_family;

  ArchSocket m_socket{nullptr};
  RelayState m_relayState{RelayState::Idle};
  std::string m_lineBuffer;
  std::mutex m_mutex;

  bool m_bound{false};
};
