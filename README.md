# Collaborative Code Editor — Phase V Workspace

Phase V now includes:

- SQLite persistence
- Hierarchical files/folders
- Create, rename, move and delete files
- Create, rename, move and delete folders
- Duplicate-name prevention within a directory
- Persistent workspace tree
- Unique usernames per room
- Presence list
- Multi-file editor
- VS Code + Apple-inspired UI

## Backend

Ubuntu/Debian:

```bash
sudo apt update
sudo apt install build-essential cmake libboost-all-dev libsqlite3-dev
```

Build:

```bash
cd backend
rm -rf build
cmake -S . -B build
cmake --build build -j
./build/collab_server
```

The server uses `collab.db`.

If you already have a Phase V database containing the old flat `files` table, this version migrates those files into the new workspace tree automatically.

## Frontend

```bash
cd frontend
npm install
npm run dev
```

Open the Vite URL, normally:

```text
http://localhost:5173
```

Open two tabs and join the same room with different usernames.

## Phase V protocol

### Join

```json
{"type":"join_room","room_id":"demo","username":"alice"}
```

### Create file

```json
{"type":"create_file","parent_id":"","name":"main.cpp","kind":"file"}
```

### Create folder

```json
{"type":"create_file","parent_id":"","name":"src","kind":"folder"}
```

### Rename

```json
{"type":"rename_node","node_id":"node-2","name":"utils.cpp"}
```

### Move

```json
{"type":"move_node","node_id":"node-2","parent_id":"node-1"}
```

### Delete

```json
{"type":"delete_node","node_id":"node-2"}
```

### Open

```json
{"type":"open_file","file_id":"node-2"}
```

### Edit

```json
{"type":"edit","file_id":"node-2","base_version":3,"position":10,"delete_count":0,"insert_text":"hello"}
```

## Important design decisions

The server owns the workspace tree. The browser only renders it.

A workspace node has:

```text
id
parent_id
name
kind = file | folder
content
version
language
```

Names only need to be unique among siblings, so these are valid:

```text
src/main.cpp
tests/main.cpp
```

but this is rejected:

```text
src/main.cpp
src/main.cpp
```

`main.cpp` at the workspace root is protected from deletion in this educational build.

Usernames are unique within a room, case-insensitively. For example:

```text
Alice -> allowed
alice -> rejected while Alice is present
```

## Persistence test

1. Join a room.
2. Create `src`.
3. Create `src/utils.cpp`.
4. Edit it.
5. Rename it.
6. Stop the backend.
7. Start it again.
8. Rejoin the room.
9. The tree and file content should still exist.

## Next phase

Phase VI can add real authentication, roles/permissions, REST APIs, automated tests, Docker and deployment.
