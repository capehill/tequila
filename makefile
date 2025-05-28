NAME = Tequila

CC = ppc-amigaos-gcc
CFLAGS = -Wall -Wextra -Wpedantic -Wconversion -Werror -O3 -gstabs
AMIGADATE = $(shell date LFORMAT "%-d.%-m.%Y")
SRCS = $(wildcard src/*.c)
OBJS = $(SRCS:.c=.o)
DEPS = $(OBJS:.o=.d)
LOCALE_DESCRIPTOR = translations/tequila.cd
LOCALE_TEMPLATE = translations/tequila.ct
FINNISH_TEMPLATE = translations/finnish.ct

all: src/locale_generated.h $(NAME)

%.o : %.c
	$(CC) -o $@ -c $< $(CFLAGS) -D__AMIGA_DATE__="$(AMIGADATE)"

$(NAME): $(OBJS)
	$(CC) -o $@ $(OBJS) -lauto

src/locale_generated.h: $(LOCALE_DESCRIPTOR)
	CATCOMP $(LOCALE_DESCRIPTOR) CFILE $@

$(LOCALE_TEMPLATE): $(LOCALE_DESCRIPTOR)
	CATCOMP $< CTFILE $(LOCALE_TEMPLATE)

#$(FINNISH_TEMPLATE): $(LOCALE_TEMPLATE)
#	 copy $< $@

translations/finnish.catalog: $(LOCALE_DESCRIPTOR) $(FINNISH_TEMPLATE)
	CATCOMP $(LOCALE_DESCRIPTOR) $(FINNISH_TEMPLATE) CATALOG $@

catalogs: translations/finnish.catalog
	copy $< Catalogs/finnish/tequila.catalog

strip:
	ppc-amigaos-strip $(NAME)

# Dependencies
%.d : %.c
	$(CC) -MM -MP -MT $(@:.d=.o) -o $@ $< $(CFLAGS)

clean:
	rm $(NAME) $(OBJS) $(DEPS)

ifneq ($(MAKECMDGOALS),clean)
-include $(DEPS)
endif
