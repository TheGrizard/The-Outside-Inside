# desktop renderer: make -C app/tools, then app/tools/render ENV SECONDS SEED OUT.wav (fm1-base in base/, or set FM1_BASE)
CFLAGS = -O2 -std=gnu11 -Wall -Wextra -Wno-unused-parameter -Wno-misleading-indentation -ffp-contract=off -I$(FM1_BASE)/include
FM1_BASE ?= ../../base
render: ../dsp/*.c ../dsp/*.h render.c
	$(CC) $(CFLAGS) -o render render.c ../dsp/generators.c ../dsp/engine.c ../dsp/patches.c -lm
