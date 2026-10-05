/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

/// \addtogroup Trinityd
/// @{
/// \file

#include "Common.h"
#include "Errors.h"
#include "ObjectMgr.h"
#include "World.h"
#include "Configuration/Config.h"

#include "CliRunnable.h"
#include "Log.h"
#include "Util.h"

#if TRINITY_PLATFORM != TRINITY_PLATFORM_WINDOWS
#include "Chat.h"
#include "ChatCommand.h"
#include <cstring>
#include <iostream>
#include <readline/readline.h>
#include <readline/history.h>
#include <unistd.h>
#else
#include <Windows.h>
#endif

static constexpr char CLI_PREFIX[] = "TC> ";

static inline void PrintCliPrefix()
{
    printf("%s", CLI_PREFIX);
}

#if TRINITY_PLATFORM != TRINITY_PLATFORM_WINDOWS
namespace Trinity::Impl::Readline
{
    static std::vector<std::string> vec;
    char* cli_unpack_vector(char const*, int state)
    {
        static size_t i=0;
        if (!state)
            i = 0;
        if (i < vec.size())
            return strdup(vec[i++].c_str());
        else
            return nullptr;
    }

    char** cli_completion(char const* text, int /*start*/, int /*end*/)
    {
        ::rl_attempted_completion_over = 1;
        vec = Trinity::ChatCommands::GetAutoCompletionsFor(CliHandler(nullptr,nullptr), text);
        return ::rl_completion_matches(text, &cli_unpack_vector);
    }

    int cli_hook_func()
    {
           if (World::IsStopped())
               ::rl_done = 1;
           return 0;
    }
}
#endif

void utf8print(void* /*arg*/, std::string_view str)
{
#if TRINITY_PLATFORM == TRINITY_PLATFORM_WINDOWS
    WriteWinConsole(str);
#else
{
    printf(STRING_VIEW_FMT, STRING_VIEW_FMT_ARG(str));
    fflush(stdout);
}
#endif
}

void commandFinished(void*, bool /*success*/)
{
    PrintCliPrefix();
    fflush(stdout);
}

#ifdef linux
// Non-blocking keypress detector, when return pressed, return 1, else always return 0
int kb_hit_return()
{
    struct timeval tv;
    fd_set fds;
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    select(STDIN_FILENO+1, &fds, nullptr, nullptr, &tv);
    return FD_ISSET(STDIN_FILENO, &fds);
}
#endif

/// %Thread start
void CliThread()
{
#if TRINITY_PLATFORM == TRINITY_PLATFORM_WINDOWS
    // print this here the first time
    // later it will be printed after command queue updates
    PrintCliPrefix();
#else
    // readline is only meaningful on a terminal. Under a supervisor (nodemgr,
    // systemd) stdin is /dev/null or a pipe: readline() then returns NULL at
    // once, but because it reads the descriptor itself feof(stdin) never becomes
    // true, so the loop below used to re-print the prompt at full speed and fill
    // the disk with "TC> ". A non-tty stdin is read line-by-line instead, and EOF
    // on it ends the console thread — not the server.
    bool const stdinIsTty = ::isatty(STDIN_FILENO) == 1;
    if (stdinIsTty)
    {
        ::rl_attempted_completion_function = &Trinity::Impl::Readline::cli_completion;
        {
            static char BLANK = '\0';
            ::rl_completer_word_break_characters = &BLANK;
        }
        ::rl_event_hook = &Trinity::Impl::Readline::cli_hook_func;
    }
    else
        TC_LOG_INFO("server.worldserver", "Console: stdin is not a terminal — line mode, no readline (EOF ends the console, not the server)");
#endif

    if (sConfigMgr->GetBoolDefault("BeepAtStart", true))
        printf("\a");                                       // \a = Alert

#if TRINITY_PLATFORM == TRINITY_PLATFORM_WINDOWS
    if (sConfigMgr->GetBoolDefault("FlashAtStart", true))
    {
        FLASHWINFO fInfo;
        fInfo.cbSize = sizeof(FLASHWINFO);
        fInfo.dwFlags = FLASHW_TRAY | FLASHW_TIMERNOFG;
        fInfo.hwnd = GetConsoleWindow();
        fInfo.uCount = 0;
        fInfo.dwTimeout = 0;
        FlashWindowEx(&fInfo);
    }
#endif
    ///- As long as the World is running (no World::m_stopEvent), get the command line and handle it
    while (!World::IsStopped())
    {
        fflush(stdout);

        std::string command;

#if TRINITY_PLATFORM == TRINITY_PLATFORM_WINDOWS
        if (!ReadWinConsole(command))
            continue;
#else
        if (stdinIsTty)
        {
            char* command_str = readline(CLI_PREFIX);
            ::rl_bind_key('\t', ::rl_complete);
            if (command_str == nullptr)
            {
                // Ctrl-D / EOF on an interactive console: the operator asked
                // the server to go down (upstream semantics).
                World::StopNow(SHUTDOWN_EXIT_CODE);
                break;
            }
            command = command_str;
            free(command_str);
        }
        else
        {
            // Supervised / piped stdin. Block on the next line; on EOF the
            // console simply ends. The world keeps running — a supervisor
            // closing our stdin is not a shutdown request.
            if (!std::getline(std::cin, command))
            {
                TC_LOG_INFO("server.worldserver", "Console: stdin closed — console thread exiting, server continues");
                return;
            }
        }
#endif

        if (!command.empty())
        {
            Optional<std::size_t> nextLineIndex = RemoveCRLF(command);
            if (nextLineIndex && *nextLineIndex == 0)
            {
#if TRINITY_PLATFORM == TRINITY_PLATFORM_WINDOWS
                PrintCliPrefix();
#endif
                continue;
            }

            fflush(stdout);
            sWorld->QueueCliCommand(new CliCommandHolder(nullptr, command.c_str(), &utf8print, &commandFinished));
#if TRINITY_PLATFORM != TRINITY_PLATFORM_WINDOWS
            if (stdinIsTty)
                add_history(command.c_str());
#endif
        }
#if TRINITY_PLATFORM == TRINITY_PLATFORM_WINDOWS
        else if (feof(stdin))
        {
            World::StopNow(SHUTDOWN_EXIT_CODE);
        }
#endif
    }
}
