/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "net/ISocketFactory.h"
#include "net/TCPSocketFactory.h"

#include <string>

class IEventQueue;
class SocketMultiplexer;

/// @brief Socket factory that creates relay-tunnelled sockets.
///
/// Drop-in replacement for TCPSocketFactory when internet relay mode is active.
/// create() returns a RelaySocket; createListen() returns a RelayListenSocket.
class RelaySocketFactory : public ISocketFactory
{
public:
  RelaySocketFactory(
      IEventQueue *events, SocketMultiplexer *socketMultiplexer, const std::string &relayHost, int relayPort,
      const std::string &roomId
  );
  ~RelaySocketFactory() override = default;

  IDataSocket *create(
      IArchNetwork::AddressFamily family = IArchNetwork::AddressFamily::INet,
      SecurityLevel securityLevel = SecurityLevel::PlainText
  ) const override;

  IListenSocket *createListen(
      IArchNetwork::AddressFamily family = IArchNetwork::AddressFamily::INet,
      SecurityLevel securityLevel = SecurityLevel::PlainText
  ) const override;

private:
  TCPSocketFactory m_tcpFactory;
  std::string m_relayHost;
  int m_relayPort{24801};
  std::string m_roomId;
};
