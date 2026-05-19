/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "net/TCPSocket.h"

#include <string>

class IEventQueue;
class SocketMultiplexer;

/// @brief TCP socket that connects through a deskflow relay server.
///
/// The relay handshake runs as part of the normal async connection flow:
///  1. connect() connects to the relay server (not the real server address).
///  2. serviceRelayConnecting() sends "RELAY <roomId> client\n" once the TCP
///     connection to the relay is established.
///  3. serviceRelayHandshaking() / doRead() processes relay response lines
///     ("WAITING", "CONNECTED", "ERROR ...").
///  4. Once "CONNECTED" is received the socket fires DataSocketConnected and
///     thereafter behaves identically to a plain TCPSocket.
class RelaySocket : public TCPSocket
{
public:
  RelaySocket(
      IEventQueue *events, SocketMultiplexer *socketMultiplexer, const std::string &relayHost, int relayPort,
      const std::string &roomId, IArchNetwork::AddressFamily family = IArchNetwork::AddressFamily::INet
  );
  ~RelaySocket() override = default;

  /// Connect via the relay (the @p serverAddr argument is ignored).
  void connect(const NetworkAddress &serverAddr) override;

  /// Create the appropriate multiplexer job for the current relay state.
  ISocketMultiplexerJob *newJob() override;

protected:
  /// Intercepts reads during the Handshaking phase to parse relay responses.
  JobResult doRead() override;

private:
  enum class RelayState
  {
    Idle,
    Connecting,  ///< TCP connection to relay server in progress.
    Handshaking, ///< Handshake sent; waiting for CONNECTED response.
    Connected    ///< Relay bridged; socket operates as a plain TCPSocket.
  };

  ISocketMultiplexerJob *serviceRelayConnecting(ISocketMultiplexerJob *, bool read, bool write, bool error);
  ISocketMultiplexerJob *serviceRelayHandshaking(ISocketMultiplexerJob *, bool read, bool write, bool error);

  std::string m_relayHost;
  int m_relayPort{24801};
  std::string m_roomId;
  RelayState m_relayState{RelayState::Idle};
  std::string m_lineBuffer;
};
