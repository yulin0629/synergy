/*
 * synergy -- mouse and keyboard sharing utility
 * Copyright (C) 2012 Nick Bolton
 * 
 * This package is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * found in the file COPYING that should have accompanied this file.
 * 
 * This package is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "CIpcServer.h"
#include "Ipc.h"
#include "IEventQueue.h"
#include "TMethodEventJob.h"
#include "CEvent.h"
#include "CLog.h"
#include "CIpcClientProxy.h"
#include "IStream.h"
#include "IDataSocket.h"
#include "CIpcMessage.h"

CEvent::Type			CIpcServer::s_clientConnectedEvent = CEvent::kUnknown;

CIpcServer::CIpcServer() :
m_address(CNetworkAddress(IPC_HOST, IPC_PORT))
{
	m_address.resolve();

	EVENTQUEUE->adoptHandler(
		IListenSocket::getConnectingEvent(), &m_socket,
		new TMethodEventJob<CIpcServer>(
		this, &CIpcServer::handleClientConnecting));
}

CIpcServer::~CIpcServer()
{
	CClientSet::iterator it;
	for (it = m_clients.begin(); it != m_clients.end(); it++) {
		delete *it;
	}
	m_clients.empty();

	EVENTQUEUE->removeHandler(m_socket.getConnectingEvent(), &m_socket);
}

void
CIpcServer::listen()
{
	m_socket.bind(m_address);
}

void
CIpcServer::handleClientConnecting(const CEvent&, void*)
{
	synergy::IStream* stream = m_socket.accept();
	if (stream == NULL) {
		return;
	}

	LOG((CLOG_DEBUG "accepted ipc client connection"));
	CIpcClientProxy* proxy = new CIpcClientProxy(*stream);
	m_clients.insert(proxy);

	EVENTQUEUE->adoptHandler(
		CIpcClientProxy::getDisconnectedEvent(), proxy,
		new TMethodEventJob<CIpcServer>(
		this, &CIpcServer::handleClientDisconnected));

	EVENTQUEUE->addEvent(CEvent(
		getClientConnectedEvent(), this, proxy, CEvent::kDontFreeData));
}

void
CIpcServer::handleClientDisconnected(const CEvent& e, void*)
{
	CIpcClientProxy* proxy = static_cast<CIpcClientProxy*>(e.getTarget());

	EVENTQUEUE->removeHandler(
		CIpcClientProxy::getDisconnectedEvent(), proxy);

	CClientSet::iterator& it = m_clients.find(proxy);
	delete proxy;
	m_clients.erase(it);
	LOG((CLOG_DEBUG "ipc client proxy removed, connected=%d", m_clients.size()));
}

bool
CIpcServer::hasClients(EIpcClientType clientType) const
{
	if (m_clients.size() == 0) {
		return false;
	}

	CClientSet::iterator it;
	for (it = m_clients.begin(); it != m_clients.end(); it++) {
		// at least one client is alive and type matches, there are clients.
		CIpcClientProxy* p = *it;
		if (!p->m_disconnecting && p->m_clientType == clientType) {
			return true;
		}
	}

	// all clients must be disconnecting, no active clients.
	return false;
}

CEvent::Type
CIpcServer::getClientConnectedEvent()
{
	return EVENTQUEUE->registerTypeOnce(
		s_clientConnectedEvent, "CIpcServer::clientConnected");
}

void
CIpcServer::send(const CIpcMessage& message, EIpcClientType filterType)
{
	CClientSet::iterator it;
	for (it = m_clients.begin(); it != m_clients.end(); it++) {
		CIpcClientProxy* proxy = *it;
		if (proxy->m_clientType == filterType) {
			proxy->send(message);
		}
	}
}