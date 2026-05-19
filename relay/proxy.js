/**
 * Deskflow Relay Proxy
 *
 * Runs locally on each machine. Bridges the relay server to a local TCP port
 * so that deskflow itself doesn't need to know about the relay at all.
 *
 * Server-mode proxy (run on the machine that has the keyboard/mouse):
 *   node proxy.js server <relay_host> <relay_port> <room_id> [local_deskflow_port]
 *   - Connects to relay as "server" for <room_id>
 *   - Forwards relay traffic to local deskflow server on <local_deskflow_port> (default 24800)
 *   - After relay connects, a TCP connection is made to 127.0.0.1:<local_deskflow_port>
 *
 * Client-mode proxy (run on the machine being controlled):
 *   node proxy.js client <relay_host> <relay_port> <room_id> [local_listen_port]
 *   - Listens on <local_listen_port> (default 24800)
 *   - When deskflow client connects to that local port, joins relay as "client" for <room_id>
 *   - Bridges the relay connection to the local deskflow client
 *
 * Examples:
 *   # On Device A (server/controller):
 *   node proxy.js server relay.example.com 24801 myroom123
 *
 *   # On Device B (client/controlled):
 *   node proxy.js client relay.example.com 24801 myroom123
 *
 * For bidirectional control (A controls B AND B controls A):
 *   # On Device A:
 *   node proxy.js server relay.example.com 24801 A-controls-B        # A is server
 *   node proxy.js client relay.example.com 24801 B-controls-A 24801  # B is server
 *
 *   # On Device B:
 *   node proxy.js client relay.example.com 24801 A-controls-B        # A controls B
 *   node proxy.js server relay.example.com 24801 B-controls-A 24800  # B is server
 */

'use strict';

const net = require('net');

const [, , mode, relayHost, relayPortStr, roomId, localPortStr] = process.argv;

function usage() {
  console.error('Usage: node proxy.js <server|client> <relay_host> <relay_port> <room_id> [local_port]');
  process.exit(1);
}

if (!mode || !relayHost || !relayPortStr || !roomId) usage();
if (mode !== 'server' && mode !== 'client') usage();

const relayPort = parseInt(relayPortStr, 10);
const localPort = parseInt(localPortStr || '24800', 10);

function connectToRelay(role, onReady, onError) {
  const relay = net.createConnection({ host: relayHost, port: relayPort }, () => {
    console.log(`[proxy] connected to relay ${relayHost}:${relayPort}`);
    relay.write(`RELAY ${roomId} ${role}\n`);
  });

  let lineBuffer = '';
  let handshakeDone = false;

  relay.on('data', (chunk) => {
    if (handshakeDone) {
      onReady(relay, chunk);
      return;
    }

    lineBuffer += chunk.toString('latin1');
    const nl = lineBuffer.indexOf('\n');
    if (nl === -1) return;

    const line = lineBuffer.substring(0, nl).trim();
    const rest = lineBuffer.substring(nl + 1);
    lineBuffer = '';

    if (line === 'WAITING') {
      console.log(`[proxy] waiting for peer in room '${roomId}' as ${role}...`);
    } else if (line === 'CONNECTED') {
      console.log(`[proxy] relay connected! room '${roomId}' bridged.`);
      handshakeDone = true;
      // Pass any bytes that arrived after CONNECTED to the ready callback
      onReady(relay, rest.length > 0 ? Buffer.from(rest, 'latin1') : null);
    } else if (line.startsWith('ERROR')) {
      console.error(`[proxy] relay error: ${line}`);
      relay.destroy();
      onError(new Error(line));
    }
  });

  relay.on('error', (err) => {
    if (!handshakeDone) onError(err);
    else console.error(`[proxy] relay error: ${err.message}`);
  });

  relay.on('close', () => {
    if (!handshakeDone) {
      onError(new Error('relay closed before handshake complete'));
    } else {
      console.log('[proxy] relay connection closed');
    }
  });

  return relay;
}

function pipe(a, b, initialDataForB) {
  if (initialDataForB && initialDataForB.length > 0) {
    b.write(initialDataForB);
  }
  a.on('data', (d) => { try { b.write(d); } catch (_) {} });
  b.on('data', (d) => { try { a.write(d); } catch (_) {} });
  a.on('close', () => { try { b.destroy(); } catch (_) {} });
  b.on('close', () => { try { a.destroy(); } catch (_) {} });
  a.on('error', (e) => { console.error('[proxy] pipe error (a):', e.message); try { b.destroy(); } catch (_) {} });
  b.on('error', (e) => { console.error('[proxy] pipe error (b):', e.message); try { a.destroy(); } catch (_) {} });
}

if (mode === 'server') {
  // Server proxy: waits in relay room, then connects to local deskflow server
  console.log(`[proxy] SERVER mode | room='${roomId}' | relay=${relayHost}:${relayPort} | deskflow=127.0.0.1:${localPort}`);

  function connectRelay() {
    connectToRelay(
      'server',
      (relaySocket, initialBytes) => {
        // Relay is paired — connect to local deskflow server
        const local = net.createConnection({ host: '127.0.0.1', port: localPort }, () => {
          console.log(`[proxy] connected to local deskflow server on port ${localPort}`);
          pipe(relaySocket, local, initialBytes);
        });
        local.on('error', (err) => {
          console.error(`[proxy] local deskflow connection failed: ${err.message}`);
          relaySocket.destroy();
        });
        // Re-register after this connection ends
        relaySocket.on('close', () => {
          console.log('[proxy] session ended, reconnecting to relay...');
          setTimeout(connectRelay, 2000);
        });
      },
      (err) => {
        console.error(`[proxy] relay error: ${err.message}, retrying in 5s...`);
        setTimeout(connectRelay, 5000);
      }
    );
  }
  connectRelay();

} else {
  // Client proxy: listens locally, then joins relay room for each connection
  console.log(`[proxy] CLIENT mode | room='${roomId}' | relay=${relayHost}:${relayPort} | listen=127.0.0.1:${localPort}`);

  const localServer = net.createServer((deskflowClient) => {
    console.log('[proxy] local deskflow client connected, joining relay...');
    connectToRelay(
      'client',
      (relaySocket, initialBytes) => {
        pipe(relaySocket, deskflowClient, initialBytes);
      },
      (err) => {
        console.error(`[proxy] relay error: ${err.message}`);
        deskflowClient.destroy();
      }
    );
  });

  localServer.on('error', (err) => {
    console.error(`[proxy] local server error: ${err.message}`);
    process.exit(1);
  });

  localServer.listen(localPort, '127.0.0.1', () => {
    console.log(`[proxy] listening on 127.0.0.1:${localPort} for deskflow client`);
    console.log(`[proxy] configure deskflow client to connect to 127.0.0.1:${localPort}`);
  });
}
