XBE_TITLE = XiFiConfig
GEN_XISO  = $(XBE_TITLE).iso
SRCS      = $(CURDIR)/main.c
NXDK_DIR  ?= $(CURDIR)/../..
NXDK_SDL       = y
NXDK_SDL_TTF   = y
NXDK_SDL_IMAGE = y
NXDK_SDL_AUDIODRV = dsp

SRCS += \
    $(CURDIR)/xifi_detect.c send_cmd.c kybd.c kb_data.c
CFLAGS += -I$(CURDIR)/src

include $(NXDK_DIR)/Makefile
