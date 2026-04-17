PREFIX    ?= /usr/local
BINDIR     = $(PREFIX)/bin
MANDIR     = $(PREFIX)/share/man/man1

BINARY     = issex
MANPAGE    = issex.1

.PHONY: install uninstall

install: $(BINARY) $(MANPAGE) $(ELISP)
	@echo "Installing binary  -> $(BINDIR)/$(BINARY)"
	@install -d $(BINDIR)
	@install -m 755 $(BINARY) $(BINDIR)/$(BINARY)

	@echo "Installing man page -> $(MANDIR)/$(MANPAGE)"
	@install -d $(MANDIR)
	@install -m 644 $(MANPAGE) $(MANDIR)/$(MANPAGE)

uninstall:
	@echo "Removing $(BINDIR)/$(BINARY)"
	@rm -f $(BINDIR)/$(BINARY)
	@echo "Removing $(MANDIR)/$(MANPAGE)"
	@rm -f $(MANDIR)/$(MANPAGE)
	@echo "Done."
