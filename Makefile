all:: run

OUTPUT		= lz4mt
SRCDIR		= src
OBJDIR		= obj

CC		= gcc
CXX		= g++

CFLAGS		= -Wall -W -Wextra -pedantic -O2 -std=c99
CXXFLAGS	= -DLZ4_USE_THREADPOOL -Wall -W -Wextra -pedantic -Weffc++ -Wno-missing-field-initializers -O2 -std=c++0x -Ilz4/

GCC46		= gcc-4.6
GXX46		= g++-4.6
GCC46_FLAGS	= -Wall -W -Wextra -pedantic -O2 -std=c99
GXX46_FLAGS	= -DLZ4_USE_THREADPOOL -Wall -W -Wextra -pedantic -Weffc++ -Wno-missing-field-initializers -O2 -std=c++0x -Ilz4/

CLANG		= clang
CLANGXX		= clang++
CLANG_FLAGS	= -Wall -W -Wextra -pedantic -O2 -std=c99 -Wno-static-in-inline
CLANGXX_FLAGS	= -DLZ4_USE_THREADPOOL -Wall -W -Wextra -pedantic -Weffc++ -Wno-missing-field-initializers -O2 -std=c++11 -Ilz4/ -stdlib=libstdc++

ifeq ($(CC),clang)
CC		= $(CLANG)
CXX		= $(CLANGXX)
CFLAGS		= $(CLANG_FLAGS)
CXXFLAGS	= $(CLANGXX_FLAGS)
endif

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
	-./$(OUTPUT) --help

$(OUTPUT): $(OBJS) $(LZ4_OBJS)
	$(LD) -o $@ $^ $(LDFLAGS)

obj/%.o: src/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

obj/%.o: lz4/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

setup:
	-@mkdir $(OBJDIR)

clean:
	-@rm -f *.linux.lz4.c*
	-@rm -f *.linux.lz4.d*
	-@rm -f $(OBJDIR)/*
	-@rmdir $(OBJDIR) 2> /dev/null || true
	-@rm -f $(OUTPUT)

test:
	-@rm -f *.linux.lz4.c*
	-@rm -f *.linux.lz4.d*
	./$(OUTPUT) -y -c0 $(ENWIK) $(ENWIK).linux.lz4.c0
	./$(OUTPUT) -y -c1 $(ENWIK) $(ENWIK).linux.lz4.c1
	./$(OUTPUT) -y -d $(ENWIK).linux.lz4.c0 $(ENWIK).linux.lz4.d0
	./$(OUTPUT) -y -d $(ENWIK).linux.lz4.c1 $(ENWIK).linux.lz4.d1
	-@cmp $(ENWIK) $(ENWIK).linux.lz4.d0
	-@cmp $(ENWIK) $(ENWIK).linux.lz4.d1
