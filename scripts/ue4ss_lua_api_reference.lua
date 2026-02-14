--[[
==============================================================================
  UE4SS Lua API Reference (v3.0.0)
  Documentation Summary from: https://docs.ue4ss.com/lua-api.html
==============================================================================

This file provides a comprehensive reference for the UE4SS Lua API.
UE4SS allows you to interact with Unreal Engine 4/5 games through Lua.

------------------------------------------------------------------------------
  GLOBAL FUNCTIONS
------------------------------------------------------------------------------
]]

-- PRINTING & OUTPUT
-- print(string Message)
--   Basic print without formatting. Use string.format() if formatting needed.

-- OBJECT FINDING
-- StaticFindObject(string ObjectName) -> UObject | AActor | nil
-- StaticFindObject(UClass Class, UObject InOuter, string ObjectName, bool ExactClass)
--   Maps to UE's StaticFindObject API.

-- FindFirstOf(string ShortClassName) -> UObject | AActor | nil
--   Find first non-default instance of class (short name only, no path).

-- FindAllOf(string ShortClassName) -> table of UObjects
--   Find all non-default instances of class (short name only, no path).

-- FindObject(string|FName|nil ClassName, string|FName|nil ObjectShortName, 
--            EObjectFlags RequiredFlags, EObjectFlags BannedFlags) -> UObject
--   Either ClassName or ObjectShortName can be nil, but not both.

-- FindObjects(integer NumObjectsToFind, string|FName|nil ClassName, 
--             string|FName|nil ObjectShortName, EObjectFlags RequiredFlags, 
--             EObjectFlags BannedFlags, bool bExactClass) -> table of UObjects
--   Set NumObjectsToFind to 0 or nil to find all matching objects.

-- ForEachUObject(function Callback)
--   Execute callback for each UObject in GUObjectArray.
--   Callback params: (UObject object, integer ChunkIndex, integer ObjectIndex)

-- KEY BINDINGS
-- RegisterKeyBind(integer Key, function Callback)
-- RegisterKeyBind(integer Key, table ModifierKeys, function callback)
--   Register callback for key-bind (only works when game/console is focused).
--   ModifierKeys table format: { "#" | "string (Microsoft Virtual Key-Code)" }

-- IsKeyBindRegistered(integer Key) -> bool
-- IsKeyBindRegistered(integer Key, table ModifierKeys) -> bool
--   Check if keys are registered.

-- FUNCTION HOOKS
-- RegisterHook(string UFunctionName, function Callback) -> integer PreId, integer PostId
--   Hook a UFunction. Callback params: (UObject self, UFunctionParams...)
--   Returns two IDs needed for UnregisterHook.

-- UnregisterHook(string UFunctionName, integer PreId, integer PostId)
--   Unregister a previously registered hook.

-- GAME EVENT HOOKS
-- RegisterLoadMapPreHook(function Callback)
-- RegisterLoadMapPostHook(function Callback)
--   Callback params: (UEngine, FWorldContext&, FURL, UPendingNetGame*, FString& Error)

-- RegisterInitGameStatePreHook(function Callback)
-- RegisterInitGameStatePostHook(function Callback)
--   Callback params: (AGameModeBase Context)

-- RegisterBeginPlayPreHook(function Callback)
-- RegisterBeginPlayPostHook(function Callback)
--   Callback params: (AActor Context)

-- RegisterProcessConsoleExecPreHook(function Callback)
-- RegisterProcessConsoleExecPostHook(function Callback)
--   Callback params: (UObject Context, string Cmd, table CommandParts, 
--                     FOutputDevice Ar, UObject Executor)
--   Return true/false to override ProcessConsoleExec return value.

-- RegisterCallFunctionByNameWithArgumentsPreHook(function Callback)
-- RegisterCallFunctionByNameWithArgumentsPostHook(function Callback)
--   Callback params: (UObject Context, string Str, FOutputDevice Ar, 
--                     UObject Executor, bool bForceCallWithNonExec)

-- RegisterULocalPlayerExecPreHook(function Callback)
-- RegisterULocalPlayerExecPostHook(function Callback)
--   Callback params: (ULocalPlayer Context, UWorld InWorld, string Cmd, FOutputDevice Ar)
--   Return value 1: true/false to override Exec return
--   Return value 2: false to prevent original Exec execution

-- CONSOLE COMMANDS
-- RegisterConsoleCommandHandler(string CommandName, function Callback)
--   Register callback for console command (UGameViewportClient context only).
--   Callback params: (string Cmd, table CommandParts, FOutputDevice Ar)
--   Return true to prevent other handlers from running.

-- RegisterConsoleCommandGlobalHandler(string CommandName, function Callback)
--   Like RegisterConsoleCommandHandler but runs in all contexts.

-- CUSTOM EVENTS
-- RegisterCustomEvent(string EventName, function Callback)
--   Register callback for BP function/event with name EventName.

-- NotifyOnNewObject(string UClassName, function Callback)
--   Execute callback whenever instance of class (or derived class) is constructed.

-- OBJECT CONSTRUCTION
-- StaticConstructObject(UClass Class, UObject Outer, FName Name, 
--                       EObjectFlags Flags, EInternalObjectFlags InternalSetFlags,
--                       bool CopyTransientsFromClassDefaults, 
--                       bool AssumeTemplateIsArchetype, UObject Template,
--                       FObjectInstancingGraph InstanceGraph, UPackage ExternalPackage,
--                       void SubobjectOverrides) -> UObject
--   Construct UObject of passed UClass. Maps to UE's StaticConstructObject_Internal.

-- UTILITIES
-- FName(string Name) -> FName
-- FName(integer ComparisonIndex) -> FName
--   Returns FName for string/index, or "None" if doesn't exist.

-- FText(string Text) -> FText
--   Returns FText representation of string.

-- LoadAsset(string AssetPathAndName)
--   Load asset by name. MUST be called from game thread only.

-- ExecuteInGameThread(function Callback)
--   Execute code in game thread using ProcessEvent.

-- ExecuteAsync(function Callback)
--   Asynchronously execute function.

-- ExecuteWithDelay(integer DelayInMilliseconds, function Callback)
--   Asynchronously execute function after delay.

-- LoopAsync(integer DelayInMilliseconds, function Callback)
--   Start loop that sleeps for delay. Stops when callback returns true.

-- IterateGameDirectories() -> table
--   Returns table of all game directories.
--   Example: IterateGameDirectories().Game.Binaries.Win64
--   Use .__name, .__absolute_path, .__files for details.

-- CUSTOM PROPERTIES
-- RegisterCustomProperty(table CustomPropertyInfo)
--   Register custom property for automatic use with UObject.__index.
--   CustomPropertyInfo format:
--     Name            | string
--     Type            | table (PropertyTypes)
--     BelongsToClass  | string (full class name)
--     OffsetInternal  | integer or table (OffsetInternalInfo)
--     ArrayProperty   | table (Optional, ArrayPropertyInfo)

--[[
------------------------------------------------------------------------------
  MAIN CLASSES
------------------------------------------------------------------------------
]]

-- RemoteObject
--   Base class for objects containing pointer to C/C++ game object.
--   Methods:
--     IsValid() -> bool

-- LocalObject
--   Base class for objects fully owned by Lua.

-- UE4SS
--   Interact with UE4SS metadata.
--   Methods:
--     GetVersion() -> integer major, integer minor, integer hotfix

-- UnrealVersion
--   Methods:
--     GetMajor() -> integer
--     GetMinor() -> integer
--     IsEqual(number Major, number Minor) -> bool
--     IsAtLeast(number Major, number Minor) -> bool
--     IsAtMost(number Major, number Minor) -> bool
--     IsBelow(number Major, number Minor) -> bool
--     IsAbove(number Major, number Minor) -> bool

-- Mod
--   Interact with local mod object (inherits RemoteObject).
--   Methods:
--     SetSharedVariable(string Name, any Value)
--       Share variable across mods. Value types: nil, string, number, bool, 
--       UObject, lightuserdata. Not reset on hot-reload.
--     
--     GetSharedVariable(string Name) -> any
--       Get variable set by any mod.
--     
--     type() -> "ModRef"

-- UObject (inherits RemoteObject)
--   Base class for most Unreal Engine game objects.
--   Methods:
--     __index(string MemberVarName) -> auto
--       Get member variable or callable UFunction.
--     
--     __newindex(string MemberVarName, auto NewValue)
--       Set member variable value.
--     
--     GetFullName() -> string
--     GetFName() -> FName
--     GetAddress() -> integer
--     GetClass() -> UClass
--     GetOuter() -> UObject
--     GetWorld() -> UWorld
--     
--     IsAnyClass() -> bool
--     IsClass() -> bool
--     IsA(UClass Class) -> bool
--     IsA(string FullClassName) -> bool
--     
--     HasAllFlags(EObjectFlags Flags) -> bool
--     HasAnyFlags(EObjectFlags Flags) -> bool
--     HasAnyInternalFlags(EInternalObjectFlags Flags) -> bool
--     
--     Reflection() -> UObjectReflection
--     GetPropertyValue(string MemberVarName) -> auto  -- Same as __index
--     SetPropertyValue(string MemberVarName, auto NewValue)  -- Same as __newindex
--     
--     CallFunction(UFunction function, auto Params...)
--     ProcessConsoleExec(string Cmd, nil Reserved, UObject Executor)
--     
--     type() -> string  -- Returns UE4SS type, not Unreal type

-- UStruct (inherits UObject)
--   Methods:
--     GetSuperStruct() -> UClass  -- Can be invalid
--     
--     ForEachFunction(function Callback)
--       Callback param: (UFunction Function)
--       Return true to stop iterating.
--     
--     ForEachProperty(function Callback)
--       Callback param: (Property Property)
--       Return true to stop iterating.

-- UClass (inherits UStruct)
--   Methods:
--     GetCDO() -> UClass  -- Returns ClassDefaultObject
--     IsChildOf(UClass Class) -> bool

-- AActor (inherits UObject)
--   Methods:
--     GetWorld() -> UObject | nil
--     GetLevel() -> UObject | nil

-- UFunction (inherits UObject)
--   Methods:
--     __call(UFunctionParams...)  -- Call the function
--     GetFunctionFlags() -> integer
--     SetFunctionFlags(integer Flags)

-- FName (inherits LocalObject)
--   Methods:
--     ToString() -> string
--     GetComparisonIndex() -> integer  -- Index into global names array

-- FString (inherits RemoteObject)
--   TArray of characters.
--   Methods:
--     ToString() -> string
--     Clear()  -- Sets TArray size to 0

-- TArray (inherits RemoteObject)
--   Methods:
--     __index(integer ArrayIndex) -> auto
--     __newindex(integer ArrayIndex, auto NewValue)
--     
--     GetArrayAddress() -> integer
--     GetArrayNum() -> integer  -- Current element count
--     GetArrayMax() -> integer  -- Max capacity
--     GetArrayDataAddress() -> integer
--     
--     ForEach(function Callback)
--       Callback params: (integer index, RemoteUnrealParam|LocalUnrealParam elem)
--       Use elem:get() and elem:set() to access/mutate elements.

-- UEnum (inherits RemoteObject)
--   Methods:
--     GetNameByValue(integer Value) -> FName
--     
--     ForEachName(function Callback)
--       Callback params: (FName Name, integer Value)
--       Return true to stop iterating.

-- UScriptStruct (inherits LocalObject)
--   Methods:
--     __index(string MemberVarName) -> auto
--     __newindex(string MemberVarName, auto NewValue)
--     
--     GetBaseAddress() -> integer  -- UObject address
--     GetStructAddress() -> integer
--     GetPropertyAddress() -> integer  -- U/FProperty address
--     
--     IsValid() -> bool
--     IsMappedToObject() -> bool
--     IsMappedToProperty() -> bool
--     
--     type() -> "UScriptStruct"

-- Property (inherits RemoteObject)
--   Methods:
--     GetFullName() -> string
--     GetFName() -> FName
--     GetClass() -> PropertyClass
--     IsA(PropertyTypes PropertyType) -> bool
--     
--     ContainerPtrToValuePtr(UObject Container, integer ArrayIndex) -> LightUserdata
--       Equivalent to FProperty::ContainerPtrToValuePtr<uint8>.
--     
--     ImportText(string Buffer, LightUserdata Data, integer PortFlags, UObject Owner)
--       Equivalent to FProperty::ImportText (without ErrorText param).

-- ObjectProperty (inherits Property)
--   Methods:
--     GetPropertyClass() -> UClass

-- BoolProperty (inherits Property)
--   Methods:
--     GetByteMask() -> integer
--     GetByteOffset() -> integer
--     GetFieldMask() -> integer
--     GetFieldSize() -> integer

-- StructProperty (inherits Property)
--   Methods:
--     GetStruct() -> UScriptStruct

-- ArrayProperty (inherits Property)
--   Methods:
--     GetInner() -> Property

-- UObjectReflection
--   Methods:
--     GetProperty(string PropertyName) -> Property

-- FieldClass (inherits LocalObject)
--   Methods:
--     GetFName() -> FName

-- FOutputDevice (inherits RemoteObject)
--   Methods:
--     Log(string Message)  -- Log to output device (in-game console)

-- FWeakObjectPtr (inherits LocalObject)
--   Methods:
--     Get() -> UObject derivative  -- Can be invalid, call IsValid() after

-- RemoteUnrealParam | LocalUnrealParam (inherits RemoteObject | LocalObject)
--   Dynamic wrapper for any type/class.
--   Methods:
--     get() -> auto  -- Get underlying value
--     set(auto NewValue)  -- Set underlying value
--     type() -> "RemoteUnrealParam" | "LocalUnrealParam"

--[[
------------------------------------------------------------------------------
  TABLE DEFINITIONS / ENUMS
------------------------------------------------------------------------------
]]

-- ModifierKeys
--   Table format: { "#" | "string (Microsoft Virtual Key-Code)" }

-- PropertyTypes
--   ObjectProperty, ObjectPtrProperty, Int8Property, Int16Property, IntProperty,
--   Int64Property, NameProperty, FloatProperty, StrProperty, ByteProperty,
--   UInt16Property, UIntProperty, UInt64Property, BoolProperty, ArrayProperty,
--   MapProperty, StructProperty, ClassProperty, WeakObjectProperty, EnumProperty,
--   TextProperty

-- OffsetInternalInfo
--   Property        | string (property name to use as relative start)
--   RelativeOffset  | integer (offset from relative start)

-- ArrayPropertyInfo
--   Type | table (PropertyTypes)

-- CustomPropertyInfo
--   Name            | string (name for __index metamethod)
--   Type            | table (PropertyTypes)
--   BelongsToClass  | string (full class name without type)
--   OffsetInternal  | integer or table (OffsetInternalInfo)
--   ArrayProperty   | table (Optional, ArrayPropertyInfo)

-- EObjectFlags (can be OR'd with |)
local ObjectFlags = {
    RF_NoFlags                       = 0x00000000,
    RF_Public                        = 0x00000001,
    RF_Standalone                    = 0x00000002,
    RF_MarkAsNative                  = 0x00000004,
    RF_Transactional                 = 0x00000008,
    RF_ClassDefaultObject            = 0x00000010,
    RF_ArchetypeObject               = 0x00000020,
    RF_Transient                     = 0x00000040,
    RF_MarkAsRootSet                 = 0x00000080,
    RF_TagGarbageTemp                = 0x00000100,
    RF_NeedInitialization            = 0x00000200,
    RF_NeedLoad                      = 0x00000400,
    RF_KeepForCooker                 = 0x00000800,
    RF_NeedPostLoad                  = 0x00001000,
    RF_NeedPostLoadSubobjects        = 0x00002000,
    RF_NewerVersionExists            = 0x00004000,
    RF_BeginDestroyed                = 0x00008000,
    RF_FinishDestroyed               = 0x00010000,
    RF_BeingRegenerated              = 0x00020000,
    RF_DefaultSubObject              = 0x00040000,
    RF_WasLoaded                     = 0x00080000,
    RF_TextExportTransient           = 0x00100000,
    RF_LoadCompleted                 = 0x00200000,
    RF_InheritableComponentTemplate  = 0x00400000,
    RF_DuplicateTransient            = 0x00800000,
    RF_StrongRefOnFrame              = 0x01000000,
    RF_NonPIEDuplicateTransient      = 0x01000000,
    RF_Dynamic                       = 0x02000000,
    RF_WillBeLoaded                  = 0x04000000,
    RF_HasExternalPackage            = 0x08000000,
    RF_AllFlags                      = 0x0FFFFFFF,
}

-- EInternalObjectFlags (can be OR'd with |)
local InternalObjectFlags = {
    ReachableInCluster               = 0x00800000,
    ClusterRoot                      = 0x01000000,
    Native                           = 0x02000000,
    Async                            = 0x04000000,
    AsyncLoading                     = 0x08000000,
    Unreachable                      = 0x10000000,
    PendingKill                      = 0x20000000,
    RootSet                          = 0x40000000,
    GarbageCollectionKeepFlags       = 0x0E000000,
    AllFlags                         = 0x7F800000,
}

--[[
------------------------------------------------------------------------------
  USAGE EXAMPLES
------------------------------------------------------------------------------
]]

-- Example: Find and modify an object
--[[
local MyActor = FindFirstOf("MyActorClass")
if MyActor and MyActor:IsValid() then
    local health = MyActor.Health
    MyActor.Health = 100
    print(string.format("Health changed from %d to %d", health, MyActor.Health))
end
]]

-- Example: Hook a function
--[[
local preId, postId = RegisterHook("/Script/Game.MyClass:MyFunction", function(self, ...)
    print("MyFunction called on: " .. self:GetFullName())
end)
]]

-- Example: Register key bind
--[[
RegisterKeyBind(Key.F1, function()
    print("F1 pressed!")
end)
]]

-- Example: Iterate all actors
--[[
local allActors = FindAllOf("Actor")
for i, actor in ipairs(allActors) do
    if actor:IsValid() then
        print(string.format("%d: %s", i, actor:GetFullName()))
    end
end
]]

-- Example: Custom console command
--[[
RegisterConsoleCommandHandler("MyCommand", function(cmd, parts, output)
    output:Log("MyCommand executed with args: " .. table.concat(parts, ", "))
    return true  -- Prevent other handlers
end)
]]

-- Example: Notify on object creation
--[[
NotifyOnNewObject("/Script/Engine.PlayerController", function(obj)
    print("New PlayerController created: " .. obj:GetFullName())
end)
]]

-- Example: TArray iteration
--[[
local someArray = MyObject.MyArrayProperty
someArray:ForEach(function(index, elem)
    local value = elem:get()
    print(string.format("Array[%d] = %s", index, tostring(value)))
end)
]]

--[[
------------------------------------------------------------------------------
  NOTES & BEST PRACTICES
------------------------------------------------------------------------------

1. Always check IsValid() before using UObjects
2. Use game thread functions (ExecuteInGameThread, LoadAsset) appropriately
3. Hook parameters (except strings, bools, FOutputDevice) need :Get() and :Set()
4. Shared variables persist across hot-reloads
5. FindFirstOf/FindAllOf use short class names only (no path)
6. Custom properties are automatically accessible via __index
7. Return true in ForEach callbacks to stop iteration
8. Console command handlers should return true to prevent further handling

For full documentation, see: https://docs.ue4ss.com/lua-api.html
]]
