#!/bin/bash

# Terminal color codes
RED=$(tput setaf 1)
GREEN=$(tput setaf 2)
YELLOW=$(tput setaf 3)
CYAN=$(tput setaf 6)
BOLD=$(tput bold)
RESET=$(tput sgr0)

OS=$(uname -s)

case "$OS" in
    Linux)
        echo "${GREEN}${BOLD}🛡️  Vaulthalla recognizes this as a worthy environment.${RESET}"
        ;;
    Darwin)
        clear
        cat << EOF
${YELLOW}${BOLD}⛔️  macOS Detected${RESET}

${CYAN}╔══════════════════════════════════════════════════════════════════════╗
║  macOS is not yet battle-tested for Vaulthalla.                       ║
║  Join the contributors and help make it happen:                       ║
║                                                                       ║
║     ⚔️  https://vaulthalla.dev/contribute/mac                         ║
╚══════════════════════════════════════════════════════════════════════╝${RESET}

${YELLOW}Until then, run a Linux VM or boot live media.${RESET}
${CYAN}You're not excluded—just invited to help.${RESET}
EOF
        sleep 2
        exit 1
        ;;
    MINGW* | MSYS* | CYGWIN* | Windows_NT)
        clear
        cat << EOF
${RED}${BOLD}🚨🚨🚨  WINDOWS DETECTED — IMMEDIATE INTERVENTION REQUIRED  🚨🚨🚨${RESET}

${RED}╔══════════════════════════════════════════════════════════════════════╗
║  🪟  Windows Users                                                     ║
║                                                                       ║
║  You run Windows? Cool.                                               ║
║                                                                       ║
║  If you want to run Vaulthalla:                                       ║
║    - Install a Linux VM.                                              ║
║    - Or boot from USB like a grown adult.                             ║
║    - Or SSH into a real server like the rest of us.                   ║
║                                                                       ║
║  Whatever crimes your hypervisor commits to bridge FUSE with NTFS—    ║
║  it's not our problem.                                                ║
║                                                                       ║
║  We don’t patch for Windows.                                          ║
║  We don’t test on Windows.                                            ║
║  And we certainly don’t apologize for it.                             ║
╚══════════════════════════════════════════════════════════════════════╝${RESET}

${YELLOW}This isn’t personal. It’s architectural. Return with a real OS.${RESET}
EOF
        sleep 3
        exit 1
        ;;
    *)
        echo "${YELLOW}${BOLD}🧐 Unknown OS: $OS${RESET}"
        echo "${CYAN}Proceeding... but understand this is unsupported terrain.${RESET}"
        ;;
esac
