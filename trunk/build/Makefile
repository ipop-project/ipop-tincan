# all: library.cpp main.cpp
# $@ evaluates to all, $< evaluates to library.cpp, $^ evaluates to library.cpp main.cpp
BINARY = ipop-tincan
CC = g++
IDIR_I = ../include
IDIR_L = ../include/linux
IDIR_E = ../../external/include
CFLAGS = -std=c++14 -Wall -Wno-unknown-pragmas -Wno-unused-variable -Wno-unused-value -fno-rtti -I$(IDIR_I) -I$(IDIR_E) -I$(IDIR_L) -D LINUX -D WEBRTC_POSIX -D _IPOP_LINUX
SRCDIR = ../src
LIN_SRC = ../src/linux
LDIR = -L ../../external/lib/debug_x64/linux
LIBS = -lrtc_p2p -lrtc_base -ljsoncpp -lrtc_base_approved -lrtc_xmpp -lrtc_xmllite -lpthread \
-lfield_trial_default -lexpat -lboringssl -lrt -lboringssl_asm
ODIR = ../out/debug/obj
BDIR = ../out/debug
TARGET = $(patsubst %,$(BDIR)/%,$(BINARY))
_OBJ = control_dispatch.o control_listener.o peer_network.o tincan.o tincan_control.o\
tincan_main.o virtual_link.o virtual_network.o tap_frame.o
LIN_OBJ_=tapdev_lnx.o
OBJ = $(patsubst %,$(ODIR)/%,$(_OBJ))
LIN_OBJ = $(patsubst %,$(ODIR)/%,$(LIN_OBJ_))
HDRS = $(IDIR_I)/%.h $(IDIR_L)/%.h

$(ODIR)/%.o: $(SRCDIR)/%.cc
	if [ ! -d $(ODIR) ]; then  mkdir -p $(ODIR); fi
	$(CC) -c -o $@ $< $(CFLAGS)

$(TARGET): $(OBJ) $(LIN_OBJ)
	$(CC) -o $@ $^ $(LDIR) $(LIBS)

$(LIN_OBJ): $(LIN_SRC)/tapdev_lnx.cc
	$(CC) -c $< -o $@ $(CFLAGS)

clean:
	rm -rf $(TARGET) $(ODIR) *~
