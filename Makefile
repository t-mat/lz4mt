all:: run

OUTPUT		= lz4mt
SRCDIR		= src
OBJDIR		= obj

CC		= gcc
CXX		= g++

CFLAGS		= -Wall -O2 -std=c99
CXXFLAGS	= -Wall -O2 -std=c++0x -Ilz4/

LD		= $(CXX)
LDFLAGS		= -lrt -pthread

SRCS		= $(wildcard $(SRCDIR)/*.cpp)
OBJS		= $(addprefix $(OBJDIR)/,$(notdir $(SRCS:.cpp=.o)))

LZ4_SRCS	= lz4/lz4.c lz4/lz4hc.c lz4/xxhash.c
LZ4_OBJS	= $(addprefix obj/,$(notdir $(LZ4_SRCS:.c=.o)))

ENWIK		= enwik8

##
ifeq ($(wildcard $(OBJDIR)),)
	TSETUP = setup
else
	TSETUP =
endif


##
run: $(TSETUP) $(OUTPUT)
	./$(OUTPUT) --help

$(OUTPUT): $(OBJS) $(LZ4_OBJS)
	$(LD) -o $@ $^ $(LDFLAGS)

obj/%.o: src/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

obj/%.o: lz4/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

setup:
	-mkdir $(OBJDIR)

clean:
	-rm -f $(OBJDIR)/*
	-rmdir $(OBJDIR)
	-rm -f $(OUTPUT)

test:
	./$(OUTPUT) -c0 $(ENWIK) $(ENWIK).linux.c0
	./$(OUTPUT) -c1 $(ENWIK) $(ENWIK).linux.c1
	./$(OUTPUT) -d $(ENWIK).linux.c0 $(ENWIK).linux.d0
	./$(OUTPUT) -d $(ENWIK).linux.c1 $(ENWIK).linux.d1
