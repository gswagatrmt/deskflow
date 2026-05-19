/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "net/RelaySocketFactory.h"
#include "net/RelayListenSocket.h"
#include "net/RelaySocket.h"

RelaySocketFactory::RelaySocketFactory(
    IEventQueue *events, SocketMultiplexer *socketMultiplexer, const std::string &relayHost, int relayPort,
    const std::string &roomId
)
    : m_tcpFactory(events, socketMultiplexer),
      m_relayHost(relayHost),
      m_relayPort(relayPort),
      m_roomId(roomId)
{
}

IDataSocket *RelaySocketFactory::create(IArchNetwork::AddressFamily family, SecurityLevel /*securityLevel*/) const
{
  // Relay connections are always encrypted by the relay tunnel;
  // TLS inside the relay would add overhead without extra benefit,
  // but can be layered by wrapping the RelaySocket in SecureSocket if desired.
  return m_tcpFactory.createRelay(m_relayHost, m_relayPort, m_roomId, family);
}

IListenSocket *RelaySocketFactory::createListen(IArchNetwork::AddressFamily family, SecurityLevel /*securityLevel*/) const
{
  return m_tcpFactory.createRelayListen(m_relayHost, m_relayPort, m_roomId, family);
}
