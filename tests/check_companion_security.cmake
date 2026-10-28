# Garde structurelle du compagnon IA (AI Companion 6, docs/adr/0001 et 0002) :
#  - AUCUN port reseau ouvert par le pont ni l'agent : pas de serveur TCP/UDP/HTTP/WebSocket ;
#  - AUCUNE execution de commande arbitraire depuis l'agent ou le pont : pas de QProcess,
#    system(), popen(), CreateProcess, ShellExecute, exec* dans libs/agent, libs/mcp et les
#    fichiers desktop du compagnon (live_server, companion_*, claude_backend) ;
#  - AUCUN secret ecrit dans QSettings (setValue avec une cle nommee key/secret/token).
# Une exception volontaire se marque `security-ok: <raison>` sur la ligne ou la precedente.
#
# Usage : cmake -DSRC_DIR=<racine du depot> -P check_companion_security.cmake

if(NOT DEFINED SRC_DIR)
    message(FATAL_ERROR "SRC_DIR non defini")
endif()

file(GLOB_RECURSE scoped
    "${SRC_DIR}/libs/agent/*.cpp" "${SRC_DIR}/libs/agent/*.hpp"
    "${SRC_DIR}/libs/mcp/*.cpp" "${SRC_DIR}/libs/mcp/*.hpp"
    "${SRC_DIR}/apps/mcp/*.cpp"
    "${SRC_DIR}/apps/desktop/live_server.*"
    "${SRC_DIR}/apps/desktop/companion_*"
    "${SRC_DIR}/apps/desktop/claude_backend.*"
    "${SRC_DIR}/apps/desktop/claude_code_process_backend.*")

set(network_patterns "QTcpServer" "QUdpSocket" "QHttpServer" "QWebSocketServer" "QSslServer"
    "AF_INET" "INADDR_ANY" "SOCK_DGRAM" "WSAStartup")
set(exec_patterns "QProcess" "std::system" "[^a-z_]system\\(" "popen\\(" "_popen" "CreateProcess"
    "ShellExecute" "execv" "execl" "posix_spawn" "QDesktopServices::openUrl")
set(secret_patterns "setValue\\([^)]*([Kk]ey|[Ss]ecret|[Tt]oken)[^)]*\\)")

set(violations "")
foreach(src_file ${scoped})
    file(STRINGS "${src_file}" lines)
    set(line_no 0)
    set(prev_line "")
    foreach(line IN LISTS lines)
        math(EXPR line_no "${line_no} + 1")
        set(is_comment FALSE)
        if(line MATCHES "^[ \t]*//")
            set(is_comment TRUE)
        endif()
        if(NOT is_comment AND NOT line MATCHES "security-ok:" AND NOT prev_line MATCHES "security-ok:")
            foreach(kind network exec secret)
                foreach(pattern IN LISTS ${kind}_patterns)
                    if(line MATCHES "${pattern}")
                        string(APPEND violations "  ${src_file}:${line_no} [${kind}] ${line}\n")
                    endif()
                endforeach()
            endforeach()
        endif()
        set(prev_line "${line}")
    endforeach()
endforeach()

if(violations)
    message(FATAL_ERROR
        "Garde de securite du compagnon IA violee (reseau / execution de commande / secret) :\n"
        "${violations}"
        "Si c'est volontaire et sur, annoter la ligne avec `security-ok: <raison>`.")
endif()
list(LENGTH scoped scoped_count)
message(STATUS "check_companion_security : OK (${scoped_count} fichiers examines)")
