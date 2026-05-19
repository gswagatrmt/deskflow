/**
 * Deskflow Internet Relay Server
 *
 * Brokers TCP connections between deskflow peers over the internet.
 * Each "room" pairs exactly one server and one client; after pairing the relay
 * transparently forwards raw bytes in both directions.
 *
 * Protocol (line-based, each line terminated by \n):
 *   Client → Server:  RELAY <room_id> <role>   (role = "server" | "client")
 *   Server → Client:  WAITING                   (waiting for peer)
 *   Server → Client:  CONNECTED                 (peer joined, bridge active)
 *   Server → Client:  ERROR <message>           (error, connection closed)
 *
 * After CONNECTED both sides send raw deskflow protocol bytes through the relay.
 * Any bytes buffered before CONNECTED on both sides are flushed once bridged.
 */

'use strict';

const net = require('net');
const crypto = require('crypto');

const PORT = parseInt(process.env.RELAY_PORT || '24801', 10);
const MAX_ROOM_ID_LEN = 64;
const HANDSHAKE_TIMEOUT_MS = 30_000;

// Map<roomId, { server?: PeerState, client?: PeerState }>
const rooms = new Map();

class PeerState {
  constructor(socket) {
    this.socket = socket;
    this.handshakeDone = false;
    this.buffer = Buffer.alloc(0);     // data buffered before bridge
    this.partner = null;               // other PeerState once bridged
  }
}

function sendLine(socket, line) {
  try {
    socket.write(line + '\n');
  } catch (_) {}
}

function closeSocket(socket) {
  try { socket.destroy(); } catch (_) {}
}

function bridgePeers(serverState, clientState) {
  serverState.partner = clientState;
  clientState.partner = serverState;

  sendLine(serverState.socket, 'CONNECTED');
  sendLine(clientState.socket, 'CONNECTED');

  // Flush any buffered data accumulated during handshake wait
  if (serverState.buffer.length > 0) {
    try { clientState.socket.write(serverState.buffer); } catch (_) {}
    serverState.buffer = Buffer.alloc(0);
  }
  if (clientState.buffer.length > 0) {
    try { serverState.socket.write(clientState.buffer); } catch (_) {}
    clientState.buffer = Buffer.alloc(0);
  }

  // From now on, forward all incoming data directly to partner
  serverState.socket.removeAllListeners('data');
  clientState.socket.removeAllListeners('data');

  serverState.socket.on('data', (chunk) => {
    try { clientState.socket.write(chunk); } catch (_) {}
  });
  clientState.socket.on('data', (chunk) => {
    try { serverState.socket.write(chunk); } catch (_) {}
  });
}

function removePeerFromRoom(roomId, role) {
  const room = rooms.get(roomId);
  if (!room) return;
  delete room[role];
  if (!room.server && !room.client) {
    rooms.delete(roomId);
    console.log(`[relay] room '${roomId}' closed`);
  }
}

function handleConnection(socket) {
  const remote = `${socket.remoteAddress}:${socket.remotePort}`;
  console.log(`[relay] new connection from ${remote}`);

  let roomId = null;
  let role = null;
  const peer = new PeerState(socket);
  let lineBuffer = '';
  let handshakeDone = false;

  // Enforce handshake timeout
  const handshakeTimer = setTimeout(() => {
    if (!handshakeDone) {
      sendLine(socket, 'ERROR handshake timeout');
      closeSocket(socket);
    }
  }, HANDSHAKE_TIMEOUT_MS);

  socket.on('data', (chunk) => {
    if (!handshakeDone) {
      // Still parsing the handshake line
      lineBuffer += chunk.toString('latin1');
      const nl = lineBuffer.indexOf('\n');
      if (nl === -1) {
        if (lineBuffer.length > 256) {
          sendLine(socket, 'ERROR handshake too long');
          closeSocket(socket);
        }
        return;
      }

      const line = lineBuffer.substring(0, nl).trim();
      // Any bytes after the newline are buffered pre-bridge data
      const rest = lineBuffer.substring(nl + 1);
      if (rest.length > 0) {
        peer.buffer = Buffer.concat([peer.buffer, Buffer.from(rest, 'latin1')]);
      }
      lineBuffer = '';

      const parts = line.split(' ');
      if (parts.length < 3 || parts[0] !== 'RELAY') {
        sendLine(socket, 'ERROR invalid handshake (expected: RELAY <room> <role>)');
        closeSocket(socket);
        return;
      }

      roomId = parts[1];
      role = parts[2].toLowerCase();

      if (roomId.length === 0 || roomId.length > MAX_ROOM_ID_LEN) {
        sendLine(socket, 'ERROR invalid room id');
        closeSocket(socket);
        return;
      }

      if (role !== 'server' && role !== 'client') {
        sendLine(socket, 'ERROR invalid role (must be server or client)');
        closeSocket(socket);
        return;
      }

      if (!rooms.has(roomId)) {
        rooms.set(roomId, {});
      }
      const room = rooms.get(roomId);

      if (room[role]) {
        sendLine(socket, `ERROR role '${role}' already taken in room '${roomId}'`);
        closeSocket(socket);
        return;
      }

      room[role] = peer;
      handshakeDone = true;
      clearTimeout(handshakeTimer);

      const otherRole = role === 'server' ? 'client' : 'server';
      const otherPeer = room[otherRole];

      if (otherPeer) {
        // Both peers present — bridge immediately
        bridgePeers(room.server, room.client);
        console.log(`[relay] room '${roomId}' bridged (${remote} as ${role})`);
      } else {
        sendLine(socket, 'WAITING');
        console.log(`[relay] room '${roomId}' waiting for ${otherRole} (${remote} as ${role})`);
      }
    } else {
      // Bridge is active; this handler should have been replaced by bridgePeers().
      // Buffer any stray data just in case of a race.
      peer.buffer = Buffer.concat([peer.buffer, chunk]);
    }
  });

  socket.on('close', () => {
    clearTimeout(handshakeTimer);
    console.log(`[relay] ${remote} disconnected (room=${roomId ?? 'none'} role=${role ?? 'none'})`);
    if (roomId && role) {
      removePeerFromRoom(roomId, role);
    }
    if (peer.partner) {
      console.log(`[relay] closing partner in room '${roomId}'`);
      closeSocket(peer.partner.socket);
      peer.partner = null;
    }
  });

  socket.on('error', (err) => {
    console.error(`[relay] socket error from ${remote}: ${err.message}`);
  });
}

// Admin HTTP endpoint (optional, for health checks / room listing)
let httpServer = null;
if (process.env.RELAY_ADMIN_PORT) {
  const http = require('http');
  const adminPort = parseInt(process.env.RELAY_ADMIN_PORT, 10);
  httpServer = http.createServer((req, res) => {
    if (req.url === '/health') {
      res.writeHead(200, { 'Content-Type': 'application/json' });
      res.end(JSON.stringify({ status: 'ok', rooms: rooms.size }));
    } else if (req.url === '/rooms') {
      const info = {};
      for (const [id, room] of rooms) {
        info[id] = { hasServer: !!room.server, hasClient: !!room.client };
      }
      res.writeHead(200, { 'Content-Type': 'application/json' });
      res.end(JSON.stringify(info));
    } else {
      res.writeHead(404);
      res.end();
    }
  });
  httpServer.listen(adminPort, () => {
    console.log(`[relay] admin HTTP on port ${adminPort}`);
  });
}

const server = net.createServer(handleConnection);

server.on('error', (err) => {
  console.error(`[relay] server error: ${err.message}`);
  process.exit(1);
});

server.listen(PORT, '0.0.0.0', () => {
  console.log(`[relay] Deskflow relay server listening on 0.0.0.0:${PORT}`);
  console.log(`[relay] Set RELAY_PORT env var to override port (default 24801)`);
  console.log(`[relay] Set RELAY_ADMIN_PORT env var to enable health/room HTTP endpoint`);
});

process.on('SIGINT', () => {
  console.log('[relay] shutting down');
  server.close();
  if (httpServer) httpServer.close();
  process.exit(0);
});
