from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def require(text: str, fragment: str, message: str) -> None:
    if fragment not in text:
        raise AssertionError(message)


def main() -> None:
    main_cpp = read("Source/Main.cpp")
    main_window_h = read("Source/MainWindow.h")
    main_window_cpp = read("Source/MainWindow.cpp")
    core_services_h = read("Source/CoreServices.h")
    core_services_cpp = read("Source/CoreServices.cpp")
    ui_cpp = read("Source/UI/UIComponent.cpp")
    graph_h = read(
        "Source/Processors/ProcessorGraph/ProcessorGraph.h"
    )
    graph_cpp = read(
        "Source/Processors/ProcessorGraph/ProcessorGraph.cpp"
    )
    plugin_h = read(
        "Source/Processors/PluginManager/PluginManager.h"
    )
    plugin_cpp = read(
        "Source/Processors/PluginManager/PluginManager.cpp"
    )

    require(
        main_cpp,
        "RuntimeOptions::parse",
        "Main must parse typed runtime isolation options",
    )
    require(
        main_cpp,
        "setApplicationReturnValue",
        "Invalid runtime options must fail with a non-zero result",
    )
    require(
        main_cpp,
        "File::isAbsolutePath",
        "Explicit state directories must be absolute",
    )
    require(
        main_cpp,
        "setSavedStateDirectoryOverride",
        "State isolation must be installed before MainWindow construction",
    )
    require(
        main_cpp,
        "InterProcessLock",
        "State directories must be locked against concurrent use",
    )
    require(
        main_cpp,
        "mainWindow.reset();\n        stateDirectoryLock.reset();",
        "The state lock must outlive MainWindow final writes",
    )
    require(
        main_cpp,
        "canonicalDirectory",
        "State lock identity must use a canonical directory path",
    )
    require(
        core_services_h + core_services_cpp,
        "savedStateDirectoryOverride",
        "All saved-state callers must share one process override",
    )
    require(
        main_window_h,
        "RuntimeIsolationOptions",
        "MainWindow must receive the isolation policy",
    )
    require(
        main_window_cpp,
        "CoreServices::getSavedStateDirectory",
        "MainWindow must use the centralized saved-state directory",
    )
    require(
        main_window_cpp,
        "nativeHttpLockedOff",
        "Native HTTP disablement must be a hard runtime gate",
    )
    require(
        ui_cpp,
        "allowsNativeHttp",
        "The UI must not offer a bypass for the native HTTP lock",
    )
    require(
        main_window_cpp,
        "runtimeOptions.agentPort",
        "Agent loopback must use the requested port",
    )
    require(
        graph_h + graph_cpp,
        "includeUserPlugins",
        "ProcessorGraph must propagate user-plugin isolation",
    )
    require(
        plugin_h + plugin_cpp,
        "includeUserPlugins",
        "PluginManager must conditionally skip user plugin paths",
    )
    require(
        plugin_cpp,
        "isAChildOf (bundledPluginDirectory)",
        "Direct plugin loads must stay within the bundled plugin root",
    )
    require(
        ui_cpp + core_services_cpp,
        "allowsUserPlugins",
        "Plugin installation actions must respect user-plugin isolation",
    )
    require(
        main_window_cpp,
        "agentPortExplicit",
        "Explicit Agent endpoint failures must fail closed",
    )
    require(
        main_window_cpp,
        'publishRuntimeRecoveryGraph ("CONFIG_LOAD")',
        "A successfully loaded graph must publish live runtime evidence",
    )
    require(
        main_window_cpp,
        "if (! runtimeOptions.stateDirectory.empty())",
        "Official state behavior must remain unchanged without isolation",
    )
    require(
        main_window_cpp,
        'getChildFile ("recoveryConfig.xml")',
        "Runtime graph evidence must use the isolated recovery path",
    )

    print("PASS runtime isolation wiring source contract")


if __name__ == "__main__":
    main()
