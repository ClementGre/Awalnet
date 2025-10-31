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

!! when an error is returned from the server, it also sends the id of the previous call to help the client identify which call caused the error !!
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

### CHALLENGE
```mermaid
sequenceDiagram
Challenger->>Server: CHALLENGE (target user id)
Server->>Challenger: continue business as usual 
Server->>Challenged: CHALLENGE notification (from Challenger)
Challenged->>Server: CHALLENGE_REQUEST_ANSWER notification (ACCEPT or REJECT)
Server->>Challenger: CHALLENGE_REQUEST_ANSWER notification
```

### CONSULT_USER_PROFILE
```mermaid
sequenceDiagram
    Requester->>Server: CONSULT_USER_PROFILE (target id)
    Server->>Target: CONSULT_USER_PROFILE (requester id)
    Target->>Server: SENT_USER_PROFILE (requester id + serialized profile)
    Server->>Requester: RECEIVE_USER_PROFILE (serialized profile)
```
