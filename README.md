# OSC Server Plugin for OBS Studio

A powerful, high-performance bridge for bidirectional OSC (Open Sound Control) and WebSocket communication within OBS Studio.

## Features

- **Bidirectional OSC Support**: Send and receive OSC messages over UDP.
- **Embedded WebSocket Server**: Built-in high-performance Mongoose server for direct communication with Browser Sources.
- **Dynamic Routing**: Route incoming OSC messages from specific devices to selected Browser Sources by name or output port.
- **Auto-Discovery Handshake**: Automatically broadcasts server details (IP, Port, Connected Devices) to all active browser overlays on startup.
- **Persistent Configuration**: Save server settings, device lists, and UI states (like console collapse status) directly in OBS.
- **Rich Settings UI**: Modern Qt-based interface with real-time status indicators and a live log console.

## Tech Stack

- **Core**: C++17
- **Networking**: [Mongoose](https://github.com/cesanta/mongoose) (WebSockets/HTTP)
- **OSC Protocol**: [tinyosc](https://github.com/v923z/tinyosc) (UDP)
- **UI Framework**: Qt 6 (OBS standard)
- **Build System**: CMake

## Installation (Local Development)

To build and deploy the plugin locally on macOS:

1. Clone the repository.
2. Run the deployment script:
   ```bash
   ./deploy_macos.sh
   ```
3. Restart OBS Studio.
4. Open the settings via **Tools > OSC Server Settings**.

## Browser Overlay Integration

The plugin simplifies browser source communication using a dynamic handshake.

### 1. Connection Handshake
On OBS startup (or when clicking "Send OSC details"), the plugin emits a `osc_server_details` event to all browser sources:

```javascript
window.addEventListener('osc_server_details', (e) => {
    const { ip, port, clients } = e.detail;
    // Connect to ws://127.0.0.1:[port]/ws
});
```

### 2. Receiving OSC
Messages received via UDP are emitted to JavaScript via the `osc_message` event:

```javascript
window.addEventListener('osc_message', (e) => {
    const { client, address, args } = e.detail;
    console.log(`Received ${address} from ${client}`);
});
```

### 3. Sending OSC
Send JSON payloads to the WebSocket server to broadcast OSC to hardware devices:

```javascript
const payload = {
    address: "/filter/cutoff",
    format: "f",
    args: [{ value: 0.75 }],
    target: "MyDevice" // Optional: route to specific device or port
};
ws.send(JSON.stringify(payload));
```

## Settings Configuration

- **OSC Server (UDP)**: Set the listener IP and Port for incoming hardware messages.
- **Mongoose Server (TCP/WS)**: Configure the internal port for browser-to-plugin communication (default: 12347).
- **OSC Devices**: Add specific hardware devices with their IP and Output Port. You can specify which browser source should receive messages from which device.
- **Log Console**: A real-time monitor for all OSC and WebSocket traffic, with a collapsible UI for focus.

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
