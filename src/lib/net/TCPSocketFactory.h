/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Deskflow Developers
 * SPDX-FileCopyrightText: (C) 2012 - 2016 Symless Ltd.
 * SPDX-FileCopyrightText: (C) 2002 Chris Schoeneman
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "arch/IArchNetwork.h"
#include "net/ISocketFactory.h"

#include <QString>
#include <string>

class IEventQueue;
class SocketMultiplexer;

//! Socket factory for TCP sockets
class TCPSocketFactory : public ISocketFactory
{
public:
  TCPSocketFactory(IEventQueue *events, SocketMultiplexer *socketMultiplexer);
  ~TCPSocketFactory() override = default;

  // ISocketFactory overrides
  IDataSocket *create(
      IArchNetwork::AddressFamily family = IArchNetwork::AddressFamily::INet,
      SecurityLevel securityLevel = SecurityLevel::PlainText
  ) const override;
  IListenSocket *createListen(
      IArchNetwork::AddressFamily family = IArchNetwork::AddressFamily::INet,
      SecurityLevel securityLevel = SecurityLevel::PlainText
  ) const override;

  /// Create a client-side data socket that connects through a relay server.
  IDataSocket *createRelay(
      const std::string &relayHost, int relayPort, const std::string &roomId,
      IArchNetwork::AddressFamily family = IArchNetwork::AddressFamily::INet
  ) const;

  /// Create a server-side listen socket that accepts connections through a relay server.
  IListenSocket *createRelayListen(
      const std::string &relayHost, int relayPort, const std::string &roomId,
      IArchNetwork::AddressFamily family = IArchNetwork::AddressFamily::INet
  ) const;

private:
  IEventQueue *m_events;
  SocketMultiplexer *m_socketMultiplexer;
};
