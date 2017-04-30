TARGET  = plexer
CC      = gcc
COMMIT  = $(shell git rev-parse HEAD)
CFLAGS  = -Wall -I. -Wno-initializer-overrides -D PLXR_COMMIT_HASH=$(COMMIT)
OBJ     = main.o socket.o http.o file.o server.o connection.o
HEADERS =        socket.h http.h file.h server.h connection.h version.h

# where plexer will be installed
INSTALL_DEST = /usr/local/bin

.PHONY: default all clean test install uninstall

default: all
all: $(TARGET) $(OBJ) test

install: $(TARGET)
	chmod 755 ./$(TARGET)
	cp ./$(TARGET) $(INSTALL_DEST)/$(TARGET)

uninstall:
	rm $(INSTALL_DEST)/$(TARGET)

test: $(HEADERS) $(OBJ)
	$(MAKE) $(OBJ)
	cd test && $(MAKE) test
	./test/test

run: $(TARGET)
	./$(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(OBJ) $(CFLAGS) -o $@

debug: $(OBJ)
	$(CC) $(OBJ) $(CFLAGS) -g -o $(TARGET)-debug

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	$(RM) *.o
	$(RM) $(TARGET)-debug
	$(RM) -r $(TARGET)-debug.dSYM
	$(RM) $(TARGET)
