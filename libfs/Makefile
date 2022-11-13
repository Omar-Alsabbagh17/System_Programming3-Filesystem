# Target library
lib 	:= libfs.a
objs	:= disk.o fs.o

CUR_PWD := $(shell pwd)

CC		:=	gcc
CFLAGS	:=	-Wall -Wextra -Werror
CFLAGS	+=	-g

## Dependency generation
CFLAGS	+= 	-MMD

# static library compile
AR = ar
RCS = rcs

# Don't print the commands unless explicitly requested with `make V=1`
ifneq ($(V),1)
Q = @
V = 0
endif
 
all: $(lib)

deps	:=$(patsubst %.o, %.d, $(objs))
-include $(deps)

$(lib): $(objs)
	@echo "CC  $@"
	$(Q)$(AR) $(RCS) $@ $^

# Generic rule for compiling objects
%.o: %.c
	@echo "CC  $@"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

clean:
	@echo "CLEAN  $(CUR_PWD)"
	$(Q)rm -f $(lib) $(objs) $(deps)
