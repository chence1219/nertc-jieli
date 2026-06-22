all: ac791n_nertc_demo

ac791n_nertc_demo:
	$(MAKE) -C board/wl82 -f Makefile

clean_ac791n_nertc_demo:
	$(MAKE) -C board/wl82 -f Makefile clean

clean: clean_ac791n_nertc_demo

.PHONY: all clean ac791n_nertc_demo clean_ac791n_nertc_demo
