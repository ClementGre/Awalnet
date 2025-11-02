# Awalnet

A simple C project with Makefile build system.

## Building

To build the project:
```bash
make
```

To clean build artifacts:
```bash
make clean
```

To build and run:
```bash
make && ./bin/awalnet
```

## Project Structure

- `src/` - Source files (.c and .h)
- `bin/` - Build output directory (excluded from git)
- `bin/obj/` - Object files (.o)
- `Makefile` - Build configuration

## Processes


### CONNECT
```mermaid
sequenceDiagram
    participant Client
    participant Server
    Client->>Server: CONNECT (username)
    Server->>Client: Serialized User (User struct)
    
 ``` 

### LIST_USERS
```mermaid
sequenceDiagram
participant Client
participant Server
Client->>Server: LIST_USERS
Server->>Client: List of connected users (usernames + ids)
```



### CONSULT_USER_PROFILE
```mermaid
sequenceDiagram
    Requester->>Server: CONSULT_USER_PROFILE (target id)
    Server->>Target: CONSULT_USER_PROFILE (requester id)
    Target->>Server: SENT_USER_PROFILE (requester id + serialized profile)
    Server->>Requester: RECEIVE_USER_PROFILE (serialized profile)
```

### CHALLENGE (when a user challenges another user)
```mermaid
sequenceDiagram
Challenger->>Server: CHALLENGE (target user id)
Server->>Challenger: continue business as usual 
Server->>Challenged: CHALLENGE notification (from Challenger)
Challenged->>Server: CHALLENGE_REQUEST_ANSWER notification (ACCEPT or REJECT)
Server->>Challenger: CHALLENGE_REQUEST_ANSWER notification
```

### GAME MODE - GAME LOOP
```mermaid
sequenceDiagram
    Player1->>Server: SEND_MOVE (move data)
    Server->>Player2: RECEIVE_MOVE (move data)
    Player2->>Server: SEND_MOVE (move data)
    Server->>Player1: RECEIVE_MOVE (move data)
```

## NOTES
!! when an error is returned from the server, it also sends the id of the previous call to help the client identify which call caused the error !!

The game logic is implemented on the client side to ease server load. It means that the server only receives the moves and sends them to the opponent without any validation because the move validation has been done by the player sending it.
Also, both player and client have a copy of the game board state so they can render it locally without exchanging it.
But ultimately, it is the server who stops the game when a player wins or when there is a draw or when a player disconnects.
