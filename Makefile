############################################################

CC			=  gcc -std=c11
AR			=  ar

ifdef DEBUG
CFLAGS         += -Wall -g -pedantic -fno-inline-functions -DDEBUG
else
CFLAGS	       += -Wall
OPTFLAGS       += -O3 -DNDEBUG
endif

ARFLAGS		=  rvs
INCLUDES	= -I.
LDFLAGS 	= -L.
LIBS		= -lrt -lpthread

############################################################

TARNAME = YuriyRymarchuk-614484

FILES_TO_ARCHIVE =	Makefile farm.c generafile.c test.sh \
					boundedqueue.c \
					util.h boundedqueue.h \
					RelazioneProgetto.pdf

TARGETS			= farm

OBJECTS			= boundedqueue.o

INCLUDE_FILES   =	util.h \
					boundedqueue.h

############################################################

.PHONY: all clean cleanall zip
.SUFFIXES: .c .h

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) $(OPTFLAGS) -c -o $@ $<

%: %.c
	$(CC) $(CFLAGS) $(INCLUDES) $(OPTFLAGS) -o $@ $< $(LDFLAGS) 

all		: $(TARGETS)

farm: farm.o libfarm.a $(INCLUDE_FILES)
	$(CC) $(CFLAGS) $(INCLUDES) $(OPTFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS)
	@make cleanobj

libfarm.a: $(OBJECTS)
	$(AR) $(ARFLAGS) $@ $^

############################################################

generafile: generafile.o
	$(CC) $(CFLAGS) $(INCLUDES) $(OPTFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS)
	@make cleanobj

clean		:
	@rm -f $(TARGETS)

cleanall	: clean
	@rm -f *.o *~ libfarm.a

cleanobj	:
	@rm -f *.o libfarm.a

zip			:
	tar -czvf $(TARNAME).tar.gz $(FILES_TO_ARCHIVE)
	@echo "*** Tar created: $(TARNAME).tar.gz "

############################################################
