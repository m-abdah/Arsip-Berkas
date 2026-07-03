#!/bin/bash

# ============================================
#  Compile Script untuk ADHARC Archiver
#  Support: Linux & macOS
# ============================================

set -e  # Exit on error

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Configuration
SOURCE_FILE="adharc.c"
OUTPUT_NAME="adharc"
CFLAGS_BASE="-Wall -Wextra"
LDFLAGS="-lm"

# Detect OS and set optimization flags
detect_os() {
    case "$(uname -s)" in
        Linux*)  
            PLATFORM="Linux"
            CFLAGS_OPT="-O3 -march=native -flto -DNDEBUG"
            ;;
        Darwin*) 
            PLATFORM="macOS"
            CFLAGS_OPT="-O3 -march=native -flto -DNDEBUG"
            # macOS可能需要Xcode CLT
            if ! command -v gcc &> /dev/null; then
                echo -e "${YELLOW}⚠ gcc not found, using cc (clang)...${NC}"
                CC="cc"
            else
                CC="gcc"
            fi
            ;;
        *)       
            PLATFORM="Unknown"
            CFLAGS_OPT="-O3 -DNDEBUG"
            ;;
    esac
}

# Show banner
show_banner() {
    echo -e "${CYAN}"
    echo "╔═══════════════════════════════════════╗"
    echo "║     ADHARC Archiver - Compiler       ║"
    echo "║     Version 5.0.0                    ║"
    echo "╚═══════════════════════════════════════╝"
    echo -e "${NC}"
}

# Check dependencies
check_deps() {
    echo -e "${YELLOW}🔍 Checking dependencies...${NC}"
    
    if ! command -v ${CC:-gcc} &> /dev/null; then
        echo -e "${RED}❌ GCC/CC not found! Install build-essential or Xcode CLT${NC}"
        exit 1
    fi
    
    echo -e "${GREEN}✓ Compiler found: ${CC:-gcc} $(${CC:-gcc} --version | head -n1)${NC}"
}

# Check source file
check_source() {
    if [ ! -f "$SOURCE_FILE" ]; then
        echo -e "${RED}❌ Source file '$SOURCE_FILE' not found!${NC}"
        echo -e "${YELLOW}Make sure you're in the correct directory.${NC}"
        exit 1
    fi
    echo -e "${GREEN}✓ Source file found: $SOURCE_FILE${NC}"
}

# Compile function
compile() {
    local mode="$1"
    local cflags="$CFLAGS_BASE"
    
    case "$mode" in
        release)
            echo -e "\n${BLUE}🚀 Compiling RELEASE version...${NC}"
            cflags="$cflags $CFLAGS_OPT -fomit-frame-pointer"
            ;;
        debug)
            echo -e "\n${BLUE}🐛 Compiling DEBUG version...${NC}"
            cflags="-g -O0 -DDEBUG -Wall -Wextra -pedantic"
            ;;
        fast)
            echo -e "\n${BLUE}⚡ Compiling FAST version...${NC}"
            cflags="$cflags $CFLAGS_OPT -ffast-math -funroll-loops"
            ;;
        small)
            echo -e "\n${BLUE}📦 Compiling SMALL version...${NC}"
            cflags="$cflags -Os -s -DNDEBUG"
            ;;
        *)
            echo -e "\n${BLUE}📦 Compiling STANDARD version...${NC}"
            cflags="$cflags $CFLAGS_OPT"
            ;;
    esac
    
    echo -e "${YELLOW}Compiling with flags:${NC}"
    echo -e "  ${cflags}"
    echo ""
    
    # Actual compilation
    if ${CC:-gcc} $cflags -o "$OUTPUT_NAME" "$SOURCE_FILE" $LDFLAGS 2>&1 | tee /tmp/adharc_build.log; then
        echo ""
        echo -e "${GREEN}═══════════════════════════════════════${NC}"
        echo -e "${GREEN}✅ Compilation successful!${NC}"
        echo -e "${GREEN}═══════════════════════════════════════${NC}"
        
        # Show binary info
        if [ -f "$OUTPUT_NAME" ]; then
            SIZE=$(du -h "$OUTPUT_NAME" | cut -f1)
            echo -e "\n${CYAN}📊 Binary Information:${NC}"
            echo -e "  Name: $(file "$OUTPUT_NAME" | cut -d: -f2-)"
            echo -e "  Size: ${SIZE}"
            
            if [ "$mode" != "debug" ] && command -v strip &> /dev/null; then
                echo -e "\n${YELLOW}💡 Tip: Run './compile.sh strip' to reduce binary size${NC}"
            fi
        fi
        
        return 0
    else
        echo ""
        echo -e "${RED}═══════════════════════════════════════${NC}"
        echo -e "${RED}❌ Compilation failed!${NC}"
        echo -e "${RED}═══════════════════════════════════════${NC}"
        echo -e "${YELLOW}Check /tmp/adharc_build.log for details${NC}"
        return 1
    fi
}

# Strip binary
strip_binary() {
    if [ ! -f "$OUTPUT_NAME" ]; then
        echo -e "${RED}❌ Binary not found. Compile first!${NC}"
        exit 1
    fi
    
    if command -v strip &> /dev/null; then
        echo -e "${YELLOW}📏 Stripping $OUTPUT_NAME...${NC}"
        BEFORE=$(du -h "$OUTPUT_NAME" | cut -f1)
        strip "$OUTPUT_NAME"
        AFTER=$(du -h "$OUTPUT_NAME" | cut -f1)
        echo -e "${GREEN}✓ Stripped: $BEFORE → $AFTER${NC}"
    else
        echo -e "${YELLOW}⚠ strip not available, skipping${NC}"
    fi
}

# Install binary
install_binary() {
    if [ ! -f "$OUTPUT_NAME" ]; then
        echo -e "${RED}❌ Binary not found. Compile first!${NC}"
        exit 1
    fi
    
    INSTALL_DIR="/usr/local/bin"
    echo -e "${YELLOW}📥 Installing to $INSTALL_DIR...${NC}"
    
    if [ -w "$INSTALL_DIR" ]; then
        install -m 755 "$OUTPUT_NAME" "$INSTALL_DIR/"
    else
        echo -e "${YELLOW}🔐 Need sudo to install...${NC}"
        sudo install -m 755 "$OUTPUT_NAME" "$INSTALL_DIR/"
    fi
    
    echo -e "${GREEN}✓ Installed successfully!${NC}"
    echo -e "  Run: ${CYAN}$OUTPUT_NAME --help${NC}"
}

# Clean artifacts
clean() {
    echo -e "${YELLOW}🧹 Cleaning build artifacts...${NC}"
    rm -f "$OUTPUT_NAME" *.o /tmp/adharc_build.log
    echo -e "${GREEN}✓ Cleaned${NC}"
}

# Test binary
test_binary() {
    if [ ! -f "$OUTPUT_NAME" ]; then
        echo -e "${RED}❌ Binary not found. Compile first!${NC}"
        exit 1
    fi
    
    echo -e "${CYAN}🧪 Running tests...${NC}\n"
    
    # Test 1: Version
    echo -e "${YELLOW}Test 1: Version${NC}"
    ./"$OUTPUT_NAME" --version
    
    # Test 2: Help
    echo -e "\n${YELLOW}Test 2: Help${NC}"
    ./"$OUTPUT_NAME" --help | head -n 5
    
    # Test 3: Create & extract
    echo -e "\n${YELLOW}Test 3: Create archive${NC}"
    echo "Hello World" > /tmp/test_adharc.txt
    ./"$OUTPUT_NAME" -f /tmp/test_adharc.txt -o /tmp/test_adharc.adc
    echo -e "\n${YELLOW}Test 4: List archive${NC}"
    ./"$OUTPUT_NAME" -l /tmp/test_adharc.adc
    echo -e "\n${YELLOW}Test 5: Extract archive${NC}"
    mkdir -p /tmp/test_adharc_out
    cd /tmp/test_adharc_out && ../test_adharc.adc 2>/dev/null || ../../"$OUTPUT_NAME" -x ../test_adharc.adc
    cat /tmp/test_adharc_out/test_adharc.txt 2>/dev/null
    
    # Cleanup
    rm -rf /tmp/test_adharc.txt /tmp/test_adharc.adc /tmp/test_adharc_out
    echo -e "\n${GREEN}✅ All tests passed!${NC}"
}

# Show help
show_help() {
    echo "ADHARC Archiver - Compile Script"
    echo ""
    echo "Usage: ./compile.sh [option]"
    echo ""
    echo "Options:"
    echo "  (none)    Standard optimized build"
    echo "  release   Maximum optimization build"
    echo "  fast      Aggressive optimization build"
    echo "  debug     Debug build (no optimization)"
    echo "  small     Size-optimized build"
    echo "  strip     Strip binary (reduce size)"
    echo "  install   Install to /usr/local/bin"
    echo "  test      Run basic tests"
    echo "  clean     Remove build artifacts"
    echo "  help      Show this help"
    echo ""
    echo "Examples:"
    echo "  ./kompilasi.sh              # Standard build"
    echo "  ./kompilasi.sh release      # Release build"
    echo "  ./kompilasi.sh debug        # Debug build"
}

# Main
main() {
    detect_os
    show_banner
    
    echo -e "Platform: ${GREEN}$PLATFORM${NC}"
    echo -e "Compiler: ${GREEN}${CC:-gcc}${NC}"
    echo ""
    
    case "${1:-build}" in
        release|debug|fast|small)
            check_source
            check_deps
            compile "$1"
            ;;
        build)
            check_source
            check_deps
            compile "standard"
            ;;
        strip)
            strip_binary
            ;;
        install)
            install_binary
            ;;
        test)
            test_binary
            ;;
        clean)
            clean
            ;;
        help|--help|-h)
            show_help
            ;;
        *)
            echo -e "${RED}Unknown option: $1${NC}"
            show_help
            exit 1
            ;;
    esac
}

main "$@"
