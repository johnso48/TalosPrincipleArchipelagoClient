# The Talos Principle Reawakened - Archipelago Mod

A mod for [The Talos Principle Reawakened](https://store.steampowered.com/app/1938910/The_Talos_Principle_Reawakened/) that integrates with [Archipelago](https://archipelago.gg/) multiworld randomizer.

## Features

- **Full Archipelago Integration**: Connect to AP servers and receive items from other worlds
- **Decoupled Collection**: Physical pickup locations are separate from item ownership
- **Re-collectable Items**: Items can be picked up multiple times if granted by different worlds
- **Real-time Sync**: Locations and items sync automatically via WebSocket connection
- **Debug Tools**: F5-F11 keybinds for testing grants, inspecting state, and connection status

## Installation

### Prerequisites

- [The Talos Principle Reawakened](https://store.steampowered.com/app/1938910/The_Talos_Principle_Reawakened/)
- [UE4SS](https://github.com/UE4SS-RE/RE-UE4SS) (for Lua mod support)

### Setup

1. **Install the mod**:
   - Download the latest release
   - Extract to `Talos1/Binaries/Win64/Mods/ArchipelagoMod/`

2. **Install lua-apclientpp**:
   - Download [lua-apclientpp v0.6.4+](https://github.com/black-sliver/lua-apclientpp/releases) (`lua54.7z`)
   - Extract the **`lua54-clang64-static`** build
   - Copy `lua-apclientpp.dll` to `scripts/` folder

3. **Configure connection**:
   - Copy `scripts/config.json.example` to `scripts/config.json`
   - Edit with your AP server details:
     ```json
     {
       "server": "archipelago.gg:38281",
       "slot_name": "YourName",
       "password": "",
       "game": "The Talos Principle Reawakened"
     }
     ```

4. **Launch the game** and the mod will auto-connect

## Configuration

Edit `scripts/config.json`:

- **server**: AP server address and port (e.g. `archipelago.gg:38281`)
- **slot_name**: Your player/slot name in the multiworld
- **password**: Server password (leave empty `""` if none)
- **game**: Game name (should be `"The Talos Principle Reawakened"`)

## Debug Keybinds

- **F5**: Grant DJ3 (simulate receiving item from AP)
- **F6**: Dump full state (collection, inventory, progress)
- **F7**: Inspect all items (visibility, collision, grant status)
- **F8**: Revoke DJ3 (make it re-collectable)
- **F9**: Reset all checked locations
- **F10**: Show Archipelago connection status
- **F11**: Reconnect to Archipelago server

## How It Works

### Core Mechanics

The mod separates two concepts:
- **Location Checked**: The player physically picked up an item in their world
- **Item Granted**: Archipelago says the player owns an item (may come from another world)

Items are:
- **Visible** if not yet checked this session
- **Collectable** if not yet checked (even if already granted)
- **Usable** (in arrangers/doors) if granted by Archipelago

### Technical Details

- **TMap Management**: Two competing loops (100ms enforce, 10ms visibility) enable granted items to be usable in puzzles while still physically collectable
- **Deferred Connection**: AP client is created on the poll coroutine to avoid Lua state mismatch errors
- **Item Mapping**: 90 tetrominoes + 30 stars mapped to AP numeric IDs (base: 0x540000)

## Architecture

```
scripts/
├── main.lua                    # Entry point, hooks, loops
├── config.json                 # User configuration (gitignored)
├── config.json.example         # Template config
└── lib/
    ├── ap_client.lua           # Archipelago WebSocket client
    ├── collection.lua          # Grant/check state management
    ├── config.lua              # Config loader
    ├── inventory.lua           # TMap inspection
    ├── item_mapping.lua        # Tetromino ↔ AP ID mapping
    ├── logging.lua             # Log utilities
    ├── progress.lua            # UTalosProgress access
    ├── scanner.lua             # Pickup detection
    ├── tetromino_utils.lua     # ID parsing/formatting
    └── visibility.lua          # Item visibility + collision
```

## Development

### Requirements
- Lua 5.4 knowledge
- UE4SS modding experience
- Familiarity with Archipelago protocol

### Building
No build step required — Lua scripts are loaded directly by UE4SS.

### Testing
1. Use debug keybinds (F5-F11) to test grant/revoke
2. Set logging level in `lib/logging.lua` to DEBUG for verbose output
3. Check UE4SS.log for detailed execution traces

## License

[Add your license here]

## Credits

- **lua-apclientpp**: [black-sliver/lua-apclientpp](https://github.com/black-sliver/lua-apclientpp)
- **Archipelago**: [archipelago.gg](https://archipelago.gg/)
- **UE4SS**: [UE4SS-RE/RE-UE4SS](https://github.com/UE4SS-RE/RE-UE4SS)

## Contributing

Contributions are welcome! Please open an issue or PR on GitHub.
