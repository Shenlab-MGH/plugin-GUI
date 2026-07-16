/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI
    Copyright (C) 2024 Open Ephys

    ------------------------------------------------------------------

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

#ifdef _WIN32
#include <winsock2.h>
#include <Windows.h>

#define _MAIN
#endif
#include "../JuceLibraryCode/JuceHeader.h"
#include "Agent/RuntimeOptions.h"
#include "MainWindow.h"

#include <fstream>
#include <memory>
#include <stdio.h>
#include <string>
#include <vector>

namespace
{
File canonicalDirectory (const File& directory)
{
#ifdef _WIN32
    if (! directory.exists())
    {
        const auto parent = directory.getParentDirectory();
        if (parent != directory)
            return canonicalDirectory (parent)
                .getChildFile (directory.getFileName());
    }

    const auto handle = CreateFileW (
        directory.getFullPathName().toWideCharPointer(),
        0,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        return directory;

    std::vector<wchar_t> path (32768);
    const auto length = GetFinalPathNameByHandleW (
        handle,
        path.data(),
        static_cast<DWORD> (path.size()),
        FILE_NAME_NORMALIZED);
    CloseHandle (handle);
    if (length == 0 || length >= path.size())
        return directory;

    String finalPath (path.data(), length);
    if (finalPath.startsWith ("\\\\?\\UNC\\"))
        finalPath = "\\\\" + finalPath.substring (8);
    else if (finalPath.startsWith ("\\\\?\\"))
        finalPath = finalPath.substring (4);
    return File (finalPath);
#else
    return directory;
#endif
}
}

/**
 * This function is called when the console window is closed.
 * Handles the CTRL+C, CTRL+BREAK, and console close button  events.
*/
#ifdef _WIN32
BOOL WINAPI ConsoleHandler (DWORD CEvent)
{
    switch (CEvent)
    {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
            JUCEApplication::getInstance()->systemRequestedQuit();
            return TRUE;
        case CTRL_CLOSE_EVENT:
            JUCEApplication::getInstance()->quit();
            return TRUE;
        default:
            return FALSE;
    }
}
#endif

/**

  Launches the application and creates the CustomLookAndFeelClass.

  The OpenEphysApplication class own the application's MainWindow (via
  a ScopedPointer).

  @see MainWindow

*/

class OpenEphysApplication : public JUCEApplication
{
public:
    OpenEphysApplication() {}

    ~OpenEphysApplication() {}

    void initialise (const String& commandLine)
    {
        std::cout << commandLine << std::endl;

        StringArray parameters;
        parameters.addTokens (commandLine, " ", "\"");
        parameters.removeEmptyStrings();

#ifdef _WIN32

        SetConsoleTitleA ("Open Ephys GUI Launcher");

        std::cout << "Initializing Open Ephys GUI... DO NOT CLOSE THIS WINDOW" << std::endl;

        SetConsoleCtrlHandler (ConsoleHandler, TRUE);

        if (HWND hwnd = GetConsoleWindow())
        {
            if (HMENU hMenu = GetSystemMenu (hwnd, FALSE))
            {
                EnableMenuItem (hMenu, SC_CLOSE, MF_BYCOMMAND | MF_DISABLED | MF_GRAYED);
            }
        }

#endif

        std::vector<std::string> arguments;
        arguments.reserve (static_cast<std::size_t> (parameters.size()));
        for (const auto& parameter : parameters)
            arguments.push_back (parameter.toStdString());

        const auto parsed = RuntimeOptions::parse (arguments);
        if (! parsed.ok())
        {
            std::cerr << "Invalid runtime options: "
                      << parsed.error << std::endl;
            setApplicationReturnValue (2);
            quit();
            return;
        }

        auto runtimeOptions = parsed.options;
        if (! runtimeOptions.stateDirectory.empty())
        {
            const String stateDirectory (
                runtimeOptions.stateDirectory);
            if (! File::isAbsolutePath (stateDirectory))
            {
                std::cerr << "--state-dir must be an absolute path."
                          << std::endl;
                setApplicationReturnValue (2);
                quit();
                return;
            }

            File stateDirectoryFile (stateDirectory);
            if (stateDirectoryFile.getParentDirectory()
                == stateDirectoryFile)
            {
                std::cerr << "--state-dir must not be a filesystem root."
                          << std::endl;
                setApplicationReturnValue (2);
                quit();
                return;
            }

            stateDirectoryFile =
                canonicalDirectory (stateDirectoryFile);
            const auto officialStateRoot = canonicalDirectory (
                File::getSpecialLocation (File::windowsLocalAppData)
                    .getChildFile ("Open Ephys"));
            if (stateDirectoryFile == officialStateRoot
                || stateDirectoryFile.isAChildOf (officialStateRoot))
            {
                std::cerr << "--state-dir must be outside the official "
                             "Open Ephys state directory."
                          << std::endl;
                setApplicationReturnValue (2);
                quit();
                return;
            }

            if (stateDirectoryFile.exists()
                && ! stateDirectoryFile.isDirectory())
            {
                std::cerr << "--state-dir exists but is not a directory."
                          << std::endl;
                setApplicationReturnValue (2);
                quit();
                return;
            }

            const auto stateDirectoryResult =
                stateDirectoryFile.createDirectory();
            if (stateDirectoryResult.failed())
            {
                std::cerr << "Unable to create --state-dir: "
                          << stateDirectoryResult.getErrorMessage()
                          << std::endl;
                setApplicationReturnValue (2);
                quit();
                return;
            }

            auto lockIdentity =
                stateDirectoryFile.getFullPathName();
#ifdef _WIN32
            lockIdentity = lockIdentity.toLowerCase();
#endif
            const auto lockName =
                "open-ephys-agent-state-"
                + String::toHexString (
                    lockIdentity.hashCode64());
            stateDirectoryLock =
                std::make_unique<InterProcessLock> (lockName);
            if (! stateDirectoryLock->enter (0))
            {
                std::cerr << "--state-dir is already in use."
                          << std::endl;
                setApplicationReturnValue (2);
                quit();
                return;
            }

            const auto writeProbe =
                stateDirectoryFile.getNonexistentChildFile (
                    ".oe-agent-write-probe",
                    ".tmp",
                    false);
            if (! writeProbe.create().wasOk()
                || ! writeProbe.deleteFile())
            {
                std::cerr << "--state-dir is not writable."
                          << std::endl;
                setApplicationReturnValue (2);
                stateDirectoryLock.reset();
                quit();
                return;
            }

            if (! CoreServices::setSavedStateDirectoryOverride (
                    stateDirectoryFile))
            {
                std::cerr << "Unable to install --state-dir override."
                          << std::endl;
                setApplicationReturnValue (2);
                quit();
                return;
            }
        }

        File fileToLoad;
        if (! runtimeOptions.configurationFile.empty())
        {
            const String configurationPath (
                runtimeOptions.configurationFile);
            fileToLoad = File::isAbsolutePath (configurationPath)
                             ? File (configurationPath)
                             : File::getCurrentWorkingDirectory()
                                   .getChildFile (configurationPath);
            if (! fileToLoad.existsAsFile())
            {
                std::cerr << "Configuration file does not exist: "
                          << fileToLoad.getFullPathName()
                          << std::endl;
                setApplicationReturnValue (2);
                quit();
                return;
            }
        }

        if (runtimeOptions.agentPortExplicit)
        {
            const auto token =
                SystemStats::getEnvironmentVariable (
                    "OE_AGENT_TOKEN",
                    "");
            if (token.length() < 32)
            {
                std::cerr << "Explicit --agent-port requires "
                             "OE_AGENT_TOKEN with at least 32 characters."
                          << std::endl;
                setApplicationReturnValue (2);
                quit();
                return;
            }
        }

        SystemStats::setApplicationCrashHandler (handleCrash);

        try
        {
            mainWindow = std::make_unique<MainWindow> (
                fileToLoad,
                runtimeOptions.headless,
                runtimeOptions);
            if (! mainWindow->getInitializationError().isEmpty())
            {
                const auto error =
                    mainWindow->getInitializationError();
                mainWindow.reset();
                std::cerr << "Open Ephys initialization failed: "
                          << error << std::endl;
                setApplicationReturnValue (2);
                stateDirectoryLock.reset();
                quit();
            }
        }
        catch (const std::exception& error)
        {
            std::cerr << "Open Ephys initialization failed: "
                      << error.what() << std::endl;
            setApplicationReturnValue (2);
            quit();
        }
    }

    void shutdown()
    {
        mainWindow.reset();
        stateDirectoryLock.reset();
    }

    static void handleCrash (void* input)
    {
        MainWindow::handleCrash (input);
    }

    void systemRequestedQuit()
    {
        if (! mainWindow)
        {
            quit();
            return;
        }

        bool shouldQuit = true;

        if (CoreServices::getAcquisitionStatus())
        {
            String message;

            if (CoreServices::getRecordingStatus())
            {
                AlertWindow::showMessageBox (AlertWindow::WarningIcon,
                                             "Cannot quit while recording is active.",
                                             "Please stop recording before closing the GUI.",
                                             "OK");
                shouldQuit = false;
            }
            else
            {
                shouldQuit = AlertWindow::showOkCancelBox (AlertWindow::WarningIcon,
                                                           "Are you sure you want to quit?",
                                                           "The GUI is still acquiring data.",
                                                           "Yes",
                                                           "No");
            }
        }

        if (shouldQuit)
        {
            mainWindow->shutDownGUI();
            quit();
        }
    }

    const String getApplicationName()
    {
        return "Open Ephys GUI";
    }

    const String getApplicationVersion()
    {
        return ProjectInfo::versionString;
    }

    bool moreThanOneInstanceAllowed()
    {
        return true;
    }

    void anotherInstanceStarted (const String& commandLine)
    {
    }

private:
    std::unique_ptr<InterProcessLock> stateDirectoryLock;
    std::unique_ptr<MainWindow> mainWindow;
};

//==============================================================================
// This macro generates the main() routine that starts the app.
START_JUCE_APPLICATION (OpenEphysApplication)
