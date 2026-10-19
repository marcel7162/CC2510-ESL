# ============================================================
#  Makefile fuer das CC2510-ESL Firmware (SDCC / 8051)
#  Erkennt automatisch Windows vs. Linux/macOS.
# ============================================================

FLASH_SIZE = 0x7FFF   # 32 KB Flash

XRAM_START = 0xF000
XRAM_SIZE  = 0xF00    # langsamer RAM

IRAM_SIZE  = 0x100    # schneller RAM

TARGET = firmware

CC = sdcc
SDCC_FLAGS = --model-small --opt-code-speed
LDFLAGS_FLASH = \
	--out-fmt-ihx \
	--code-loc 0x000 --code-size $(FLASH_SIZE) \
	--xram-loc $(XRAM_START) --xram-size $(XRAM_SIZE) \
	--iram-size $(IRAM_SIZE)

# ------------------------------------------------------------
#  Plattform-Erkennung: Quelldateien finden + clean-Befehl.
#  Unter Windows setzt die Umgebung OS=Windows_NT.
# ------------------------------------------------------------
ifeq ($(OS),Windows_NT)
	SRC = $(wildcard *.c */*.c */*/*.c)
	CLEAN = for /r %%f in (*.rel *.asm *.lst *.sym *.rst *.lk *.map *.mem *.ihx) do del /q "%%f" 2>nul
else
	SRC = $(shell find . -type f -name "*.c")
	CLEAN = find . -type f \( -name '*.rel' -o -name '*.asm' -o -name '*.lst' \
	          -o -name '*.sym' -o -name '*.rst' -o -name '*.lk' -o -name '*.map' \
	          -o -name '*.mem' -o -name '*.ihx' \) -delete
endif

REL = $(patsubst %.c, %.rel, $(SRC))

all: $(TARGET).hex

%.rel : %.c Makefile
	$(CC) -c $(SDCC_FLAGS) -DBUILD -c -o $*.rel $<

$(TARGET).ihx: $(REL)
	$(CC) $(LDFLAGS_FLASH) $(SDCC_FLAGS) -o $(TARGET).ihx $(REL)

$(TARGET).hex: $(TARGET).ihx
	packihx $(TARGET).ihx > $(TARGET).hex

clean:
	@echo Loesche generierte Dateien...
	@$(CLEAN)
	@echo Bereinigung abgeschlossen.

.PHONY: all clean
