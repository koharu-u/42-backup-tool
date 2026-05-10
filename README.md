# 42-backup-tool

Ncurses-based TUI backup/restore utility for syncing a local 42 workspace with
two git remotes (for example GitHub and Codeberg).

## Build

```bash
make
```

## Run

```bash
./42-backup-tool
```

## Configuration

The app reads config from:

1. `$BACKUP_TOOL_CONFIG` (if set)
2. `~/.config/42-backup-tool.conf`

Example config:

```conf
LOCAL_DIR=/home/you/Documents/42bangkok
GH_DIR=/home/you/Documents/.piscine-42-github
CB_DIR=/home/you/Documents/.piscine-42-codeberg
BRANCH=main
```
