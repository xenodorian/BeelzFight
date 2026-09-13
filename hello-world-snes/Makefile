TARGET  = hello.sfc
CFG     = snes-lorom.cfg
SRC     = src/hello.s
OBJ     = src/hello.o

all: $(TARGET)

$(OBJ): $(SRC)
	ca65 --cpu 65816 -o $(OBJ) $(SRC)

$(TARGET): $(OBJ) $(CFG)
	ld65 -C $(CFG) -o $(TARGET) $(OBJ)
	python3 tools/fix_checksum.py $(TARGET)

clean:
	rm -f $(OBJ) $(TARGET)

.PHONY: all clean
