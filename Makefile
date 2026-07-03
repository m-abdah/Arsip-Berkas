# Makefile untuk ADHARC Archiver
# Support: Linux, Mac, Windows (MinGW/MSYS2)

# Compiler dan flags
CC = gcc
CFLAGS = -O3 -march=native -flto -DNDEBUG -Wall -Wextra
LDFLAGS = -lm
TARGET = adharc

# Deteksi OS
UNAME_S := $(shell uname -s 2>/dev/null || echo Windows)

# Source files
SOURCES = adharc.c
OBJECTS = $(SOURCES:.c=.o)

# Colors
GREEN = \033[0;32m
YELLOW = \033[1;33m
RED = \033[0;31m
NC = \033[0m

.PHONY: all clean install uninstall debug release strip help

all: $(TARGET)
	@echo "$(GREEN)✅ Build successful: $(TARGET)$(NC)"

$(TARGET): $(OBJECTS)
	@echo "$(YELLOW)🔨 Linking...$(NC)"
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "$(GREEN)✓ Linked successfully$(NC)"

%.o: %.c
	@echo "$(YELLOW)📦 Compiling $<...$(NC)"
	$(CC) $(CFLAGS) -c -o $@ $<

# Release build (extra optimizations)
release: CFLAGS += -fomit-frame-pointer -fstrict-aliasing -ffast-math
release: clean all
	@echo "$(GREEN)✅ Release build completed$(NC)"

# Debug build
debug: CFLAGS = -g -O0 -DDEBUG -Wall -Wextra -pedantic
debug: clean all
	@echo "$(GREEN)✅ Debug build completed$(NC)"

# Strip binary (Linux/Mac)
strip: $(TARGET)
	@echo "$(YELLOW)📏 Stripping binary...$(NC)"
	strip $(TARGET)
	@echo "$(GREEN)✓ Stripped: $(shell du -h $(TARGET) | cut -f1)$(NC)"

# Install ke /usr/local/bin (Linux/Mac only)
install: $(TARGET)
	@if [ "$(UNAME_S)" != "Windows" ]; then \
		echo "$(YELLOW)📥 Installing to /usr/local/bin...$(NC)"; \
		sudo install -m 755 $(TARGET) /usr/local/bin/; \
		echo "$(GREEN)✓ Installed successfully$(NC)"; \
	else \
		echo "$(RED)❌ Use compile.bat on Windows$(NC)"; \
	fi

# Uninstall
uninstall:
	@if [ "$(UNAME_S)" != "Windows" ]; then \
		echo "$(YELLOW)🗑 Uninstalling...$(NC)"; \
		sudo rm -f /usr/local/bin/$(TARGET); \
		echo "$(GREEN)✓ Uninstalled$(NC)"; \
	else \
		echo "$(RED)❌ Manual uninstall on Windows$(NC)"; \
	fi

# Clean
clean:
	@echo "$(YELLOW)🧹 Cleaning...$(NC)"
	rm -f $(OBJECTS) $(TARGET) $(TARGET).exe
	@echo "$(GREEN)✓ Cleaned$(NC)"

# Test
test: $(TARGET)
	@echo "$(YELLOW)🧪 Running tests...$(NC)"
	@./$(TARGET) --version
	@echo ""
	@echo "Test 1: Create archive..."
	@echo "test" > /tmp/test.txt
	@./$(TARGET) -f /tmp/test.txt -o /tmp/test.adc 2>&1 | grep "✅" || echo "❌ Failed"
	@echo ""
	@echo "Test 2: List archive..."
	@./$(TARGET) -l /tmp/test.adc 2>&1 | grep "📦" || echo "❌ Failed"
	@echo ""
	@echo "Test 3: Extract archive..."
	@mkdir -p /tmp/test_extract
	@cd /tmp/test_extract && ../../$(TARGET) -x ../test.adc 2>&1 | grep "✅" || echo "❌ Failed"
	@rm -rf /tmp/test.txt /tmp/test.adc /tmp/test_extract
	@echo "$(GREEN)✅ Tests completed$(NC)"

# Help
help:
	@echo "ADHARC Archiver - Build System"
	@echo ""
	@echo "Targets:"
	@echo "  all        Build adharc (default)"
	@echo "  release    Build with extra optimizations"
	@echo "  debug      Build with debug symbols"
	@echo "  strip      Strip binary (reduce size)"
	@echo "  install    Install to /usr/local/bin"
	@echo "  uninstall  Remove from /usr/local/bin"
	@echo "  clean      Remove build artifacts"
	@echo "  test       Run basic tests"
	@echo "  help       Show this help"
