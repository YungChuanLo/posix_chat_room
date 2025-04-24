# posix-chat-room

A simple multi-user chat room application written in C using POSIX sockets and pthreads.

## Repository Name
**posix-chat-room**

## Description
This project implements a concurrent chat room server and a command-line client in C. It supports multiple connected users, broadcast messaging, private messaging, user listing, delayed messages, and graceful shutdown.

## Features
- **Broadcast Messaging**: Send messages to all connected users.
- **Private Messaging**: Use `/pm <user> <message>` to send a message directly to a specific user.
- **User Listing**: Use `/list` to see all connected users.
- **Delayed Messaging**: Use `/delay <seconds> <user> <message>` to schedule a private message.
- **Graceful Shutdown**: Clients can send `/quit` or use Ctrl+C to disconnect cleanly.

## Requirements
- GCC (or compatible C compiler)
- POSIX-compliant system (Linux, macOS)
- pthreads library

## Building
```bash
# Compile server
gcc -pthread -o server server.c

# Compile client
gcc -pthread -o client client.c
```

## Usage
1. **Start the server** on a chosen port (e.g., 12345):
   ```bash
   ./server 12345
   ```
2. **Connect clients** by providing a username, server host, and port:
   ```bash
   ./client Alice 127.0.0.1 12345
   ```

## Client Commands
- `/list` &mdash; List all connected users.
- `/pm <recipient> <message>` &mdash; Send a private message.
- `/delay <seconds> <recipient> <message>` &mdash; Schedule a private message.
- `/quit` or `/exit` &mdash; Disconnect from the server.

## Code Structure
- `server.c` &mdash; Chat server implementation (connection handling, messaging logic).
- `client.c` &mdash; Command-line client (input parsing, message sending).

## License
MIT License. See [LICENSE](LICENSE) for details.

## Acknowledgments
- Inspired by university networking assignments and POSIX tutorials.

