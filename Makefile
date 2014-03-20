all:: run

OUTPUT		= lz4mt
SRCDIR		= src
OBJDIR		= obj

CC		= gcc
CXX		= g++
##CXX		= g++-4.6
DUMPMACHINE	= $(shell gcc -dumpmachine)

CFLAGS		= -Wall -W -Wextra -pedantic -O2 -std=c99
CXXFLAGS	= -Wall -W -Wextra -pedantic -Weffc++ -Wno-missing-field-initializers -O2 -std=c++0x -Ilz4/ -Ilz4/programs

LD		= $(CXX)
LDFLAGS		=

ifneq (, $(findstring darwin, $(DUMPMACHINE)))
	## Mac OS X
	LDFLAGS	+= -pthread
else
	## Linux, etc
	LDFLAGS	+= -lrt -pthread
endif

SRCS		= $(wildcard $(SRCDIR)/*.cpp)
OBJS		= $(addprefix $(OBJDIR)/,$(notdir $(SRCS:.cpp=.o)))

LZ4_SRCS	= lz4/lz4.c lz4/lz4hc.c lz4/programs/xxhash.c
LZ4_OBJS	= $(addprefix obj/,$(notdir $(LZ4_SRCS:.c=.o)))

ENWIK		= enwik8
VALGRIND	= valgrind

##
ifeq ($(wildcard $(OBJDIR)),)
	TSETUP = setup
else
	TSETUP =
endif


##
run: $(TSETUP) $(OUTPUT)
	-./$(OUTPUT) -H

debug: CFLAGS   += -DDEBUG -g -O0
debug: CXXFLAGS += -DDEBUG -g -O0
debug: $(TSETUP) $(OUTPUT)

$(OUTPUT): $(OBJS) $(LZ4_OBJS)
	$(LD) -o $@ $^ $(LDFLAGS)

obj/%.o: src/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

obj/%.o: lz4/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

obj/%.o: lz4/programs/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

setup:
	-@mkdir $(OBJDIR)

clean: clean-output
	-@rm -f *.linux.lz4.c*
	-@rm -f *.linux.lz4.d*

clean-output:
	-@rm -f $(OBJDIR)/*
	-@rmdir $(OBJDIR) 2> /dev/null || true
	-@rm -f $(OUTPUT)

test:
	-@rm -f *.linux.lz4.c*
	-@rm -f *.linux.lz4.d*
	./$(OUTPUT) -c0 $(ENWIK) $(ENWIK).linux.lz4.c0
	./$(OUTPUT) -c1 $(ENWIK) $(ENWIK).linux.lz4.c1
	./$(OUTPUT) -d $(ENWIK).linux.lz4.c0 $(ENWIK).linux.lz4.d0
	./$(OUTPUT) -d $(ENWIK).linux.lz4.c1 $(ENWIK).linux.lz4.d1
	md5sum $(ENWIK) $(ENWIK).linux.lz4.d* $(ENWIK).linux.lz4.c*

test-valgrind-decompress: clean-output setup debug
	-@rm -f *.linux.lz4.c*
	-@rm -f *.linux.lz4.d*
	./$(OUTPUT) -c0 -y $(ENWIK) $(ENWIK).linux.lz4.c0
	$(VALGRIND) --leak-check=full --tool=memcheck ./$(OUTPUT) -d -y $(ENWIK).linux.lz4.c0 $(ENWIK).linux.lz4.d0 2> lz4mt-valgrind-memcheck.log
	$(VALGRIND) --tool=helgrind ./$(OUTPUT) -d -y $(ENWIK).linux.lz4.c0 $(ENWIK).linux.lz4.d0 2> lz4mt-valgrind-helgrind.log
	$(VALGRIND) --tool=drd ./$(OUTPUT) -d -y $(ENWIK).linux.lz4.c0 $(ENWIK).linux.lz4.d0 2> lz4mt-valgrind-drd.log
	md5sum $(ENWIK) $(ENWIK).linux.lz4.d* $(ENWIK).linux.lz4.c*
