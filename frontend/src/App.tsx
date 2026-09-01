import { useEffect, useMemo, useRef, useState } from "react";
import Editor, { type OnMount } from "@monaco-editor/react";

const WS_URL = "ws://localhost:8080";

type Status = "connecting" | "connected" | "disconnected";

type Node = {
  id: string;
  parent_id: string;
  name: string;
  kind: "file" | "folder";
  language: string;
};

type Message = {
  type: string;
  client_id?: string;
  username?: string;
  room_id?: string;
  member_count?: number;
  file_id?: string;
  version?: number;
  content?: string;
  position?: number;
  delete_count?: number;
  insert_text?: string;
  nodes?: Node[];
  users?: string[];
  reason?: string;
  message?: string;
  action?: string;
  archive_base64?: string;
  owner?: boolean;
};

type ContextMenuState = {
  node: Node | null;
  x: number;
  y: number;
};

type DialogState =
  | { type: "create"; kind: "file" | "folder"; parentId: string }
  | { type: "rename"; node: Node }
  | { type: "delete"; node: Node }
  | { type: "delete-room" }
  | { type: "username" }
  | null;

const initialCode = `#include <iostream>

int main() {
    std::cout << "Hello, collaborative editor!\\n";
    return 0;
}
`;

const avatarPalette = [
  "avatar-a",
  "avatar-b",
  "avatar-c",
  "avatar-d",
  "avatar-e",
];

function avatarClass(name: string) {
  let hash = 0;
  for (const char of name) hash = (hash * 31 + char.charCodeAt(0)) | 0;
  return avatarPalette[Math.abs(hash) % avatarPalette.length];
}

function App() {
  const [status, setStatus] = useState<Status>("connecting");
  const [clientId, setClientId] = useState("—");
  const [username, setUsername] = useState(
    () => `user${Math.floor(Math.random() * 10000)}`
  );
  const [userKey] = useState(() => {
    const existing = localStorage.getItem("collab-editor-user-key");
    if (existing) return existing;
    const created = globalThis.crypto?.randomUUID?.() ?? `${Date.now()}-${Math.random()}`;
    localStorage.setItem("collab-editor-user-key", created);
    return created;
  });
  const [roomInput, setRoomInput] = useState("demo");
  const [currentRoom, setCurrentRoom] = useState("—");
  const [memberCount, setMemberCount] = useState(0);
  const [isOwner, setIsOwner] = useState(false);
  const [users, setUsers] = useState<string[]>([]);
  const [nodes, setNodes] = useState<Node[]>([]);
  const [activeFile, setActiveFile] = useState("");
  const [openFiles, setOpenFiles] = useState<string[]>([]);
  const [version, setVersion] = useState(0);
  const [events, setEvents] = useState<string[]>([]);
  const [expanded, setExpanded] = useState<Set<string>>(() => new Set());

  const [contextMenu, setContextMenu] =
    useState<ContextMenuState | null>(null);
  const [moveMenuOpen, setMoveMenuOpen] = useState(false);
  const [dialog, setDialog] = useState<DialogState>(null);
  const [dialogValue, setDialogValue] = useState("");
  const [commandOpen, setCommandOpen] = useState(false);
  const [commandQuery, setCommandQuery] = useState("");
  const [userMenuOpen, setUserMenuOpen] = useState(false);
  const [roomMenuOpen, setRoomMenuOpen] = useState(false);
  const [explorerOpen, setExplorerOpen] = useState(true);
  const [inspectorOpen, setInspectorOpen] = useState(true);
  const [activityOpen, setActivityOpen] = useState(false);

  const socketRef = useRef<WebSocket | null>(null);
  const editorRef = useRef<Parameters<OnMount>[0] | null>(null);
  const currentRoomRef = useRef("—");
  const versionRef = useRef(0);
  const clientIdRef = useRef("—");
  const activeFileRef = useRef("");
  const openFilesRef = useRef<string[]>([]);
  const applyingRemote = useRef(false);
  const nodesRef = useRef<Node[]>([]);
  const mountedRef = useRef(true);
  const reconnectRef = useRef<number | undefined>();
  const autoOpenFirstFileRef = useRef(false);

  useEffect(() => {
    openFilesRef.current = openFiles;
  }, [openFiles]);

  const addEvent = (text: string) => {
    if (!mountedRef.current) return;
    setEvents((old) => [...old.slice(-49), text]);
  };

  const send = (payload: object) => {
    if (socketRef.current?.readyState === WebSocket.OPEN) {
      socketRef.current.send(JSON.stringify(payload));
    }
  };

  const activeNode = nodes.find((node) => node.id === activeFile);
  const fileLanguage = activeNode?.language || "plaintext";

  const tree = useMemo(() => {
    const children = new Map<string, Node[]>();
    for (const node of nodes) {
      const list = children.get(node.parent_id) ?? [];
      list.push(node);
      children.set(node.parent_id, list);
    }

    for (const list of children.values()) {
      list.sort((a, b) => {
        if (a.kind !== b.kind) return a.kind === "folder" ? -1 : 1;
        return a.name.localeCompare(b.name);
      });
    }
    return children;
  }, [nodes]);

  const applyRemoteEdit = (
    position: number,
    deleteCount: number,
    insertText: string
  ) => {
    const editor = editorRef.current;
    const model = editor?.getModel();
    if (!model) return;

    const text = model.getValue();
    if (position > text.length) return;

    const end = Math.min(position + deleteCount, text.length);
    applyingRemote.current = true;
    model.setValue(
      text.slice(0, position) + insertText + text.slice(end)
    );
    applyingRemote.current = false;
  };

  const openFile = (fileId: string) => {
    if (!fileId) return;
    activeFileRef.current = fileId;
    setActiveFile(fileId);
    setOpenFiles((old) => old.includes(fileId) ? old : [...old, fileId]);
    versionRef.current = 0;
    setVersion(0);
    send({ type: "open_file", file_id: fileId });
  };

  const closeFile = (fileId: string) => {
    const oldFiles = openFilesRef.current;
    const index = oldFiles.indexOf(fileId);
    const next = oldFiles.filter((id) => id !== fileId);
    const wasActive = activeFileRef.current === fileId;

    openFilesRef.current = next;
    setOpenFiles(next);

    if (!wasActive) return;

    const nextId = next[index] ?? next[index - 1] ?? "";

    activeFileRef.current = nextId;
    setActiveFile(nextId);
    versionRef.current = 0;
    setVersion(0);

    if (nextId) {
      send({ type: "open_file", file_id: nextId });
    } else {
      applyingRemote.current = true;
      editorRef.current?.setModel(null);
      applyingRemote.current = false;
    }
  };

  const handleMessage = (raw: string) => {
    let data: Message;
    try {
      data = JSON.parse(raw);
    } catch {
      addEvent(raw);
      return;
    }

    switch (data.type) {
      case "welcome": {
        const id = data.client_id ?? "—";
        clientIdRef.current = id;
        setClientId(id);
        addEvent(`Connected as ${id}`);
        break;
      }

      case "room_joined": {
        const room = data.room_id ?? "—";
        currentRoomRef.current = room;
        setCurrentRoom(room);
        setMemberCount(data.member_count ?? 0);
        setIsOwner(Boolean(data.owner));
        autoOpenFirstFileRef.current = true;
        addEvent(`Joined ${room} as ${data.username}`);
        break;
      }

      case "join_rejected":
        addEvent(`Join rejected: ${data.message}`);
        setRoomMenuOpen(true);
        break;

      case "username_changed":
        if (data.username) {
          setUsername(data.username);
          setDialog(null);
          addEvent(`Username changed to ${data.username}`);
        }
        break;

      case "username_rejected":
        addEvent(`Username rejected: ${data.message}`);
        break;

      case "presence":
        setUsers(data.users ?? []);
        setMemberCount((data.users ?? []).length);
        break;

      case "user_renamed":
        addEvent(`${data.username ?? "User"} changed their username`);
        break;

      case "user_joined":
        setMemberCount(data.member_count ?? 0);
        if (data.username) {
          setUsers((old) =>
            old.includes(data.username!) ? old : [...old, data.username!]
          );
        }
        addEvent(`${data.username} joined`);
        break;

      case "user_left":
        setMemberCount(data.member_count ?? 0);
        if (data.username) {
          setUsers((old) => old.filter((user) => user !== data.username));
        }
        addEvent(`${data.username} left`);
        break;

      case "workspace_state": {
        if (currentRoomRef.current === "—") break;
        const next = data.nodes ?? [];
        nodesRef.current = next;
        setNodes(next);

        const fileIds = new Set(
          next.filter((node) => node.kind === "file").map((node) => node.id)
        );
        setOpenFiles((old) => old.filter((id) => fileIds.has(id)));

        const activeStillExists = activeFileRef.current && fileIds.has(activeFileRef.current);

        if (!activeStillExists && activeFileRef.current) {
          const remaining = openFilesRef.current.filter((id) => fileIds.has(id) && id !== activeFileRef.current);
          const nextId = remaining[0] ?? "";
          activeFileRef.current = nextId;
          setActiveFile(nextId);
          versionRef.current = 0;
          setVersion(0);
          if (nextId) send({ type: "open_file", file_id: nextId });
        }

        if (autoOpenFirstFileRef.current && !activeFileRef.current) {
          const first = next.find((node) => node.kind === "file");
          if (first) {
            autoOpenFirstFileRef.current = false;
            openFile(first.id);
          }
        }
        break;
      }

      case "document_state": {
        if (currentRoomRef.current === "—") break;
        if (
          data.file_id !== activeFileRef.current ||
          typeof data.content !== "string"
        ) {
          break;
        }

        const model = editorRef.current?.getModel();
        if (model && model.getValue() !== data.content) {
          applyingRemote.current = true;
          model.setValue(data.content);
          applyingRemote.current = false;
        }

        const v = data.version ?? 0;
        versionRef.current = v;
        setVersion(v);
        break;
      }

      case "edit": {
        if (
          data.file_id !== activeFileRef.current ||
          data.position === undefined ||
          data.delete_count === undefined ||
          data.insert_text === undefined
        ) {
          break;
        }

        const v = data.version ?? versionRef.current;
        if (data.client_id !== clientIdRef.current) {
          applyRemoteEdit(data.position, data.delete_count, data.insert_text);
        }

        versionRef.current = v;
        setVersion(v);
        addEvent(
          `${data.username ?? data.client_id} edited ${
            activeNode?.name ?? "file"
          } → v${v}`
        );
        break;
      }

      case "edit_rejected": {
        if (
          data.file_id !== activeFileRef.current ||
          typeof data.content !== "string"
        ) {
          break;
        }

        const model = editorRef.current?.getModel();
        if (model) {
          applyingRemote.current = true;
          model.setValue(data.content);
          applyingRemote.current = false;
        }

        const v = data.version ?? 0;
        versionRef.current = v;
        setVersion(v);
        addEvent(`Resynced document → v${v}`);
        break;
      }

      case "workspace_changed":
        addEvent(`Workspace ${data.action ?? "changed"}`);
        break;

      case "room_deleted":
        currentRoomRef.current = "—";
        setCurrentRoom("—");
        setMemberCount(0);
        setUsers([]);
        nodesRef.current = [];
        setNodes([]);
        activeFileRef.current = "";
        setActiveFile("");
        setOpenFiles([]);
        versionRef.current = 0;
        setVersion(0);
        autoOpenFirstFileRef.current = false;
        applyingRemote.current = true;
        editorRef.current?.getModel()?.setValue("");
        applyingRemote.current = false;
        setRoomMenuOpen(false);
        addEvent("Room deleted");
        break;

      case "room_archive": {
        if (!data.archive_base64) break;
        const binary = atob(data.archive_base64);
        const bytes = new Uint8Array(binary.length);
        for (let i = 0; i < binary.length; i++) bytes[i] = binary.charCodeAt(i);
        const blob = new Blob([bytes], { type: "application/zip" });
        const url = URL.createObjectURL(blob);
        const anchor = document.createElement("a");
        anchor.href = url;
        anchor.download = `${(data.room_id ?? (currentRoomRef.current === "—" ? "room" : currentRoomRef.current))}.zip`;
        document.body.appendChild(anchor);
        anchor.click();
        anchor.remove();
        URL.revokeObjectURL(url);
        addEvent("Room downloaded as ZIP");
        break;
      }

      case "error":
        addEvent(`Error: ${data.message}`);
        break;

      default:
        break;
    }
  };

  const connect = () => {
    setStatus("connecting");
    const socket = new WebSocket(WS_URL);
    socketRef.current = socket;

    socket.onopen = () => {
      setStatus("connected");
      addEvent("WebSocket connected");
    };

    socket.onmessage = (event) => handleMessage(String(event.data));

    socket.onclose = () => {
      setStatus("disconnected");
      socketRef.current = null;
      currentRoomRef.current = "—";
      setCurrentRoom("—");
      setMemberCount(0);
      setIsOwner(false);
      setUsers([]);
      nodesRef.current = [];
      setNodes([]);
      activeFileRef.current = "";
      setActiveFile("");
      setOpenFiles([]);
      versionRef.current = 0;
      setVersion(0);
      autoOpenFirstFileRef.current = false;
      applyingRemote.current = true;
      editorRef.current?.getModel()?.setValue("");
      applyingRemote.current = false;

      if (mountedRef.current) {
        reconnectRef.current = window.setTimeout(connect, 1500);
      }
    };

    socket.onerror = () => setStatus("disconnected");
  };

  useEffect(() => {
    mountedRef.current = true;
    connect();

    return () => {
      mountedRef.current = false;
      if (reconnectRef.current !== undefined) clearTimeout(reconnectRef.current);
      socketRef.current?.close();
    };
  }, []);

  useEffect(() => {
    const closeMenus = () => {
      setContextMenu(null);
      setMoveMenuOpen(false);
      setUserMenuOpen(false);
      setRoomMenuOpen(false);
    };

    const onKeyDown = (event: KeyboardEvent) => {
      const modifier = event.metaKey || event.ctrlKey;

      if (modifier && event.key.toLowerCase() === "k") {
        event.preventDefault();
        setCommandOpen(true);
        setCommandQuery("");
        closeMenus();
        return;
      }

      if (event.key === "Escape") {
        event.preventDefault();
        setCommandOpen(false);
        setDialog(null);
        setContextMenu(null);
        setMoveMenuOpen(false);
        setUserMenuOpen(false);
        setRoomMenuOpen(false);
        return;
      }

      if (event.key === "F2" && activeNode) {
        event.preventDefault();
        beginRename(activeNode);
      }

      if (event.key === "Delete" && activeNode && !dialog) {
        const target = document.activeElement as HTMLElement | null;
        if (target?.tagName === "TEXTAREA" || target?.tagName === "INPUT") return;
        event.preventDefault();
        beginDelete(activeNode);
      }
    };

    window.addEventListener("keydown", onKeyDown);
    window.addEventListener("click", closeMenus);
    return () => {
      window.removeEventListener("keydown", onKeyDown);
      window.removeEventListener("click", closeMenus);
    };
  }, [activeNode, dialog]);

  const joinRoom = () => {
    const room = roomInput.trim();
    const name = username.trim();
    if (!room || !name) return;

    send({ type: "join_room", room_id: room, username: name, user_key: userKey });
  };

  const leaveRoom = () => {
    send({ type: "leave_room" });
    currentRoomRef.current = "—";
    setCurrentRoom("—");
    setMemberCount(0);
    setIsOwner(false);
    setUsers([]);
    nodesRef.current = [];
    setNodes([]);
    activeFileRef.current = "";
    setActiveFile("");
    setOpenFiles([]);
    versionRef.current = 0;
    setVersion(0);
    autoOpenFirstFileRef.current = false;
    applyingRemote.current = true;
    editorRef.current?.getModel()?.setValue("");
    applyingRemote.current = false;
    setRoomMenuOpen(false);
    setActivityOpen(false);
  };

  const beginUsernameChange = () => {
    setDialog({ type: "username" });
    setDialogValue(username);
    setUserMenuOpen(false);
  };

  const beginCreate = (kind: "file" | "folder", parentId = "") => {
    setDialog({ type: "create", kind, parentId });
    setDialogValue(kind === "folder" ? "new-folder" : "new.cpp");
    setContextMenu(null);
    setMoveMenuOpen(false);
  };

  const beginRename = (node: Node) => {
    setDialog({ type: "rename", node });
    setDialogValue(node.name);
    setContextMenu(null);
    setMoveMenuOpen(false);
  };

  const beginDelete = (node: Node) => {
    setDialog({ type: "delete", node });
    setContextMenu(null);
    setMoveMenuOpen(false);
  };

  const downloadRoom = () => {
    if (currentRoomRef.current === "—") return;
    send({ type: "download_room" });
    setRoomMenuOpen(false);
  };

  const beginDeleteRoom = () => {
    setDialog({ type: "delete-room" });
    setRoomMenuOpen(false);
  };

  const submitDialog = () => {
    const value = dialogValue.trim();

    if (dialog?.type === "create") {
      if (!value) return;
      send({
        type: dialog.kind === "folder" ? "create_folder" : "create_file",
        parent_id: dialog.parentId,
        name: value,
        kind: dialog.kind,
      });
      setDialog(null);
      return;
    }

    if (dialog?.type === "rename") {
      if (!value) return;
      send({
        type: "rename_node",
        node_id: dialog.node.id,
        name: value,
      });
      setDialog(null);
      return;
    }

    if (dialog?.type === "username") {
      if (!value) return;
      if (value === username) {
        setDialog(null);
        return;
      }

      if (currentRoomRef.current === "—") {
        setUsername(value);
        setDialog(null);
      } else {
        send({ type: "change_username", username: value });
      }
      return;
    }

    if (dialog?.type === "delete-room") {
      send({ type: "delete_room" });
      setDialog(null);
      return;
    }

    if (dialog?.type === "delete") {
      send({ type: "delete_node", node_id: dialog.node.id });
      setDialog(null);
    }
  };

  const isDescendant = (nodeId: string, possibleAncestorId: string) => {
    let current = nodes.find((node) => node.id === nodeId);
    while (current && current.parent_id) {
      if (current.parent_id === possibleAncestorId) return true;
      current = nodes.find((node) => node.id === current?.parent_id);
    }
    return false;
  };

  const moveNode = (node: Node, parentId: string) => {
    send({ type: "move_node", node_id: node.id, parent_id: parentId });
    setContextMenu(null);
    setMoveMenuOpen(false);
  };

  const moveTargets = (node: Node) => {
    const folders = nodes
      .filter(
        (candidate) =>
          candidate.kind === "folder" &&
          candidate.id !== node.id &&
          !isDescendant(candidate.id, node.id)
      )
      .sort((a, b) => a.name.localeCompare(b.name));

    return [
      { id: "", label: "Workspace", depth: 0 },
      ...folders.map((folder) => {
        let depth = 0;
        let current = folder;
        while (current.parent_id) {
          depth += 1;
          const parent = nodes.find((n) => n.id === current.parent_id);
          if (!parent) break;
          current = parent;
        }
        return { id: folder.id, label: folder.name, depth };
      }),
    ];
  };

  const openContextMenu = (
    event: React.MouseEvent,
    node: Node | null
  ) => {
    event.preventDefault();
    event.stopPropagation();

    const menuWidth = 228;
    const menuHeight = node ? 245 : 105;
    const x = Math.min(event.clientX, window.innerWidth - menuWidth - 12);
    const y = Math.min(event.clientY, window.innerHeight - menuHeight - 12);

    setMoveMenuOpen(false);
    setContextMenu({ node, x: Math.max(8, x), y: Math.max(8, y) });
    setUserMenuOpen(false);
    setRoomMenuOpen(false);
  };

  const toggleFolder = (id: string) => {
    setExpanded((old) => {
      const next = new Set(old);
      if (next.has(id)) next.delete(id);
      else next.add(id);
      return next;
    });
  };

  const renderTree = (parentId: string, depth = 0): React.ReactNode => {
    const children = tree.get(parentId) ?? [];

    return children.map((node) => {
      const isFolder = node.kind === "folder";
      const isSelected = node.id === activeFile;

      return (
        <div key={node.id}>
          <div
            className={`tree-row ${isSelected ? "selected" : ""}`}
            style={{ paddingLeft: 8 + depth * 16 }}
            onContextMenu={(event) => openContextMenu(event, node)}
          >
            {isFolder ? (
              <button
                className="chevron"
                onClick={() => toggleFolder(node.id)}
                aria-label={expanded.has(node.id) ? "Collapse folder" : "Expand folder"}
              >
                <span className={`chevron-icon ${expanded.has(node.id) ? "open" : ""}`}>
                  ›
                </span>
              </button>
            ) : (
              <span className={`file-icon ${node.language || "plain"}`}>
                {node.language === "cpp" ? "C" : "•"}
              </span>
            )}

            <button
              className="node-button"
              onClick={() =>
                isFolder ? toggleFolder(node.id) : openFile(node.id)
              }
              onDoubleClick={() => beginRename(node)}
            >
              <span className="node-name">{node.name}</span>
            </button>

            <button
              className="node-more"
              aria-label={`Actions for ${node.name}`}
              onClick={(event) => openContextMenu(event, node)}
            >
              ···
            </button>
          </div>

          {isFolder &&
            expanded.has(node.id) &&
            renderTree(node.id, depth + 1)}
        </div>
      );
    });
  };

  const handleEditorMount: OnMount = (editor) => {
    editorRef.current = editor;
    const model = editor.getModel();

    if (model && model.getValue() === "") model.setValue(initialCode);
    editor.focus();

    editor.onDidChangeModelContent((event) => {
      if (
        applyingRemote.current ||
        currentRoomRef.current === "—" ||
        !activeFileRef.current
      ) {
        return;
      }

      const currentModel = editor.getModel();
      if (!currentModel) return;

      for (const change of event.changes) {
        const position = currentModel.getOffsetAt({
          lineNumber: change.range.startLineNumber,
          column: change.range.startColumn,
        });

        send({
          type: "edit",
          file_id: activeFileRef.current,
          base_version: versionRef.current,
          position,
          delete_count: change.rangeLength,
          insert_text: change.text,
        });
      }
    });
  };

  const commands = [
    { label: "New File", shortcut: "⌘ N", run: () => beginCreate("file") },
    { label: "New Folder", shortcut: "⇧⌘ N", run: () => beginCreate("folder") },
    { label: "Rename", shortcut: "F2", run: () => activeNode && beginRename(activeNode) },
    { label: "Delete", shortcut: "⌫", run: () => activeNode && beginDelete(activeNode) },
    { label: "Toggle Explorer", shortcut: "⌘ B", run: () => setExplorerOpen((v) => !v) },
    { label: "Toggle Collaborators", shortcut: "⌘ J", run: () => setInspectorOpen((v) => !v) },
    { label: "Activity", shortcut: "", run: () => setActivityOpen((v) => !v) },
  ];

  const filteredCommands = commands.filter((command) =>
    command.label.toLowerCase().includes(commandQuery.toLowerCase())
  );

  return (
    <div className="app" onContextMenu={(event) => {
      if ((event.target as HTMLElement).closest(".editor-surface")) return;
      if (!(event.target as HTMLElement).closest(".tree")) event.preventDefault();
    }}>
      <div className="ambient ambient-one" />
      <div className="ambient ambient-two" />

      <header className="topbar glass">
        <div className="topbar-left">
          <div className="traffic" aria-hidden="true">
            <span />
            <span />
            <span />
          </div>

          <button className="brand-button" onClick={() => setExplorerOpen((v) => !v)}>
            <span className="brand-mark">⌘</span>
            <span>
              <strong>Collab Editor</strong>
              <small>Persistent workspace</small>
            </span>
          </button>
        </div>

        <div className="topbar-center">
          <button
            className="room-pill"
            onClick={(event) => {
              event.stopPropagation();
              setRoomMenuOpen((v) => !v);
              setUserMenuOpen(false);
            }}
          >
            <span className={`status-dot ${status}`} />
            <span>{currentRoom === "—" ? "No room" : currentRoom}</span>
            <span className="pill-count">{memberCount || 0}</span>
            <span className="pill-chevron">⌄</span>
          </button>
        </div>

        <div className="topbar-right">
          <button
            className="toolbar-icon"
            title="Command palette"
            onClick={(event) => {
              event.stopPropagation();
              setCommandOpen(true);
              setCommandQuery("");
            }}
          >
            ⌘K
          </button>

          <button
            className="toolbar-icon"
            title="Toggle activity"
            onClick={(event) => {
              event.stopPropagation();
              setActivityOpen((v) => !v);
            }}
          >
            ◌
          </button>

          <button
            className="user-pill"
            onClick={(event) => {
              event.stopPropagation();
              setUserMenuOpen((v) => !v);
              setRoomMenuOpen(false);
            }}
          >
            <span className={`avatar ${avatarClass(username)}`}>
              {username.slice(0, 1).toUpperCase()}
            </span>
            <span className="user-pill-name">{username}</span>
            <span>⌄</span>
          </button>
        </div>

        {roomMenuOpen && (
          <div className="top-popover room-popover" onClick={(event) => event.stopPropagation()}>
            <div className="popover-heading">ROOM</div>
            <div className="popover-room-name">{currentRoom === "—" ? "Join a room" : currentRoom}</div>
            <div className="popover-muted">{currentRoom === "—" ? "Connect with your collaborators" : `${memberCount} collaborator${memberCount === 1 ? "" : "s"}`}</div>

            <div className="popover-form">
              <label>Room name</label>
              <input
                value={roomInput}
                onChange={(event) => setRoomInput(event.target.value)}
                onKeyDown={(event) => {
                  if (event.key === "Enter") joinRoom();
                }}
                placeholder="demo"
                spellCheck={false}
              />
              <label>Your name</label>
              <input
                value={username}
                onChange={(event) => setUsername(event.target.value)}
                placeholder="Your name"
                spellCheck={false}
              />
              <button
                className="join-room-button"
                disabled={status !== "connected" || !roomInput.trim() || !username.trim()}
                onClick={() => { joinRoom(); setRoomMenuOpen(false); }}
              >
                {currentRoom === "—" ? "Join room" : "Switch room"}
              </button>
            </div>

            <div className="popover-divider" />
            <button onClick={() => navigator.clipboard?.writeText(currentRoom !== "—" ? currentRoom : roomInput)}>Copy room name</button>
            {currentRoom !== "—" && (
              <>
                <button onClick={downloadRoom}>Download workspace (.zip)</button>
                {isOwner ? (
                  <button className="danger-action" onClick={beginDeleteRoom}>Delete room</button>
                ) : (
                  <div className="popover-note">Only the room owner can delete this room.</div>
                )}
                <button onClick={() => { leaveRoom(); setRoomMenuOpen(false); }}>Leave room</button>
              </>
            )}
          </div>
        )}

        {userMenuOpen && (
          <div className="top-popover user-popover" onClick={(event) => event.stopPropagation()}>
            <div className="profile-head">
              <span className={`avatar large ${avatarClass(username)}`}>{username.slice(0, 1).toUpperCase()}</span>
              <div>
                <strong>{username}</strong>
                <span><i className="online-dot" /> {status}</span>
              </div>
            </div>
            <div className="popover-divider" />
            <button onClick={beginUsernameChange}>Change username</button>
            <button onClick={() => { setUserMenuOpen(false); setCommandOpen(true); }}>Keyboard shortcuts</button>
          </div>
        )}
      </header>

      <main className={`workspace ${explorerOpen ? "" : "explorer-hidden"} ${inspectorOpen ? "" : "inspector-hidden"}`}>
        {explorerOpen && (
          <aside className="explorer glass-panel">
            <div className="panel-header">
              <div>
                <span className="eyebrow">WORKSPACE</span>
                <strong>{currentRoom === "—" ? "Local" : currentRoom}</strong>
              </div>
              <div className="panel-actions">
                <button title="New file" onClick={() => beginCreate("file")}>＋</button>
                <button title="New folder" onClick={() => beginCreate("folder")}>▱</button>
                <button title="Hide Explorer" onClick={() => setExplorerOpen(false)}>‹</button>
              </div>
            </div>

            <div className="tree"
              onContextMenu={(event) => {
                if (event.target === event.currentTarget) openContextMenu(event, null);
              }}
            >
              {nodes.length === 0 ? (
                <div className="empty-tree">
                  <div className="empty-icon">⌘</div>
                  <strong>No workspace loaded</strong>
                  <span>Join a room to see its files.</span>
                </div>
              ) : (
                <>
                  <div className="tree-root-label">FILES</div>
                  {renderTree("")}
                </>
              )}
            </div>

            <div className="explorer-bottom">
              <button className="new-button" onClick={() => beginCreate("file")}>
                <span>＋</span> New
              </button>
              <button className="panel-toggle" onClick={() => setExplorerOpen(false)} title="Hide Explorer">⌘B</button>
            </div>
          </aside>
        )}

        <section className="editor-panel editor-surface">
          {!explorerOpen && (
            <button className="reopen-panel explorer-reopen" onClick={() => setExplorerOpen(true)}>›</button>
          )}

          <div className="editor-toolbar">
            <div className="breadcrumbs">
              <span>{currentRoom === "—" ? "Workspace" : currentRoom}</span>
              <span className="crumb-separator">/</span>
              <span>{activeNode?.name ?? "No file selected"}</span>
            </div>
            <div className="editor-toolbar-right">
              {activeNode && <span className="save-state"><i /> Saved</span>}
              <button onClick={() => setActivityOpen((v) => !v)} title="Activity">⌁</button>
            </div>
          </div>

          <div className="tabs">
            {openFiles.length > 0 ? openFiles.map((fileId) => {
              const node = nodes.find((item) => item.id === fileId && item.kind === "file");
              if (!node) return null;
              return (
                <div key={fileId} className={`tab ${fileId === activeFile ? "active" : ""}`}>
                  <button className="tab-main" onClick={() => openFile(fileId)}>
                    <span className={`file-icon ${node.language}`}>{node.language === "cpp" ? "C" : "•"}</span>
                    <span>{node.name}</span>
                  </button>
                  <button className="tab-close" onClick={() => closeFile(fileId)} aria-label={`Close ${node.name}`}>×</button>
                </div>
              );
            }) : (
              <div className="tab-placeholder">No file open</div>
            )}
          </div>

          {activeNode ? (
            <Editor
              key={activeFile || "empty-editor"}
              height="calc(100vh - 120px)"
              language={fileLanguage}
              defaultLanguage="cpp"
              defaultValue={initialCode}
              theme="vs-dark"
              onMount={handleEditorMount}
              options={{
                minimap: { enabled: false },
                fontSize: 14,
                lineHeight: 22,
                padding: { top: 22, bottom: 28 },
                smoothScrolling: true,
                automaticLayout: true,
                roundedSelection: true,
                scrollBeyondLastLine: false,
                cursorSmoothCaretAnimation: "on",
                renderLineHighlight: "line",
                bracketPairColorization: { enabled: true },
                guides: { indentation: true, bracketPairs: true },
                scrollbar: { verticalScrollbarSize: 9, horizontalScrollbarSize: 9 },
                fontLigatures: true,
              }}
            />
          ) : (
            <div className="empty-editor-state">
              <div className="empty-editor-icon">⌘</div>
              <strong>No file open</strong>
              <span>Select a file from the Explorer to start editing.</span>
            </div>
          )}

          <div className="editor-status">
            <span>{fileLanguage}</span>
            <span>Spaces: 4</span>
            <span>UTF-8</span>
            <span>LF</span>
            <span>{activeNode ? `v${version}` : ""}</span>
          </div>
        </section>

        {inspectorOpen && (
          <aside className="inspector glass-panel">
            <div className="inspector-top">
              <div>
                <span className="eyebrow">COLLABORATION</span>
                <strong>{memberCount} online</strong>
              </div>
              <button onClick={() => setInspectorOpen(false)} title="Hide collaborators">›</button>
            </div>

            <div className="people-card">
              <div className="people-stack">
                {users.slice(0, 5).map((user) => (
                  <span key={user} className={`avatar stacked ${avatarClass(user)}`} title={user}>
                    {user.slice(0, 1).toUpperCase()}
                  </span>
                ))}
              </div>
              <span className="people-caption">
                {memberCount === 0 ? "No one online" : `${memberCount} collaborator${memberCount === 1 ? "" : "s"}`}
              </span>
            </div>

            <div className="inspector-section collaborators-section">
              <div className="section-title-row">
                <span className="eyebrow">PEOPLE</span>
                <span className="live-label"><i /> LIVE</span>
              </div>

              <div className="people">
                {users.map((user) => (
                  <div className="person" key={user}>
                    <span className={`avatar small ${avatarClass(user)}`}>{user.slice(0, 1).toUpperCase()}</span>
                    <div className="person-info">
                      <strong>{user}</strong>
                      <span>{user === username ? "You" : "In this room"}</span>
                    </div>
                    <span className="presence" />
                  </div>
                ))}
              </div>
            </div>

            <div className="inspector-section document-section">
              <span className="eyebrow">CURRENT FILE</span>
              <div className="document-card">
                <div className="document-file">
                  <span className={`file-icon big ${fileLanguage}`}>C</span>
                  <div>
                    <strong>{activeNode?.name ?? "No file selected"}</strong>
                    <span>{activeNode ? `${fileLanguage} · v${version}` : "Open a file from Explorer"}</span>
                  </div>
                </div>
              </div>
            </div>

            <div className="inspector-section room-details">
              <span className="eyebrow">ROOM</span>
              <div className="room-detail-line"><span>Name</span><strong>{currentRoom}</strong></div>
              <div className="room-detail-line"><span>Connection</span><strong><i className={`status-dot ${status}`} /> {status}</strong></div>
            </div>

            {activityOpen && (
              <div className="activity-drawer">
                <div className="section-title-row">
                  <span className="eyebrow">ACTIVITY</span>
                  <button onClick={() => setActivityOpen(false)}>×</button>
                </div>
                <div className="events">
                  {events.length === 0 ? (
                    <div className="empty-events">Nothing yet</div>
                  ) : events.slice().reverse().map((event, index) => (
                    <div className="event" key={`${index}-${event}`}>{event}</div>
                  ))}
                </div>
              </div>
            )}
          </aside>
        )}

        {!inspectorOpen && (
          <button className="reopen-panel inspector-reopen" onClick={() => setInspectorOpen(true)}>‹</button>
        )}
      </main>

      <footer className="statusbar glass">
        <div className="status-left">
          <span className={`status-dot ${status}`} />
          <span>{currentRoom === "—" ? "Ready" : `Connected to ${currentRoom}`}</span>
        </div>
        <div className="status-right">
          <span>{activeNode?.language ?? "Plain Text"}</span>
          <span>{activeNode ? `v${version}` : ""}</span>
          <span>{clientId}</span>
        </div>
      </footer>

      {contextMenu && (
        <div
          className="context-menu glass"
          style={{ left: contextMenu.x, top: contextMenu.y }}
          onClick={(event) => event.stopPropagation()}
          onMouseLeave={() => setMoveMenuOpen(false)}
          onContextMenu={(event) => event.preventDefault()}
        >
          {contextMenu.node ? (
            <>
              {contextMenu.node.kind === "folder" && (
                <>
                  <button className="context-item" onClick={() => beginCreate("file", contextMenu.node!.id)}>
                    <span className="context-icon">＋</span><span>New File</span><span className="context-shortcut">⌘N</span>
                  </button>
                  <button className="context-item" onClick={() => beginCreate("folder", contextMenu.node!.id)}>
                    <span className="context-icon">▱</span><span>New Folder</span>
                  </button>
                  <div className="context-separator" />
                </>
              )}

              {contextMenu.node.kind === "file" && (
                <button className="context-item" onClick={() => { openFile(contextMenu.node!.id); setContextMenu(null); }}>
                  <span className="context-icon">↗</span><span>Open</span>
                </button>
              )}

              <button className="context-item" onClick={() => beginRename(contextMenu.node!)}>
                <span className="context-icon">✎</span><span>Rename</span><span className="context-shortcut">F2</span>
              </button>

              <div className="context-item submenu-trigger" onMouseEnter={() => setMoveMenuOpen(true)}>
                <span className="context-icon">↪</span><span>Move to</span><span className="context-chevron">›</span>
                {moveMenuOpen && (
                  <div className="context-submenu">
                    {moveTargets(contextMenu.node).map((target) => (
                      <button
                        key={target.id || "root"}
                        className="context-item"
                        style={{ paddingLeft: 10 + target.depth * 13 }}
                        onClick={() => moveNode(contextMenu.node!, target.id)}
                      >
                        <span className="context-icon">{target.id ? "▱" : "⌂"}</span>
                        <span>{target.label}</span>
                      </button>
                    ))}
                  </div>
                )}
              </div>

              <div className="context-separator" />

              <button className="context-item danger" onClick={() => beginDelete(contextMenu.node!)}>
                <span className="context-icon">⌫</span><span>Delete</span><span className="context-shortcut">⌫</span>
              </button>
            </>
          ) : (
            <>
              <button className="context-item" onClick={() => beginCreate("file")}>
                <span className="context-icon">＋</span><span>New File</span>
              </button>
              <button className="context-item" onClick={() => beginCreate("folder")}>
                <span className="context-icon">▱</span><span>New Folder</span>
              </button>
            </>
          )}
        </div>
      )}

      {dialog && (
        <div className="modal-backdrop" onMouseDown={() => setDialog(null)}>
          <div className="modal glass" onMouseDown={(event) => event.stopPropagation()}>
            {dialog.type === "delete-room" ? (
              <>
                <div className="modal-icon danger-icon">⌫</div>
                <div className="modal-title">Delete room?</div>
                <div className="modal-copy">
                  This permanently deletes “{currentRoom}” and all of its files and folders for every collaborator.
                </div>
                <div className="modal-actions">
                  <button className="secondary-button" onClick={() => setDialog(null)}>Cancel</button>
                  <button className="danger-button" onClick={submitDialog}>Delete room</button>
                </div>
              </>
            ) : dialog.type === "delete" ? (
              <>
                <div className="modal-icon danger-icon">⌫</div>
                <div className="modal-title">Delete {dialog.node.kind}?</div>
                <div className="modal-copy">
                  {dialog.node.kind === "folder"
                    ? `“${dialog.node.name}” and everything inside it will be deleted.`
                    : `“${dialog.node.name}” will be removed from the workspace.`}
                </div>
                <div className="modal-actions">
                  <button className="secondary-button" onClick={() => setDialog(null)}>Cancel</button>
                  <button className="danger-button" onClick={submitDialog}>Delete</button>
                </div>
              </>
            ) : (
              <>
                <div className="modal-icon">
                  {dialog.type === "create" ? (dialog.kind === "folder" ? "▱" : "＋") : dialog.type === "username" ? "●" : "✎"}
                </div>
                <div className="modal-title">
                  {dialog.type === "create"
                    ? `New ${dialog.kind}`
                    : dialog.type === "username"
                    ? "Change username"
                    : "Rename"}
                </div>
                <div className="modal-copy">
                  {dialog.type === "create"
                    ? `Create a new ${dialog.kind} in ${
                        dialog.parentId ? "the selected folder" : "the workspace"
                      }.`
                    : dialog.type === "username"
                    ? "Choose the name your collaborators will see."
                    : `Choose a new name for “${dialog.node.name}”.`}
                </div>
                <input
                  autoFocus
                  className="modal-input"
                  value={dialogValue}
                  onChange={(event) => setDialogValue(event.target.value)}
                  onKeyDown={(event) => {
                    if (event.key === "Enter") submitDialog();
                    if (event.key === "Escape") setDialog(null);
                  }}
                  spellCheck={false}
                />
                <div className="modal-actions">
                  <button className="secondary-button" onClick={() => setDialog(null)}>Cancel</button>
                  <button className="primary-button" disabled={!dialogValue.trim()} onClick={submitDialog}>
                    {dialog.type === "rename" ? "Rename" : dialog.type === "username" ? "Save" : "Create"}
                  </button>
                </div>
              </>
            )}
          </div>
        </div>
      )}

      {commandOpen && (
        <div className="command-backdrop" onMouseDown={() => setCommandOpen(false)}>
          <div className="command-palette glass" onMouseDown={(event) => event.stopPropagation()}>
            <div className="command-search">
              <span>⌕</span>
              <input
                autoFocus
                value={commandQuery}
                onChange={(event) => setCommandQuery(event.target.value)}
                placeholder="Type a command…"
              />
              <kbd>ESC</kbd>
            </div>
            <div className="command-list">
              {filteredCommands.map((command) => (
                <button
                  key={command.label}
                  onClick={() => {
                    command.run();
                    setCommandOpen(false);
                  }}
                >
                  <span>{command.label}</span>
                  <kbd>{command.shortcut}</kbd>
                </button>
              ))}
              {filteredCommands.length === 0 && (
                <div className="command-empty">No matching commands</div>
              )}
            </div>
          </div>
        </div>
      )}
    </div>
  );
}

export default App;
