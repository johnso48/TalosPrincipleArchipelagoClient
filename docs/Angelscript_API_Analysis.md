# The Talos Principle Reawakened - Angelscript API Analysis

This document provides a comprehensive analysis of the game's Lua type definitions from `shared/types/Angelscript.lua` for Archipelago mod development.

Please note that this document is a best guess analysis and may contain inaccurate information

## Table of Contents

1. [Tetromino System](#tetromino-system)
2. [Arranger/Puzzle UI System](#arrangerpuzzle-ui-system)
3. [Progress/Save System](#progresssave-system)
4. [Game Instance & Statics](#game-instance--statics)
5. [HUD System](#hud-system)
6. [Events & Delegates](#events--delegates)
7. [Player Character](#player-character)
8. [Key Findings & Recommendations](#key-findings--recommendations)

---

## Tetromino System

### Core Classes

#### `ATetrominoItem : AActor` (Line 8593)
The world actor representing a collectible tetromino piece.

**Key Fields:**
| Field | Type | Description |
|-------|------|-------------|
| `Root` | `USceneComponent` | Scene root |
| `TetrominoMesh` | `USkeletalMeshComponent` | Visual mesh |
| `Capsule` | `UCapsuleComponent` | Collision capsule |
| `Particles` | `UNiagaraComponent` | VFX |
| `bIsAnimating` | `boolean` | **IMPORTANT**: True during pickup animation |
| `InstanceInfo` | `FTetrominoInstanceInfo` | Type/Shape/Number info |
| `OnPicked` | `FTetrominoItemOnPicked` | **Delegate**: Fired when picked up |

**Key Methods:**
| Method | Description |
|--------|-------------|
| `MarkPicked()` | Marks the tetromino as picked (called during collection) |
| `HideTetromino()` | Hides the tetromino visually |
| `UnhideTetromino()` | Shows the tetromino visually |
| `PerformWidgetUpdates()` | Updates associated UI widgets |
| `OnBeginOverlap(actor, actor)` | Overlap event handler |
| `UpdateAnimationTimer(delta) -> bool` | Updates pickup animation |
| `GetTetrominoColorInfo() -> FTetrominoColorInfo` | Gets color info |

#### `ATetrominoMap : AGameplayActor` (Line 8661)
World object displaying tetromino collection status on a map.

**Key Fields:**
| Field | Type | Description |
|-------|------|-------------|
| `TetrominoInstances` | `TArray<FTetrominoInstanceInfo>` | List of tetrominos on this map |
| `CreatedTetrominoDecals` | `TMap<FString, UDecalComponent>` | Decals for tetrominos |
| `CreatedCrossDecals` | `TMap<FString, UDecalComponent>` | Cross marks for collected |

**Key Methods:**
| Method | Description |
|--------|-------------|
| `CreateTetrominoDecals()` | Creates visual decals |
| `CreateCrossDecals()` | Creates collected markers |

#### `ATetrominoSign : AGameplayActor` (Line 8688)
Sign post that shows tetromino hints/directions.

---

### Data Structures

#### `FTetrominoInstanceInfo` (C++ struct - not defined in Lua)
Contains tetromino identification data. Referenced in multiple places.

**Known Fields (from usage):**
- `Type` - ETetrominoPieceType (Door=1, Messenger=2, etc.)
- `Shape` - ETetrominoPieceShape (I=1, J=2, L=4, O=8, S=16, T=32, Z=64)
- `Number` - int32 (1, 2, 3, etc.)

#### Tetromino ID Format
From our mod implementation, IDs are formatted as: `"DJ3"` where:
- First char = Type letter (D=Door, M=Messenger, N=Nexus, S=Star, A=Administrator, R=Riddle, H=Help)
- Second char = Shape letter (I, J, L, O, S, T, Z)
- Remaining = Number

---

### Events

#### `UScriptEvent_TetrominoPicked : UScriptEventBase` (Line 13751)
Script event fired when a tetromino is picked up. No additional fields defined.

---

## Arranger/Puzzle UI System

### Widget Classes

#### `UArrangerInfoPanel : UExplorationModeWidget` (Line 10536)
**Main HUD panel showing all tetromino puzzle widgets.**

**Key Fields:**
| Field | Type | Description |
|-------|------|-------------|
| `Root` | `UWrapBox` | Container for widgets |
| `TetrominoStarCounter` | `UArrangerSimpleCounter` | Star counter |
| `HelpSigilCounter` | `UArrangerSimpleCounter` | Help sigil counter |
| `SigilCounters` | `TArray<UArrangerSimpleCounter>` | All sigil counters |
| `Widgets` | `TArray<UArrangerPuzzleWidget>` | **All puzzle widgets** |

**Key Methods:**
| Method | Description |
|--------|-------------|
| `UpdateInventory()` | **IMPORTANT**: Refreshes the inventory display |

#### `UArrangerPuzzleWidget : UArrangerWidgetWithLines` (Line 10591)
**Individual puzzle widget showing tetromino slots.**

**Key Fields:**
| Field | Type | Description |
|-------|------|-------------|
| `ArrangerPanel` | `UCanvasPanel` | Visual panel |
| `Icon` | `UImage` | Puzzle icon |
| `SinglePieceWidgetClass` | `TSubclassOf<UTetrominoPiece>` | Widget class for pieces |
| `PuzzleId` | `FString` | Puzzle identifier |
| `PuzzleInfo` | `FArrangerPuzzleInfo` | Puzzle data |
| `TetrominoSlots` | `TArray<UTetrominoPiece>` | **Individual piece widgets** |

**Key Methods:**
| Method | Description |
|--------|-------------|
| `UpdateCollectedTetrominos(TArray<FTetrominoInstanceInfo>)` | **Updates which tetrominos show as collected** |
| `FlushPendingTetromino()` | Clears pending animation state |
| `OnTetrominoPicked_AnimTrigger()` | Triggers pickup animation |
| `StartBlinking()` | Starts blink animation |
| `BlinkStep()` | Advances blink animation |
| `Initialize(AArranger, FString, FArrangerPuzzleInfo)` | Initializes the widget |
| `GetArrangerRef() -> AArranger` | Gets the arranger actor |
| `SetArrangerRef(AArranger)` | Sets the arranger actor |

#### `UTetrominoPiece : UUserWidget` (Line 17478)
**Individual tetromino slot in a puzzle widget.**

**Key Fields:**
| Field | Type | Description |
|-------|------|-------------|
| `HexagonCell` | `UImage` | Background hexagon |
| `TetrominoIcon` | `UImage` | Tetromino shape icon |
| `TetrominoIconOutline` | `UImage` | Outline for collected state |
| `TetrominoType` | `ETetrominoPieceType` | Type enum |
| `TetrominoShape` | `ETetrominoPieceShape` | Shape enum |

**Key Methods:**
| Method | Description |
|--------|-------------|
| `RestoreColor()` | Restores default color |
| `Blink()` | Triggers blink effect |

#### `UArrangerSimpleCounter : UArrangerWidgetWithLines` (Line 10642)
Counter widget for stars, help sigils, etc.

**Key Fields:**
| Field | Type | Description |
|-------|------|-------------|
| `CounterType` | `ETetrominoPieceType` | What type this counts |
| `CollectibleShape` | `ETetrominoPieceShape` | What shape this counts |

#### `UArrangerMissingPiecesWidget : UUserWidget` (Line 10553)
Warning widget when pieces are missing for a puzzle.

**Key Methods:**
| Method | Description |
|--------|-------------|
| `RunTetrominoMissingWarning(TArray<ETetrominoPieceShape>, ETetrominoPieceType, bool)` | Shows missing piece warning |

---

### Arranger Actor

#### `AScriptArranger : AArranger` (Line 7436)
The world actor for an arranger puzzle.

**Key Fields:**
| Field | Type | Description |
|-------|------|-------------|
| `ArrangerInfoWidgetClass` | `TSubclassOf<UArrangerPuzzleWidget>` | Widget class to use |

**Key Methods:**
| Method | Description |
|--------|-------------|
| `GetPuzzleId() -> FString` | Gets puzzle identifier |
| `OnScriptTetrominoCollected_BP(ATetrominoItem)` | **Called when tetromino collected for this arranger** |
| `OnArrangerSolved(ATalosCharacter)` | Called when puzzle is solved |
| `OnStartUsingArranger(ATalosCharacter)` | Player started interacting |
| `OnStopUsingArranger(ATalosCharacter)` | Player stopped interacting |
| `GetOrCreateArrangerInfoWidget() -> UArrangerPuzzleWidget` | Gets or creates the widget |
| `RemoveWidget()` | Removes the widget |

---

## Progress/Save System

### Progress Classes

#### `UScriptTalosProgress : UTalosProgress` (Line 13899)
**Main progress tracking object (save data).**

**Key Fields:**
| Field | Type | Description |
|-------|------|-------------|
| `AtmosphereIndexOnLevel` | `TMap<FName, int32>` | Atmosphere settings |
| `VisitedTerminals` | `TArray<FString>` | Terminals visited |
| `PlayedVoiceovers` | `TArray<FString>` | Voiceovers heard |
| `CollectedMessengerHints` | `TArray<FString>` | Messenger hints |
| `HeardAudioLogsInPlaythrough` | `TArray<uint8>` | Audio logs heard |

**Note:** `CollectedTetrominos` is defined in the C++ base class `UTalosProgress`, not visible in Angelscript.

**Key Methods:**
| Method | Description |
|--------|-------------|
| `SetPuzzleEditorStarCollected(ATetrominoItem, ETetrominoPieceType, ETetrominoPieceShape, bool, bool)` | Marks puzzle editor star collected |
| `IsPuzzleEditorStarCollected(ATetrominoItem) -> bool` | Checks if puzzle editor star collected |
| `MarkPuzzleEditorCollectibleUsedInCurrentEpisode(ETetrominoPieceType, ETetrominoPieceShape, bool)` | Marks collectible as used |
| `FindTetrominoLocationsForArranger(FString, FSolvedArrangerTetrominoLocations) -> bool` | Gets tetromino placement |
| `StoreTetrominoLocationsForArranger(FString, FSolvedArrangerTetrominoLocations)` | Stores tetromino placement |

---

## Game Instance & Statics

### Static Utility Classes

These are accessed via `FindFirstOf()` and contain static utility methods.

#### `UModule_Game_Info_ProgressStatics` (Line 12501)

**Key Methods:**
| Method | Description |
|--------|-------------|
| `GetTalosProgress(_World_Context) -> UScriptTalosProgress` | **Gets the current progress object** |
| `Get(_World_Context) -> UScriptTalosProgress` | Alias for above |
| `ConsoleDump(TArray<FString>, _World_Context)` | Debug dump |
| `ConsoleBoolGet/Set(...)` | Console variable access |

#### `UModule_Game_Info_PuzzlesDatabaseStatics` (Line 12536)

**Key Methods:**
| Method | Description |
|--------|-------------|
| `GetPuzzleInfoByTetromino(FTetrominoInstanceInfo, FName, FPuzzleInfo, _World_Context) -> bool` | Gets puzzle info for a tetromino |
| `CompletelySolvePuzzle(UObject, FName, FTetrominoInstanceInfo, ATetrominoItem, ATalosSwitch, _World_Context)` | **Completely solves a puzzle** |

#### `UModule_Game_Info_TalosGameInstanceStatics` (Line 12554)

**Key Methods:**
| Method | Description |
|--------|-------------|
| `Get(UObject, _World_Context) -> UScriptTalosGameInstance` | Gets game instance |

---

## HUD System

### HUD Classes

#### `UTalosHUD : UTalosUserWidget` (Line 16294)
**Main game HUD widget.**

**Key Fields:**
| Field | Type | Description |
|-------|------|-------------|
| `ArrangerInfo` | `UArrangerInfoPanel` | **The tetromino inventory panel** |
| `TokenCounter` | `UMessengerTokenCounter` | Messenger token counter |
| `PuzzleInfo` | `UPuzzleInfo` | Current puzzle info |
| `MechanicsProgress` | `UMechanicsProgress` | Mechanics tutorial progress |

**Key Methods:**
| Method | Description |
|--------|-------------|
| `SetExplorationMode(bool, bool)` | Sets exploration mode |
| `UpdateExplorationMode()` | Updates exploration mode display |
| `ShowInteractionHint(FText)` | Shows interaction hint |
| `HideInteractionHint()` | Hides interaction hint |

#### `UModule_Game_Interface_HUD_TalosHUDStatics` (Line 12668)

**Key Methods:**
| Method | Description |
|--------|-------------|
| `ToggleHud(bool, _World_Context)` | Toggles HUD visibility |
| `UpdateExplorationMode(double, _World_Context)` | Updates exploration mode |

---

## Events & Delegates

### Tetromino-Related Events

| Event/Delegate | Source | Description |
|---------------|--------|-------------|
| `OnPicked` (`FTetrominoItemOnPicked`) | `ATetrominoItem` | Fired when tetromino is picked |
| `UScriptEvent_TetrominoPicked` | Script event | General tetromino picked event |

### Native Hook Points

Based on our mod implementation, these native functions can be hooked:

| Function | Class | Description |
|----------|-------|-------------|
| `MarkTetrominoCollected` | `PuzzleMemoryFunctions` | **Called when tetromino is registered as collected** |
| `ClientRestart` | `PlayerController` | Player respawn/restart |

---

## Player Character

### `AScriptTalosCharacter : ATalosCharacter` (Line 7483)
The player character.

**Key Methods:**
| Method | Description |
|--------|-------------|
| `PerformReset()` | Resets to checkpoint |
| `IsInteracting() -> bool` | Checks if player is interacting |
| `DisplayInputHint(FHudInputInfo, double)` | Shows input hint |
| `HideCurrentInputHint()` | Hides input hint |

---

## Key Findings & Recommendations

### 1. UI Update Challenge

The `UArrangerPuzzleWidget:UpdateCollectedTetrominos()` method expects a `TArray<FTetrominoInstanceInfo>`, which is difficult to construct from Lua. Options:

- **Option A:** Find a way to call `UArrangerInfoPanel:UpdateInventory()` without crashes
- **Option B:** Access `UArrangerPuzzleWidget.TetrominoSlots` and manipulate individual `UTetrominoPiece` widgets directly
- **Option C:** Hook into `AScriptArranger:OnScriptTetrominoCollected_BP()` to understand the update flow

### 2. Collection Flow (Natural Pickup)

Based on the API analysis, the normal collection flow appears to be:

1. Player overlaps `ATetrominoItem` capsule
2. `OnBeginOverlap()` is triggered
3. `MarkPicked()` is called
4. `bIsAnimating` becomes `true`
5. `OnPicked` delegate fires
6. `PuzzleMemoryFunctions:MarkTetrominoCollected()` is called (native C++)
7. `AScriptArranger:OnScriptTetrominoCollected_BP()` is called
8. Widget updates occur via `PerformWidgetUpdates()`

### 3. Potential Approaches for UI Refresh

1. **Call `ATetrominoItem:PerformWidgetUpdates()`** - This method exists and might trigger proper UI updates
2. **Broadcast the `OnPicked` delegate** on a tetromino item
3. **Find and call `AScriptArranger:OnScriptTetrominoCollected_BP()`** for the relevant arranger
4. **Toggle exploration mode** via `TalosHUD` which might force a re-read

### 4. Important Native Functions (Not in Angelscript)

These are referenced but defined in C++:
- `PuzzleMemoryFunctions:MarkTetrominoCollected` - The actual collection registration
- `UTalosProgress` - Base progress class with `CollectedTetrominos` TMap
- `FTetrominoInstanceInfo` - The struct containing Type/Shape/Number

### 5. TMap Key Format

The `CollectedTetrominos` TMap uses string keys in format `"DJ3"` (Type+Shape+Number) mapping to boolean values (possibly indicating whether it was used in a puzzle).

---

## Appendix: Enum Values

### ETetrominoPieceType (Bit flags)
| Letter | Value | Name |
|--------|-------|------|
| D | 1 | Door |
| M | 2 | Messenger |
| N | 4 | Nexus |
| S | 8 | Star |
| A | 16 | Administrator |
| R | 32 | Riddle |
| H | 64 | Help |

### ETetrominoPieceShape (Bit flags)
| Letter | Value | Name |
|--------|-------|------|
| I | 1 | I-piece |
| J | 2 | J-piece |
| L | 4 | L-piece |
| O | 8 | O-piece |
| S | 16 | S-piece |
| T | 32 | T-piece |
| Z | 64 | Z-piece |

---

*Document generated for Archipelago mod development - February 2026*
