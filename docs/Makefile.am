EXTRA_DIST = jacksampler.1.doxi jacksamplerdefs.doxi

dist_man_MANS = jacksampler.1

DOCS_DIR = ~/public_html/jacksampler/docs-$(VERSION)
DOXER = ~/src/beast/doxer/doxer.py  

rebuild-manpage:
	$(DOXER) doxi2man jacksampler.1.doxi

publish-docs:
	mkdir -p $(DOCS_DIR)
	for i in $(dist_man_MANS); do \
	  $(DOXER) doxi2html $$i.doxi; \
	  cp $$i.html $(DOCS_DIR); \
          rm $$i.html ; \
        done
	cd $(DOCS_DIR) && find . -type f -print0 | xargs -0 git add
